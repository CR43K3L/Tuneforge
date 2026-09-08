#include "hw/sensors.hpp"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>

#include <algorithm>
#include <cstring>

#include "common/util.hpp"

namespace tf::hw {
namespace {

uint64_t to_u64(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

// Les chaines HWiNFO sont en page de codes ANSI (le degre du "°C" notamment).
std::string acp_to_utf8(const char* s, size_t max_len) {
    size_t len = 0;
    while (len < max_len && s[len]) ++len;
    if (len == 0) return {};
    int wneed = ::MultiByteToWideChar(CP_ACP, 0, s, static_cast<int>(len), nullptr, 0);
    if (wneed <= 0) return std::string(s, len);
    std::wstring w(static_cast<size_t>(wneed), L'\0');
    ::MultiByteToWideChar(CP_ACP, 0, s, static_cast<int>(len), w.data(), wneed);
    return narrow(w);
}

bool contains_ci(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    const std::string h = lower(haystack);
    const std::string n = lower(needle);
    return h.find(n) != std::string::npos;
}

} // namespace

const char* to_string(ReadingType t) {
    switch (t) {
        case ReadingType::Temperature: return "temperature";
        case ReadingType::Voltage:     return "tension";
        case ReadingType::Fan:         return "ventilateur";
        case ReadingType::Current:     return "courant";
        case ReadingType::Power:       return "puissance";
        case ReadingType::Clock:       return "frequence";
        case ReadingType::Usage:       return "charge";
        case ReadingType::Other:       return "autre";
        default:                       return "aucun";
    }
}

// ===========================================================================
// Compteurs systeme
// ===========================================================================
SystemSnapshot SystemCounters::sample() {
    SystemSnapshot s;

    FILETIME idle{}, kernel{}, user{};
    if (::GetSystemTimes(&idle, &kernel, &user)) {
        const uint64_t i = to_u64(idle), k = to_u64(kernel), u = to_u64(user);
        if (primed_) {
            const uint64_t di = i - prev_idle_;
            const uint64_t dk = k - prev_kernel_;
            const uint64_t du = u - prev_user_;
            const uint64_t total = dk + du;  // kernel inclut deja idle
            if (total > 0) {
                s.cpu_load_pct = 100.0 * static_cast<double>(total - di) /
                                 static_cast<double>(total);
                s.cpu_load_pct = (std::max)(0.0, (std::min)(100.0, s.cpu_load_pct));
            }
        }
        prev_idle_ = i;
        prev_kernel_ = k;
        prev_user_ = u;
        primed_ = true;
    }

    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (::GlobalMemoryStatusEx(&ms)) {
        s.ram_total_bytes = ms.ullTotalPhys;
        s.ram_used_bytes  = ms.ullTotalPhys - ms.ullAvailPhys;
        s.ram_pct         = static_cast<double>(ms.dwMemoryLoad);
    }

    DWORD pids[2048];
    DWORD bytes = 0;
    if (::EnumProcesses(pids, sizeof(pids), &bytes)) {
        s.process_count = bytes / sizeof(DWORD);
    }
    s.uptime_seconds = ::GetTickCount64() / 1000ull;
    return s;
}

// ===========================================================================
// Frequence processeur (PDH)
// ===========================================================================
bool CpuFrequency::init(uint32_t base_mhz) {
    base_mhz_ = base_mhz;
    if (base_mhz_ == 0) {
        log_debug("frequence de base inconnue : mesure de frequence desactivee");
        return false;
    }

    PDH_HQUERY   q = nullptr;
    PDH_HCOUNTER c = nullptr;
    if (::PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS) {
        log_warn("PdhOpenQuery a echoue : frequence processeur indisponible");
        return false;
    }
    // PdhAddEnglishCounter : le nom du compteur est localise dans Windows, la
    // variante « English » est la seule qui fonctionne quelle que soit la langue.
    if (::PdhAddEnglishCounterW(q, L"\\Processor Information(_Total)\\% Processor Performance",
                                0, &c) != ERROR_SUCCESS) {
        log_warn("compteur « % Processor Performance » introuvable");
        ::PdhCloseQuery(q);
        return false;
    }
    ::PdhCollectQueryData(q);  // premiere collecte : sert de reference
    query_ = q;
    counter_ = c;
    return true;
}

CpuFrequency::~CpuFrequency() {
    if (query_) ::PdhCloseQuery(static_cast<PDH_HQUERY>(query_));
}

float CpuFrequency::sample_mhz() {
    if (!counter_) return -1.0f;
    if (::PdhCollectQueryData(static_cast<PDH_HQUERY>(query_)) != ERROR_SUCCESS) return -1.0f;

    PDH_FMT_COUNTERVALUE v{};
    DWORD                type = 0;
    if (::PdhGetFormattedCounterValue(static_cast<PDH_HCOUNTER>(counter_), PDH_FMT_DOUBLE,
                                      &type, &v) != ERROR_SUCCESS) {
        return -1.0f;
    }
    if (!primed_) { primed_ = true; return -1.0f; }  // le premier delta n'a pas de sens
    return static_cast<float>(v.doubleValue / 100.0 * base_mhz_);
}

// ===========================================================================
// NVML
// ===========================================================================
namespace {

using nvmlReturn_t = int;
using nvmlDevice_t = void*;

constexpr int NVML_SUCCESS = 0;
constexpr int NVML_TEMPERATURE_GPU = 0;
constexpr int NVML_CLOCK_GRAPHICS = 0;
constexpr int NVML_CLOCK_MEM = 2;

struct nvmlUtilization_t { unsigned int gpu; unsigned int memory; };
struct nvmlMemory_t { unsigned long long total; unsigned long long free; unsigned long long used; };

struct NvmlApi {
    nvmlReturn_t (*Init)() = nullptr;
    nvmlReturn_t (*Shutdown)() = nullptr;
    nvmlReturn_t (*DeviceGetCount)(unsigned int*) = nullptr;
    nvmlReturn_t (*DeviceGetHandleByIndex)(unsigned int, nvmlDevice_t*) = nullptr;
    nvmlReturn_t (*DeviceGetName)(nvmlDevice_t, char*, unsigned int) = nullptr;
    nvmlReturn_t (*DeviceGetTemperature)(nvmlDevice_t, int, unsigned int*) = nullptr;
    nvmlReturn_t (*DeviceGetPowerUsage)(nvmlDevice_t, unsigned int*) = nullptr;
    nvmlReturn_t (*DeviceGetClockInfo)(nvmlDevice_t, int, unsigned int*) = nullptr;
    nvmlReturn_t (*DeviceGetUtilizationRates)(nvmlDevice_t, nvmlUtilization_t*) = nullptr;
    nvmlReturn_t (*DeviceGetFanSpeed)(nvmlDevice_t, unsigned int*) = nullptr;
    nvmlReturn_t (*DeviceGetMemoryInfo)(nvmlDevice_t, nvmlMemory_t*) = nullptr;
};

NvmlApi g_nvml;

template <typename F>
void bind(HMODULE m, F& fn, const char* name) {
    fn = reinterpret_cast<F>(reinterpret_cast<void*>(::GetProcAddress(m, name)));
}

} // namespace

bool Nvml::init() {
    if (ready_) return true;

    HMODULE m = ::LoadLibraryW(L"nvml.dll");
    if (!m) m = ::LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
    if (!m) {
        log_debug("nvml.dll introuvable : telemetrie NVIDIA desactivee");
        return false;
    }
    module_ = m;

    bind(m, g_nvml.Init, "nvmlInit_v2");
    if (!g_nvml.Init) bind(m, g_nvml.Init, "nvmlInit");
    bind(m, g_nvml.Shutdown, "nvmlShutdown");
    bind(m, g_nvml.DeviceGetCount, "nvmlDeviceGetCount_v2");
    if (!g_nvml.DeviceGetCount) bind(m, g_nvml.DeviceGetCount, "nvmlDeviceGetCount");
    bind(m, g_nvml.DeviceGetHandleByIndex, "nvmlDeviceGetHandleByIndex_v2");
    if (!g_nvml.DeviceGetHandleByIndex)
        bind(m, g_nvml.DeviceGetHandleByIndex, "nvmlDeviceGetHandleByIndex");
    bind(m, g_nvml.DeviceGetName, "nvmlDeviceGetName");
    bind(m, g_nvml.DeviceGetTemperature, "nvmlDeviceGetTemperature");
    bind(m, g_nvml.DeviceGetPowerUsage, "nvmlDeviceGetPowerUsage");
    bind(m, g_nvml.DeviceGetClockInfo, "nvmlDeviceGetClockInfo");
    bind(m, g_nvml.DeviceGetUtilizationRates, "nvmlDeviceGetUtilizationRates");
    bind(m, g_nvml.DeviceGetFanSpeed, "nvmlDeviceGetFanSpeed");
    bind(m, g_nvml.DeviceGetMemoryInfo, "nvmlDeviceGetMemoryInfo");

    if (!g_nvml.Init || !g_nvml.DeviceGetCount || !g_nvml.DeviceGetHandleByIndex) {
        log_warn("nvml.dll chargee mais incomplete : telemetrie NVIDIA desactivee");
        ::FreeLibrary(m);
        module_ = nullptr;
        return false;
    }
    if (g_nvml.Init() != NVML_SUCCESS) {
        log_warn("nvmlInit a echoue");
        ::FreeLibrary(m);
        module_ = nullptr;
        return false;
    }

    unsigned int n = 0;
    if (g_nvml.DeviceGetCount(&n) == NVML_SUCCESS) count_ = n;
    ready_ = true;
    log_debug("NVML initialise : {} GPU", count_);
    return true;
}

Nvml::~Nvml() {
    if (ready_ && g_nvml.Shutdown) g_nvml.Shutdown();
    if (module_) ::FreeLibrary(static_cast<HMODULE>(module_));
}

std::optional<GpuTelemetry> Nvml::sample(uint32_t index) {
    if (!ready_ || index >= count_) return std::nullopt;

    nvmlDevice_t dev = nullptr;
    if (g_nvml.DeviceGetHandleByIndex(index, &dev) != NVML_SUCCESS) return std::nullopt;

    GpuTelemetry t;
    t.valid = true;

    char name[128]{};
    if (g_nvml.DeviceGetName && g_nvml.DeviceGetName(dev, name, sizeof(name)) == NVML_SUCCESS) {
        t.name = name;
    }
    unsigned int v = 0;
    if (g_nvml.DeviceGetTemperature &&
        g_nvml.DeviceGetTemperature(dev, NVML_TEMPERATURE_GPU, &v) == NVML_SUCCESS) {
        t.temperature_c = v;
    }
    if (g_nvml.DeviceGetPowerUsage && g_nvml.DeviceGetPowerUsage(dev, &v) == NVML_SUCCESS) {
        t.power_mw = v;
    }
    if (g_nvml.DeviceGetClockInfo &&
        g_nvml.DeviceGetClockInfo(dev, NVML_CLOCK_GRAPHICS, &v) == NVML_SUCCESS) {
        t.clock_graphics_mhz = v;
    }
    if (g_nvml.DeviceGetClockInfo &&
        g_nvml.DeviceGetClockInfo(dev, NVML_CLOCK_MEM, &v) == NVML_SUCCESS) {
        t.clock_memory_mhz = v;
    }
    nvmlUtilization_t u{};
    if (g_nvml.DeviceGetUtilizationRates &&
        g_nvml.DeviceGetUtilizationRates(dev, &u) == NVML_SUCCESS) {
        t.utilization_pct = u.gpu;
        t.memory_util_pct = u.memory;
    }
    if (g_nvml.DeviceGetFanSpeed && g_nvml.DeviceGetFanSpeed(dev, &v) == NVML_SUCCESS) {
        t.fan_pct = v;
    }
    nvmlMemory_t mem{};
    if (g_nvml.DeviceGetMemoryInfo && g_nvml.DeviceGetMemoryInfo(dev, &mem) == NVML_SUCCESS) {
        t.vram_used_bytes  = mem.used;
        t.vram_total_bytes = mem.total;
    }
    return t;
}

// ===========================================================================
// Memoire partagee HWiNFO
// ===========================================================================
namespace {

constexpr size_t kStringLen = 128;
constexpr size_t kUnitLen = 16;

#pragma pack(push, 8)
struct HwiSharedMem {
    uint32_t signature;
    uint32_t version;
    uint32_t revision;
    int64_t  poll_time;
    uint32_t offset_sensor_section;
    uint32_t size_sensor_element;
    uint32_t num_sensor_elements;
    uint32_t offset_reading_section;
    uint32_t size_reading_element;
    uint32_t num_reading_elements;
};

struct HwiSensor {
    uint32_t sensor_id;
    uint32_t sensor_inst;
    char     name_orig[kStringLen];
    char     name_user[kStringLen];
};

struct HwiElement {
    uint32_t reading_type;
    uint32_t sensor_index;
    uint32_t reading_id;
    char     label_orig[kStringLen];
    char     label_user[kStringLen];
    char     unit[kUnitLen];
    double   value;
    double   value_min;
    double   value_max;
    double   value_avg;
};
#pragma pack(pop)

static_assert(sizeof(HwiSharedMem) == 48, "layout memoire partagee HWiNFO inattendu");
static_assert(sizeof(HwiSensor) == 264, "layout capteur HWiNFO inattendu");
static_assert(sizeof(HwiElement) == 320, "layout element HWiNFO inattendu");

constexpr uint32_t kSignature = 0x53695748u;  // "HWiS"

ReadingType map_type(uint32_t t) {
    switch (t) {
        case 1: return ReadingType::Temperature;
        case 2: return ReadingType::Voltage;
        case 3: return ReadingType::Fan;
        case 4: return ReadingType::Current;
        case 5: return ReadingType::Power;
        case 6: return ReadingType::Clock;
        case 7: return ReadingType::Usage;
        case 8: return ReadingType::Other;
        default: return ReadingType::None;
    }
}

} // namespace

HwInfoSensors::~HwInfoSensors() { close(); }

void HwInfoSensors::close() {
    if (view_) { ::UnmapViewOfFile(view_); view_ = nullptr; }
    if (handle_) { ::CloseHandle(static_cast<HANDLE>(handle_)); handle_ = nullptr; }
    readings_.clear();
}

bool HwInfoSensors::open() {
    close();

    HANDLE h = ::OpenFileMappingW(FILE_MAP_READ, FALSE, L"Global\\HWiNFO_SENS_SM2");
    if (!h) h = ::OpenFileMappingW(FILE_MAP_READ, FALSE, L"HWiNFO_SENS_SM2");
    if (!h) {
        log_debug("memoire partagee HWiNFO indisponible "
                  "(HWiNFO64 non lance, ou option « Shared Memory Support » desactivee)");
        return false;
    }
    handle_ = h;

    const void* p = ::MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
    if (!p) {
        log_warn("MapViewOfFile HWiNFO a echoue : {}", last_error());
        close();
        return false;
    }
    view_ = static_cast<const unsigned char*>(p);

    HwiSharedMem hdr{};
    std::memcpy(&hdr, view_, sizeof(hdr));
    if (hdr.signature != kSignature) {
        log_warn("signature HWiNFO inattendue (0x{:08X}) : lecture abandonnee", hdr.signature);
        close();
        return false;
    }
    log_debug("HWiNFO connecte : {} capteurs, {} valeurs", hdr.num_sensor_elements,
              hdr.num_reading_elements);
    return true;
}

bool HwInfoSensors::refresh() {
    if (!view_ && !open()) return false;

    HwiSharedMem hdr{};
    std::memcpy(&hdr, view_, sizeof(hdr));
    if (hdr.signature != kSignature) {
        // HWiNFO a ete ferme : on retente une ouverture propre au prochain tour.
        close();
        return false;
    }
    // Garde-fous : les tailles d'element viennent d'une source externe.
    if (hdr.size_sensor_element < sizeof(HwiSensor) ||
        hdr.size_reading_element < sizeof(HwiElement) ||
        hdr.num_sensor_elements > 512 || hdr.num_reading_elements > 8192) {
        log_warn("en-tete HWiNFO incoherent : lecture abandonnee");
        return false;
    }

    std::vector<std::string> sensor_names;
    sensor_names.reserve(hdr.num_sensor_elements);
    for (uint32_t i = 0; i < hdr.num_sensor_elements; ++i) {
        const unsigned char* base =
            view_ + hdr.offset_sensor_section + static_cast<size_t>(i) * hdr.size_sensor_element;
        HwiSensor s{};
        std::memcpy(&s, base, sizeof(s));
        std::string name = acp_to_utf8(s.name_user, kStringLen);
        if (name.empty()) name = acp_to_utf8(s.name_orig, kStringLen);
        sensor_names.push_back(std::move(name));
    }

    readings_.clear();
    readings_.reserve(hdr.num_reading_elements);
    for (uint32_t i = 0; i < hdr.num_reading_elements; ++i) {
        const unsigned char* base =
            view_ + hdr.offset_reading_section + static_cast<size_t>(i) * hdr.size_reading_element;
        HwiElement e{};
        std::memcpy(&e, base, sizeof(e));

        Reading r;
        r.type  = map_type(e.reading_type);
        r.label = acp_to_utf8(e.label_user, kStringLen);
        if (r.label.empty()) r.label = acp_to_utf8(e.label_orig, kStringLen);
        r.unit      = acp_to_utf8(e.unit, kUnitLen);
        r.value     = e.value;
        r.value_min = e.value_min;
        r.value_max = e.value_max;
        r.value_avg = e.value_avg;
        r.sensor = e.sensor_index < sensor_names.size() ? sensor_names[e.sensor_index]
                                                        : std::string();
        readings_.push_back(std::move(r));
    }
    return true;
}

const Reading* HwInfoSensors::find(std::string_view sensor_contains,
                                   std::string_view label_contains) const {
    for (const auto& r : readings_) {
        if (contains_ci(r.sensor, sensor_contains) && contains_ci(r.label, label_contains)) {
            return &r;
        }
    }
    return nullptr;
}

const Reading* HwInfoSensors::find_any(std::string_view label_contains, ReadingType type) const {
    for (const auto& r : readings_) {
        if (type != ReadingType::None && r.type != type) continue;
        if (contains_ci(r.label, label_contains)) return &r;
    }
    return nullptr;
}

} // namespace tf::hw
