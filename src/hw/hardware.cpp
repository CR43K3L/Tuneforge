#include "hw/hardware.hpp"

#include <windows.h>
#include <dxgi.h>
#include <intrin.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mutex>

#include "common/util.hpp"
#include "platform/power.hpp"
#include "platform/winreg.hpp"

#pragma comment(lib, "dxgi.lib")

namespace tf::hw {
namespace {

// ===========================================================================
// CPUID
// ===========================================================================
struct Regs { int eax, ebx, ecx, edx; };

Regs cpuid(int leaf, int subleaf = 0) {
    int r[4]{};
    __cpuidex(r, leaf, subleaf);
    return Regs{r[0], r[1], r[2], r[3]};
}

void append_regs(std::string& out, const Regs& r) {
    const int vals[4] = {r.eax, r.ebx, r.ecx, r.edx};
    for (int v : vals) {
        for (int b = 0; b < 4; ++b) {
            char c = static_cast<char>((v >> (b * 8)) & 0xFF);
            if (c) out.push_back(c);
        }
    }
}

CpuInfo detect_cpu() {
    CpuInfo ci;

    Regs r0 = cpuid(0);
    {
        char id[13]{};
        std::memcpy(id + 0, &r0.ebx, 4);
        std::memcpy(id + 4, &r0.edx, 4);
        std::memcpy(id + 8, &r0.ecx, 4);
        ci.vendor_id = id;
    }
    if (ci.vendor_id == "GenuineIntel")      ci.vendor = CpuVendor::Intel;
    else if (ci.vendor_id == "AuthenticAMD") ci.vendor = CpuVendor::Amd;

    Regs r1 = cpuid(1);
    const uint32_t base_family = (static_cast<uint32_t>(r1.eax) >> 8) & 0xF;
    const uint32_t base_model  = (static_cast<uint32_t>(r1.eax) >> 4) & 0xF;
    const uint32_t ext_family  = (static_cast<uint32_t>(r1.eax) >> 20) & 0xFF;
    const uint32_t ext_model   = (static_cast<uint32_t>(r1.eax) >> 16) & 0xF;
    ci.stepping = static_cast<uint32_t>(r1.eax) & 0xF;
    ci.family   = (base_family == 0xF) ? base_family + ext_family : base_family;
    ci.model    = (base_family == 0x6 || base_family == 0xF) ? (ext_model << 4) + base_model
                                                             : base_model;
    ci.hyperthreading = (static_cast<uint32_t>(r1.edx) >> 28) & 1;

    if (r0.eax >= 7) {
        Regs r7 = cpuid(7, 0);
        ci.avx2    = ((static_cast<uint32_t>(r7.ebx) >> 5) & 1) != 0;
        ci.avx512f = ((static_cast<uint32_t>(r7.ebx) >> 16) & 1) != 0;
        ci.hybrid  = ((static_cast<uint32_t>(r7.edx) >> 15) & 1) != 0;
    }

    Regs rx = cpuid(static_cast<int>(0x80000000));
    if (static_cast<uint32_t>(rx.eax) >= 0x80000004u) {
        std::string brand;
        append_regs(brand, cpuid(static_cast<int>(0x80000002)));
        append_regs(brand, cpuid(static_cast<int>(0x80000003)));
        append_regs(brand, cpuid(static_cast<int>(0x80000004)));
        ci.brand = trim(brand);
    }
    ci.has_x3d = lower(ci.brand).find("x3d") != std::string::npos;

    // Coeurs physiques / logiques
    SYSTEM_INFO si{};
    ::GetNativeSystemInfo(&si);
    ci.logical_cores = si.dwNumberOfProcessors;

    DWORD bytes = 0;
    ::GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (bytes > 0) {
        std::vector<uint8_t> buf(bytes);
        if (::GetLogicalProcessorInformationEx(
                RelationProcessorCore,
                reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data()), &bytes)) {
            // Les entrees ont une taille variable : borner sur sizeof() de
            // l'union sauterait la derniere, plus courte. Seuls Relationship
            // et Size (deux DWORD) sont garantis presents.
            size_t off = 0;
            while (off + sizeof(DWORD) * 2 <= bytes) {
                auto* e = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + off);
                if (e->Size == 0 || off + e->Size > bytes) break;
                if (e->Relationship == RelationProcessorCore) ++ci.physical_cores;
                off += e->Size;
            }
        }
    }
    if (ci.physical_cores == 0) ci.physical_cores = ci.logical_cores;

    if (auto mhz = reg::read_dword(reg::Root::HKLM,
                                   L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                                   L"~MHz")) {
        ci.base_mhz = *mhz;
    }
    return ci;
}

// ===========================================================================
// SMBIOS
// ===========================================================================
struct SmbiosTable {
    std::vector<uint8_t> data;
    uint8_t              major = 0, minor = 0;
    bool                 valid = false;
};

SmbiosTable read_smbios() {
    SmbiosTable t;
    const DWORD provider = 0x52534D42u;  // 'RSMB'
    UINT size = ::GetSystemFirmwareTable(provider, 0, nullptr, 0);
    if (size == 0) return t;

    std::vector<uint8_t> raw(size);
    if (::GetSystemFirmwareTable(provider, 0, raw.data(), size) != size) return t;
    if (raw.size() < 8) return t;

    t.major = raw[1];
    t.minor = raw[2];
    uint32_t len = 0;
    std::memcpy(&len, raw.data() + 4, 4);
    if (len == 0 || 8 + static_cast<size_t>(len) > raw.size()) len = static_cast<uint32_t>(raw.size() - 8);
    t.data.assign(raw.begin() + 8, raw.begin() + 8 + len);
    t.valid = true;
    return t;
}

struct SmbiosEntry {
    uint8_t                  type = 0;
    const uint8_t*           fmt = nullptr;
    size_t                   fmt_len = 0;
    std::vector<std::string> strings;

    uint8_t byte_at(size_t off) const { return off < fmt_len ? fmt[off] : 0; }
    uint16_t word_at(size_t off) const {
        if (off + 2 > fmt_len) return 0;
        uint16_t v = 0;
        std::memcpy(&v, fmt + off, 2);
        return v;
    }
    uint32_t dword_at(size_t off) const {
        if (off + 4 > fmt_len) return 0;
        uint32_t v = 0;
        std::memcpy(&v, fmt + off, 4);
        return v;
    }
    std::string str_at(size_t off) const {
        uint8_t idx = byte_at(off);
        if (idx == 0 || idx > strings.size()) return {};
        return strings[idx - 1];
    }
};

std::vector<SmbiosEntry> parse_smbios(const SmbiosTable& t) {
    std::vector<SmbiosEntry> out;
    if (!t.valid) return out;

    const uint8_t* p   = t.data.data();
    const uint8_t* end = p + t.data.size();
    while (p + 4 <= end) {
        SmbiosEntry e;
        e.type = p[0];
        const uint8_t hdr_len = p[1];
        if (hdr_len < 4 || p + hdr_len > end) break;

        e.fmt     = p;
        e.fmt_len = hdr_len;

        const uint8_t* s = p + hdr_len;
        // Zone de chaines : suite de chaines nulles, terminee par un double nul.
        if (s + 1 < end && s[0] == 0 && s[1] == 0) {
            p = s + 2;
        } else {
            while (s < end) {
                const uint8_t* start = s;
                while (s < end && *s) ++s;
                e.strings.emplace_back(reinterpret_cast<const char*>(start),
                                       static_cast<size_t>(s - start));
                ++s;  // saute le nul
                if (s < end && *s == 0) { ++s; break; }
            }
            p = s;
        }
        out.push_back(std::move(e));
        if (e.type == 127) break;  // marqueur de fin
    }
    return out;
}

bool chassis_is_mobile(uint8_t t) {
    switch (t) {
        case 8: case 9: case 10: case 11: case 12: case 14:
        case 30: case 31: case 32:
            return true;
        default:
            return false;
    }
}

SystemInfo detect_system(const std::vector<SmbiosEntry>& entries) {
    SystemInfo si;
    for (const auto& e : entries) {
        switch (e.type) {
            case 0:  // BIOS
                if (si.bios_vendor.empty()) {
                    si.bios_vendor  = trim(e.str_at(0x04));
                    si.bios_version = trim(e.str_at(0x05));
                    si.bios_date    = trim(e.str_at(0x08));
                }
                break;
            case 1:  // Systeme
                if (si.manufacturer.empty()) {
                    si.manufacturer = trim(e.str_at(0x04));
                    si.product      = trim(e.str_at(0x05));
                }
                break;
            case 2:  // Carte mere
                if (si.baseboard_vendor.empty()) {
                    si.baseboard_vendor  = trim(e.str_at(0x04));
                    si.baseboard_product = trim(e.str_at(0x05));
                }
                break;
            case 3:  // Chassis
                if (si.chassis_type == 0) si.chassis_type = e.byte_at(0x05) & 0x7F;
                break;
            default:
                break;
        }
    }

    SYSTEM_POWER_STATUS sps{};
    if (::GetSystemPowerStatus(&sps)) {
        si.has_battery = sps.BatteryFlag != 128 && sps.BatteryFlag != 255;
    }
    si.is_laptop = chassis_is_mobile(si.chassis_type) || si.has_battery;
    return si;
}

MemoryInfo detect_memory(const std::vector<SmbiosEntry>& entries) {
    MemoryInfo mi;
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (::GlobalMemoryStatusEx(&ms)) mi.total_bytes = ms.ullTotalPhys;

    for (const auto& e : entries) {
        if (e.type != 17) continue;
        uint16_t size_field = e.word_at(0x0C);
        if (size_field == 0) continue;  // emplacement vide

        MemoryModule m;
        if (size_field == 0x7FFF) {
            m.size_bytes = static_cast<uint64_t>(e.dword_at(0x1C)) * 1024ull * 1024ull;
        } else if (size_field & 0x8000) {
            m.size_bytes = static_cast<uint64_t>(size_field & 0x7FFF) * 1024ull;
        } else {
            m.size_bytes = static_cast<uint64_t>(size_field) * 1024ull * 1024ull;
        }
        // Beaucoup de cartes meres nomment les deux barrettes « DIMM 1 » : le
        // bank locator (canal) est ce qui les distingue reellement.
        const std::string device_loc = trim(e.str_at(0x10));
        const std::string bank_loc   = trim(e.str_at(0x11));
        m.locator = bank_loc.empty() ? device_loc
                                     : (device_loc.empty() ? bank_loc
                                                           : bank_loc + " / " + device_loc);
        m.speed_mts      = e.word_at(0x15);
        m.manufacturer   = trim(e.str_at(0x17));
        m.part_number    = trim(e.str_at(0x1A));
        m.configured_mts = e.word_at(0x20);
        mi.modules.push_back(std::move(m));
    }
    return mi;
}

// ===========================================================================
// GPU
// ===========================================================================
std::string gpu_driver_version(const std::string& description) {
    // Classe « Display adapters »
    const std::wstring root =
        L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";
    for (const auto& sub : reg::enum_subkeys(reg::Root::HKLM, root)) {
        if (sub.size() != 4) continue;  // 0000, 0001, ...
        const std::wstring key = root + L"\\" + sub;
        auto desc = reg::read_string(reg::Root::HKLM, key, L"DriverDesc");
        auto ver  = reg::read_string(reg::Root::HKLM, key, L"DriverVersion");
        if (!desc || !ver) continue;
        if (iequals(narrow(*desc), description)) return narrow(*ver);
    }
    return {};
}

GpuVendor vendor_from_id(uint32_t id) {
    switch (id) {
        case 0x10DE: return GpuVendor::Nvidia;
        case 0x1002:
        case 0x1022: return GpuVendor::Amd;
        case 0x8086: return GpuVendor::Intel;
        default:     return GpuVendor::Unknown;
    }
}

std::vector<GpuInfo> detect_gpus() {
    std::vector<GpuInfo> out;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(::CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                    reinterpret_cast<void**>(&factory))) ||
        !factory) {
        log_warn("DXGI indisponible : enumeration GPU impossible");
        return out;
    }

    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
            !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            GpuInfo g;
            g.description    = narrow(desc.Description);
            g.vendor_id      = desc.VendorId;
            g.device_id      = desc.DeviceId;
            g.vendor         = vendor_from_id(desc.VendorId);
            g.dedicated_vram = desc.DedicatedVideoMemory;
            g.shared_memory  = desc.SharedSystemMemory;
            // Un adaptateur sans VRAM dediee significative est un iGPU.
            g.is_integrated  = desc.DedicatedVideoMemory < (512ull << 20);
            g.driver_version = gpu_driver_version(g.description);
            out.push_back(std::move(g));
        }
        adapter->Release();
        adapter = nullptr;
    }
    factory->Release();
    return out;
}

// ===========================================================================
// OS / securite
// ===========================================================================
OsInfo detect_os() {
    OsInfo os;
    const std::wstring key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    if (auto v = reg::read_string(reg::Root::HKLM, key, L"ProductName"))    os.product_name = narrow(*v);
    if (auto v = reg::read_string(reg::Root::HKLM, key, L"DisplayVersion")) os.display_version = narrow(*v);
    if (auto v = reg::read_string(reg::Root::HKLM, key, L"CurrentBuildNumber")) {
        os.build = static_cast<uint32_t>(std::wcstoul(v->c_str(), nullptr, 10));
    }
    if (auto v = reg::read_dword(reg::Root::HKLM, key, L"UBR")) os.ubr = *v;
    os.is_windows11 = os.build >= 22000;
    if (os.is_windows11 && os.product_name.find("Windows 10") != std::string::npos) {
        // La cle ProductName reste bloquee sur « Windows 10 » sur Win11.
        os.product_name.replace(os.product_name.find("Windows 10"), 10, "Windows 11");
    }
    return os;
}

bool service_exists(const wchar_t* name) {
    return reg::key_exists(reg::Root::HKLM,
                           std::wstring(L"SYSTEM\\CurrentControlSet\\Services\\") + name);
}

SecurityInfo detect_security() {
    SecurityInfo s;
    if (auto v = reg::read_dword(reg::Root::HKLM,
                                 L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State",
                                 L"UEFISecureBootEnabled")) {
        s.secure_boot = (*v != 0);
    }
    if (auto v = reg::read_dword(
            reg::Root::HKLM,
            L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\"
            L"HypervisorEnforcedCodeIntegrity",
            L"Enabled")) {
        s.hvci = (*v != 0);
    }
    if (auto v = reg::read_dword(reg::Root::HKLM,
                                 L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard",
                                 L"EnableVirtualizationBasedSecurity")) {
        s.vbs = (*v != 0);
    }

    struct { const wchar_t* svc; const char* label; } known[] = {
        {L"vgk",                "Riot Vanguard"},
        {L"vgc",                "Riot Vanguard (client)"},
        {L"EasyAntiCheat",      "Easy Anti-Cheat"},
        {L"EasyAntiCheat_EOS",  "Easy Anti-Cheat (EOS)"},
        {L"BEService",          "BattlEye"},
        {L"beep_service",       "BattlEye (driver)"},
        {L"FACEIT",             "FACEIT AC"},
        {L"mhyprot2",           "mhyprot"},
    };
    for (const auto& k : known) {
        if (service_exists(k.svc)) s.kernel_anticheats.emplace_back(k.label);
    }
    // Dedoublonne Vanguard (deux services pour le meme produit).
    auto& v = s.kernel_anticheats;
    v.erase(std::unique(v.begin(), v.end(),
                        [](const std::string& a, const std::string& b) {
                            return starts_with(a, "Riot Vanguard") &&
                                   starts_with(b, "Riot Vanguard");
                        }),
            v.end());
    return s;
}

// ===========================================================================
// Capacites
// ===========================================================================
bool nvml_available() {
    HMODULE h = ::LoadLibraryW(L"nvml.dll");
    if (!h) {
        h = ::LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
    }
    if (!h) return false;
    ::FreeLibrary(h);
    return true;
}

bool hwinfo_shm_available() {
    HANDLE h = ::OpenFileMappingW(FILE_MAP_READ, FALSE, L"Global\\HWiNFO_SENS_SM2");
    if (!h) h = ::OpenFileMappingW(FILE_MAP_READ, FALSE, L"HWiNFO_SENS_SM2");
    if (!h) return false;
    ::CloseHandle(h);
    return true;
}

Capabilities derive_caps(const CpuInfo& cpu, const std::vector<GpuInfo>& gpus,
                         const OsInfo& os, const SecurityInfo& sec) {
    Capabilities c;

    c.power_plans = power::active_scheme().has_value();
    if (!c.power_plans) c.blocked_reasons.emplace_back("API powrprof inaccessible");

    // Un reglage n'est utilisable que s'il est reellement present dans le plan.
    c.core_parking = power::read_ac(power::kSubProcessor, power::kSetCoreParkingMin).has_value();
    c.perf_boost_mode = power::read_ac(power::kSubProcessor, power::kSetPerfBoostMode).has_value();
    c.usb_selective_suspend =
        power::read_ac(power::kSubUsb, power::kSetUsbSelectiveSuspend).has_value();
    c.pcie_aspm = power::read_ac(power::kSubPciExpress, power::kSetPciExpressAspm).has_value();

    const bool has_discrete =
        std::any_of(gpus.begin(), gpus.end(), [](const GpuInfo& g) { return !g.is_integrated; });
    c.hags = has_discrete && os.build >= 19041;
    if (!c.hags) {
        c.blocked_reasons.emplace_back(
            has_discrete ? "HAGS demande Windows 10 2004 ou plus recent"
                         : "aucun GPU dedie detecte");
    }

    c.network_tweaks = !reg::enum_subkeys(
                            reg::Root::HKLM,
                            L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces")
                            .empty();
    c.timer_resolution = true;
    c.hwinfo_shared_memory = hwinfo_shm_available();
    if (!c.hwinfo_shared_memory) {
        c.blocked_reasons.emplace_back(
            "HWiNFO64 absent ou memoire partagee desactivee : capteurs detailles indisponibles");
    }
    c.nvml = nvml_available();

    // v0.1 : strictement usermode. Aucun acces ring 0, quelle que soit la machine.
    c.msr_read = c.msr_write = c.smu_mailbox = false;
    c.blocked_reasons.emplace_back("acces ring 0 non embarque en v0.1 (aucun driver noyau)");
    if (sec.hvci) {
        c.blocked_reasons.emplace_back(
            "HVCI actif : les drivers de la liste de blocage Microsoft seraient refuses");
    }
    if (!sec.kernel_anticheats.empty()) {
        c.blocked_reasons.emplace_back(
            "anticheat noyau present : eviter tout driver de monitoring concurrent");
    }
    (void)cpu;
    return c;
}

std::mutex g_mutex;
Profile*   g_cache = nullptr;

} // namespace

// ===========================================================================
// API publique
// ===========================================================================
const char* to_string(CpuVendor v) {
    switch (v) {
        case CpuVendor::Intel: return "Intel";
        case CpuVendor::Amd:   return "AMD";
        default:               return "inconnu";
    }
}

const char* to_string(GpuVendor v) {
    switch (v) {
        case GpuVendor::Nvidia: return "NVIDIA";
        case GpuVendor::Amd:    return "AMD";
        case GpuVendor::Intel:  return "Intel";
        default:                return "inconnu";
    }
}

std::string CpuInfo::codename() const {
    if (vendor == CpuVendor::Amd) {
        if (family == 0x17) return "Zen / Zen+ / Zen 2";
        if (family == 0x19) {
            if (model >= 0x60 && model <= 0x7F) return "Zen 4";
            if (model >= 0x40 && model <= 0x5F) return "Zen 3+";
            return "Zen 3";
        }
        if (family == 0x1A) return "Zen 5";
        return {};
    }
    if (vendor == CpuVendor::Intel && family == 6) {
        switch (model) {
            case 0x97: case 0x9A: case 0xBE: return "Alder Lake";
            case 0xB7: case 0xBA: case 0xBF: return "Raptor Lake";
            case 0xAA: case 0xAC:            return "Meteor Lake";
            case 0xC6:                       return "Arrow Lake";
            case 0xA5: case 0xA6:            return "Comet Lake";
            case 0x8C: case 0x8D:            return "Tiger Lake";
            default:                         return {};
        }
    }
    return {};
}

const GpuInfo* Profile::primary_gpu() const {
    const GpuInfo* best = nullptr;
    for (const auto& g : gpus) {
        if (g.is_integrated) continue;
        if (!best || g.dedicated_vram > best->dedicated_vram) best = &g;
    }
    if (!best && !gpus.empty()) best = &gpus.front();
    return best;
}

const Profile& detect() {
    std::lock_guard lk(g_mutex);
    if (g_cache) return *g_cache;

    auto* p = new Profile();
    p->cpu = detect_cpu();

    SmbiosTable smb = read_smbios();
    auto entries = parse_smbios(smb);
    p->system = detect_system(entries);
    p->memory = detect_memory(entries);

    p->gpus     = detect_gpus();
    p->os       = detect_os();
    p->security = detect_security();
    p->caps     = derive_caps(p->cpu, p->gpus, p->os, p->security);

    g_cache = p;
    log_info("materiel detecte : {} / {} GPU / Windows build {}", p->cpu.brand,
             p->gpus.size(), p->os.build);
    return *g_cache;
}

void invalidate_cache() {
    std::lock_guard lk(g_mutex);
    delete g_cache;
    g_cache = nullptr;
}

Json Profile::to_json() const {
    Json j = Json::object();

    Json jc = Json::object();
    jc.set("vendor", to_string(cpu.vendor));
    jc.set("brand", cpu.brand);
    jc.set("codename", cpu.codename());
    jc.set("family", static_cast<int64_t>(cpu.family));
    jc.set("model", static_cast<int64_t>(cpu.model));
    jc.set("stepping", static_cast<int64_t>(cpu.stepping));
    jc.set("physical_cores", static_cast<int64_t>(cpu.physical_cores));
    jc.set("logical_cores", static_cast<int64_t>(cpu.logical_cores));
    jc.set("base_mhz", static_cast<int64_t>(cpu.base_mhz));
    jc.set("avx2", cpu.avx2);
    jc.set("avx512f", cpu.avx512f);
    jc.set("hybrid", cpu.hybrid);
    jc.set("x3d", cpu.has_x3d);
    j.set("cpu", std::move(jc));

    Json jg = Json::array();
    for (const auto& g : gpus) {
        Json e = Json::object();
        e.set("vendor", to_string(g.vendor));
        e.set("description", g.description);
        e.set("vendor_id", static_cast<int64_t>(g.vendor_id));
        e.set("device_id", static_cast<int64_t>(g.device_id));
        e.set("vram_mb", static_cast<int64_t>(g.dedicated_vram / (1024 * 1024)));
        e.set("driver", g.driver_version);
        e.set("integrated", g.is_integrated);
        jg.push(std::move(e));
    }
    j.set("gpus", std::move(jg));

    Json js = Json::object();
    js.set("manufacturer", system.manufacturer);
    js.set("product", system.product);
    js.set("baseboard", system.baseboard_vendor + " " + system.baseboard_product);
    js.set("bios_vendor", system.bios_vendor);
    js.set("bios_version", system.bios_version);
    js.set("bios_date", system.bios_date);
    js.set("chassis_type", static_cast<int64_t>(system.chassis_type));
    js.set("laptop", system.is_laptop);
    j.set("system", std::move(js));

    Json jm = Json::object();
    jm.set("total_mb", static_cast<int64_t>(memory.total_bytes / (1024 * 1024)));
    Json jmods = Json::array();
    for (const auto& m : memory.modules) {
        Json e = Json::object();
        e.set("locator", m.locator);
        e.set("size_mb", static_cast<int64_t>(m.size_bytes / (1024 * 1024)));
        e.set("speed_mts", static_cast<int64_t>(m.speed_mts));
        e.set("configured_mts", static_cast<int64_t>(m.configured_mts));
        e.set("manufacturer", m.manufacturer);
        e.set("part_number", m.part_number);
        jmods.push(std::move(e));
    }
    jm.set("modules", std::move(jmods));
    j.set("memory", std::move(jm));

    Json jo = Json::object();
    jo.set("product_name", os.product_name);
    jo.set("display_version", os.display_version);
    jo.set("build", static_cast<int64_t>(os.build));
    jo.set("ubr", static_cast<int64_t>(os.ubr));
    j.set("os", std::move(jo));

    Json jsec = Json::object();
    jsec.set("secure_boot", security.secure_boot);
    jsec.set("hvci", security.hvci);
    jsec.set("vbs", security.vbs);
    Json jac = Json::array();
    for (const auto& a : security.kernel_anticheats) jac.push(Json(a));
    jsec.set("kernel_anticheats", std::move(jac));
    j.set("security", std::move(jsec));

    Json jcap = Json::object();
    jcap.set("power_plans", caps.power_plans);
    jcap.set("core_parking", caps.core_parking);
    jcap.set("perf_boost_mode", caps.perf_boost_mode);
    jcap.set("usb_selective_suspend", caps.usb_selective_suspend);
    jcap.set("pcie_aspm", caps.pcie_aspm);
    jcap.set("hags", caps.hags);
    jcap.set("network_tweaks", caps.network_tweaks);
    jcap.set("timer_resolution", caps.timer_resolution);
    jcap.set("hwinfo_shared_memory", caps.hwinfo_shared_memory);
    jcap.set("nvml", caps.nvml);
    jcap.set("msr_read", caps.msr_read);
    jcap.set("msr_write", caps.msr_write);
    jcap.set("smu_mailbox", caps.smu_mailbox);
    Json jbr = Json::array();
    for (const auto& r : caps.blocked_reasons) jbr.push(Json(r));
    jcap.set("blocked_reasons", std::move(jbr));
    j.set("capabilities", std::move(jcap));

    return j;
}

std::string Profile::summary() const {
    std::string s;
    auto line = [&](const std::string& k, const std::string& v) {
        s += std::format("  {:<22}{}\n", k, v);
    };

    s += "CPU\n";
    line("Modele", cpu.brand.empty() ? "inconnu" : cpu.brand);
    line("Fabricant", std::format("{} (famille {:#x}, modele {:#x}, stepping {})",
                                  to_string(cpu.vendor), cpu.family, cpu.model, cpu.stepping));
    if (!cpu.codename().empty()) line("Architecture", cpu.codename());
    line("Coeurs", std::format("{} physiques / {} logiques{}", cpu.physical_cores,
                               cpu.logical_cores, cpu.hybrid ? " (hybride P/E)" : ""));
    if (cpu.base_mhz) line("Frequence de base", std::format("{} MHz", cpu.base_mhz));
    line("Jeux d'instructions", std::format("AVX2 {}{}", cpu.avx2 ? "oui" : "non",
                                            cpu.avx512f ? ", AVX-512 oui" : ""));
    if (cpu.has_x3d) {
        line("Note", "puce X3D : cache 3D empile, tres sensible a la survolt");
    }

    s += "\nGPU\n";
    if (gpus.empty()) {
        line("", "aucun adaptateur detecte");
    } else {
        for (const auto& g : gpus) {
            line(g.is_integrated ? "Integre" : "Dedie",
                 std::format("{} — {} Mo VRAM{}", g.description,
                             g.dedicated_vram / (1024 * 1024),
                             g.driver_version.empty() ? std::string()
                                                      : " — pilote " + g.driver_version));
        }
    }

    s += "\nSysteme\n";
    line("Machine", trim(system.manufacturer + " " + system.product));
    line("Carte mere", trim(system.baseboard_vendor + " " + system.baseboard_product));
    line("BIOS", std::format("{} {} ({})", system.bios_vendor, system.bios_version,
                             system.bios_date));
    line("Format", system.is_laptop ? "portable — brides de securite actives" : "bureau");
    line("Memoire", std::format("{} Mo", memory.total_bytes / (1024 * 1024)));
    for (const auto& m : memory.modules) {
        s += std::format("    {:<26} {} Mo @ {} MT/s{}{}\n", m.locator,
                         m.size_bytes / (1024 * 1024),
                         m.configured_mts ? m.configured_mts : m.speed_mts,
                         m.manufacturer.empty() ? std::string() : " — " + m.manufacturer,
                         m.part_number.empty() ? std::string() : " " + m.part_number);
    }
    line("Windows", std::format("{} {} (build {}.{})", os.product_name, os.display_version,
                                os.build, os.ubr));

    s += "\nSecurite\n";
    line("Secure Boot", security.secure_boot ? "actif" : "inactif");
    line("HVCI", security.hvci ? "actif" : "inactif");
    line("VBS", security.vbs ? "actif" : "inactif");
    if (!security.kernel_anticheats.empty()) {
        std::string list;
        for (const auto& a : security.kernel_anticheats) {
            if (!list.empty()) list += ", ";
            list += a;
        }
        line("Anticheat noyau", list);
    }

    s += "\nCapacites\n";
    auto cap = [&](const char* name, bool v) {
        line(name, v ? "disponible" : "indisponible");
    };
    cap("Plans d'alimentation", caps.power_plans);
    cap("Core parking", caps.core_parking);
    cap("Mode de boost", caps.perf_boost_mode);
    cap("Suspension USB", caps.usb_selective_suspend);
    cap("ASPM PCIe", caps.pcie_aspm);
    cap("HAGS", caps.hags);
    cap("Tweaks reseau", caps.network_tweaks);
    cap("Timer resolution", caps.timer_resolution);
    cap("Capteurs HWiNFO", caps.hwinfo_shared_memory);
    cap("Telemetrie NVML", caps.nvml);
    cap("Acces MSR (ring 0)", caps.msr_write);
    if (!caps.blocked_reasons.empty()) {
        s += "\n  Restrictions :\n";
        for (const auto& r : caps.blocked_reasons) s += "    - " + r + "\n";
    }
    return s;
}

} // namespace tf::hw
