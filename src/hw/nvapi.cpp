#include "hw/nvapi.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/util.hpp"

namespace tf::hw {
namespace {

// ===========================================================================
// Types NVAPI
// ===========================================================================
using NvS32 = int32_t;
using NvU32 = uint32_t;
using NvAPI_Status = int32_t;
using NvPhysicalGpuHandle = void*;

constexpr NvAPI_Status NVAPI_OK = 0;
constexpr int kMaxPhysicalGpus = 64;
constexpr int kShortStringMax = 64;

// version = taille de la structure | (numero de version << 16)
template <typename T>
constexpr NvU32 make_version(NvU32 ver) {
    return static_cast<NvU32>(sizeof(T)) | (ver << 16);
}

// --- Horloges --------------------------------------------------------------
constexpr int kMaxPublicClocks = 32;
enum ClockDomain { CLOCK_GRAPHICS = 0, CLOCK_MEMORY = 4, CLOCK_PROCESSOR = 7, CLOCK_VIDEO = 8 };

struct NV_GPU_CLOCK_FREQUENCIES {
    NvU32 version;
    NvU32 clock_type_and_reserved;  // ClockType sur 2 bits, le reste reserve
    struct {
        NvU32 present_and_reserved;  // bit 0 : le domaine existe
        NvU32 frequency_khz;
    } domain[kMaxPublicClocks];
};

// --- Thermique -------------------------------------------------------------
constexpr int kMaxThermalSensors = 3;
enum ThermalTarget { THERMAL_NONE = 0, THERMAL_GPU = 1, THERMAL_MEMORY = 2,
                     THERMAL_POWER_SUPPLY = 4, THERMAL_BOARD = 8 };

struct NV_GPU_THERMAL_SETTINGS {
    NvU32 version;
    NvU32 count;
    struct {
        NvU32 controller;
        NvS32 default_min_temp;
        NvS32 default_max_temp;
        NvS32 current_temp;
        NvU32 target;
    } sensor[kMaxThermalSensors];
};

// --- Limite de puissance (non documentee) ----------------------------------
constexpr int kMaxPowerPolicies = 4;

struct NV_GPU_POWER_INFO {
    NvU32 version;
    NvU32 valid_and_count;  // bit 0 : valide, bits 1-31 : nombre d'entrees
    struct {
        NvU32 pstate;
        NvU32 unknown1[2];
        NvU32 min_permille;
        NvU32 unknown2[2];
        NvU32 default_permille;
        NvU32 unknown3[2];
        NvU32 max_permille;
        NvU32 unknown4;
    } entries[kMaxPowerPolicies];
};

struct NV_GPU_POWER_STATUS {
    NvU32 version;
    NvU32 count;
    struct {
        NvU32 unknown1;
        NvU32 unknown2;
        NvU32 power_permille;
        NvU32 unknown4;
    } entries[kMaxPowerPolicies];
};

// --- Ventilateurs (non documente) ------------------------------------------
constexpr int kMaxCoolersPerGpu = 3;

struct NV_GPU_COOLER_SETTINGS {
    NvU32 version;
    NvU32 count;
    struct {
        NvU32 type;
        NvU32 controller;
        NvU32 default_min_level;
        NvU32 default_max_level;
        NvU32 current_min_level;
        NvU32 current_max_level;
        NvU32 current_level;
        NvU32 default_policy;
        NvU32 current_policy;
        NvU32 target;
        NvU32 control_type;
        NvU32 active;
    } cooler[kMaxCoolersPerGpu];
};

// Les pilotes recents ont remplace GetCoolerSettings par cette interface.
constexpr int kMaxFanCoolerItems = 32;

struct NV_FAN_COOLERS_STATUS {
    NvU32 version;
    NvU32 count;
    NvU32 reserved[8];
    struct {
        NvU32 index;
        NvU32 reserved1[8];
        NvU32 rpm;
        NvU32 min_level;
        NvU32 max_level;
        NvU32 level;
        NvU32 reserved2[8];
    } items[kMaxFanCoolerItems];
};

// --- Etats de performance / decalages d'horloge (non documente) -------------
constexpr int kMaxPstates = 16;
constexpr int kMaxPstateClocks = 8;
constexpr int kMaxBaseVoltages = 4;

struct NV_PSTATE20_PARAM_DELTA {
    NvS32 value;
    struct { NvS32 min; NvS32 max; } value_range;
};

struct NV_PSTATE20_CLOCK_ENTRY {
    NvU32                   domain_id;
    NvU32                   type_id;   // 0 = single, 1 = range
    NvU32                   editable_and_reserved;
    NV_PSTATE20_PARAM_DELTA freq_delta_khz;
    union {
        struct { NvU32 freq_khz; } single;
        struct {
            NvU32 min_freq_khz;
            NvU32 max_freq_khz;
            NvU32 domain_id;
            NvU32 min_voltage_uv;
            NvU32 max_voltage_uv;
        } range;
    } data;
};

struct NV_PSTATE20_BASE_VOLTAGE_ENTRY {
    NvU32                   domain_id;
    NvU32                   editable_and_reserved;
    NvU32                   volt_uv;
    NV_PSTATE20_PARAM_DELTA volt_delta_uv;
};

struct NV_GPU_PERF_PSTATES20_INFO_V1 {
    NvU32 version;
    NvU32 editable_and_reserved;
    NvU32 num_pstates;
    NvU32 num_clocks;
    NvU32 num_base_voltages;
    struct {
        NvU32                          pstate_id;
        NvU32                          editable_and_reserved;
        NV_PSTATE20_CLOCK_ENTRY        clocks[kMaxPstateClocks];
        NV_PSTATE20_BASE_VOLTAGE_ENTRY base_voltages[kMaxBaseVoltages];
    } pstates[kMaxPstates];
};

// V2 et V3 ajoutent une section de survolt a la fin de V1.
struct NV_GPU_PERF_PSTATES20_INFO_V2 {
    NV_GPU_PERF_PSTATES20_INFO_V1 base;
    struct {
        NvU32                          num_voltages;
        NV_PSTATE20_BASE_VOLTAGE_ENTRY voltages[kMaxBaseVoltages];
    } ov;
};

// ===========================================================================
// Table des points d'entree
//
// Les identifiants proviennent de la retro-ingenierie publique de NVAPI. Ceux
// marques « documente » figurent dans le SDK officiel ; les autres non.
// ===========================================================================
struct EntryDef { const char* name; NvU32 id; };

constexpr EntryDef kEntries[] = {
    {"NvAPI_Initialize",                        0x0150E828},  // documente
    {"NvAPI_Unload",                            0xD22BDD7E},  // documente
    {"NvAPI_GetErrorMessage",                   0x6C2D048C},  // documente
    {"NvAPI_SYS_GetDriverAndBranchVersion",     0x2926AAAD},  // documente
    {"NvAPI_EnumPhysicalGPUs",                  0xE5AC921F},  // documente
    {"NvAPI_GPU_GetFullName",                   0xCEEE8E9F},  // documente
    {"NvAPI_GPU_GetAllClockFrequencies",        0xDCB616C3},  // documente
    {"NvAPI_GPU_GetThermalSettings",            0xE3640A56},  // documente
    {"NvAPI_GPU_GetTachReading",                0x5F608315},  // documente
    {"NvAPI_GPU_GetPstates20",                  0x6FF81213},  // documente
    {"NvAPI_GPU_SetPstates20",                  0x0F4DAE6B},  // NON documente, ECRITURE
    {"NvAPI_GPU_ClientPowerPoliciesGetInfo",    0x34206D86},  // NON documente
    {"NvAPI_GPU_ClientPowerPoliciesGetStatus",  0x70916171},  // NON documente
    {"NvAPI_GPU_ClientPowerPoliciesSetStatus",  0xAD95F5ED},  // NON documente, ECRITURE
    {"NvAPI_GPU_GetCoolerSettings",             0xDA141340},  // NON documente, ancien
    {"NvAPI_GPU_ClientFanCoolersGetStatus",     0x35AED5E8},  // NON documente, recent
    // --- Pistes pour l'undervolt et les courbes de ventilateur --------------
    // Resolues ici pour savoir ce que le pilote expose. Les fonctions
    // d'ecriture ne sont JAMAIS sondees : envoyer une structure de disposition
    // inconnue a une fonction qui ecrit reviendrait a lui passer des octets
    // arbitraires.
    {"NvAPI_GPU_GetVFPCurve",                   0x21537AD4},  // lecture courbe V/F
    {"NvAPI_GPU_ClientVoltRailsGetStatus",      0x465F9BCF},  // lecture tensions
    {"NvAPI_GPU_ClientFanCoolersGetInfo",       0xFB85B01E},  // lecture info ventilateurs
    {"NvAPI_GPU_ClientFanCoolersGetControl",    0x814B209F},  // lecture controle ventilateurs
    {"NvAPI_GPU_ClientFanCoolersSetControl",    0xA58971A5},  // ECRITURE, non sondee
    {"NvAPI_GPU_SetCoolerLevels",               0x891FA0AE},  // ECRITURE, non sondee
};

enum EntryIndex {
    E_Initialize = 0, E_Unload, E_GetErrorMessage, E_DriverVersion, E_EnumPhysicalGPUs,
    E_GetFullName, E_GetAllClockFrequencies, E_GetThermalSettings, E_GetTachReading,
    E_GetPstates20, E_SetPstates20, E_PowerPoliciesGetInfo, E_PowerPoliciesGetStatus,
    E_PowerPoliciesSetStatus, E_GetCoolerSettings, E_FanCoolersGetStatus,
    E_GetVFPCurve, E_VoltRailsGetStatus, E_FanCoolersGetInfo, E_FanCoolersGetControl,
    E_FanCoolersSetControl, E_SetCoolerLevels,
    E_COUNT
};

static_assert(sizeof(kEntries) / sizeof(kEntries[0]) == E_COUNT,
              "table des points d'entree et enumeration desynchronisees");

void* g_fn[E_COUNT] = {};

using PFN_QueryInterface = void*(__cdecl*)(NvU32);

template <typename Fn>
Fn fn_as(EntryIndex i) {
    return reinterpret_cast<Fn>(g_fn[i]);
}

} // namespace

// ===========================================================================
std::string nvapi_status_text(int status) {
    // Le pilote sait nommer ses propres codes : on le lui demande plutot que
    // de maintenir une table incomplete.
    if (g_fn[E_GetErrorMessage]) {
        char msg[kShortStringMax] = {};
        if (reinterpret_cast<NvAPI_Status(__cdecl*)(NvAPI_Status, char*)>(
                g_fn[E_GetErrorMessage])(status, msg) == NVAPI_OK &&
            msg[0]) {
            return std::format("{} ({})", msg, status);
        }
    }
    switch (status) {
        case 0:    return "OK";
        case -1:   return "erreur generique";
        case -2:   return "bibliotheque introuvable";
        case -3:   return "fonction non implementee";
        case -4:   return "API non initialisee";
        case -5:   return "argument invalide";
        case -6:   return "aucun GPU NVIDIA detecte";
        case -7:   return "fin d'enumeration";
        case -8:   return "handle invalide";
        case -9:   return "version de structure incompatible";
        case -10:  return "handle invalide (perime)";
        case -100: return "handle de GPU logique attendu";
        case -101: return "handle de GPU physique attendu";
        case -103: return "combinaison invalide";
        case -104: return "non pris en charge";
        case -111: return "privileges insuffisants";
        default:   return std::format("code {}", status);
    }
}

Nvapi::~Nvapi() {
    if (ready_ && g_fn[E_Unload]) {
        fn_as<NvAPI_Status(__cdecl*)()>(E_Unload)();
    }
    if (module_) ::FreeLibrary(static_cast<HMODULE>(module_));
}

// ---------------------------------------------------------------------------
bool Nvapi::init() {
    if (ready_) return true;

    HMODULE m = ::LoadLibraryW(L"nvapi64.dll");
    if (!m) m = ::LoadLibraryW(L"nvapi.dll");
    if (!m) {
        load_error_ = "nvapi64.dll introuvable : pilote NVIDIA absent";
        log_debug("NVAPI : {}", load_error_);
        return false;
    }
    module_ = m;

    auto query = reinterpret_cast<PFN_QueryInterface>(
        reinterpret_cast<void*>(::GetProcAddress(m, "nvapi_QueryInterface")));
    if (!query) {
        load_error_ = "nvapi_QueryInterface absent de la bibliotheque";
        ::FreeLibrary(m);
        module_ = nullptr;
        return false;
    }

    entries_.clear();
    entries_.reserve(E_COUNT);
    for (int i = 0; i < E_COUNT; ++i) {
        g_fn[i] = query(kEntries[i].id);
        entries_.push_back({kEntries[i].name, kEntries[i].id, g_fn[i] != nullptr});
    }

    if (!g_fn[E_Initialize]) {
        load_error_ = "NvAPI_Initialize non resolu";
        ::FreeLibrary(m);
        module_ = nullptr;
        return false;
    }

    const NvAPI_Status st = fn_as<NvAPI_Status(__cdecl*)()>(E_Initialize)();
    if (st != NVAPI_OK) {
        load_error_ = "NvAPI_Initialize a echoue : " + nvapi_status_text(st);
        ::FreeLibrary(m);
        module_ = nullptr;
        return false;
    }

    if (g_fn[E_DriverVersion]) {
        NvU32 version = 0;
        char  branch[kShortStringMax] = {};
        if (fn_as<NvAPI_Status(__cdecl*)(NvU32*, char*)>(E_DriverVersion)(&version, branch) ==
            NVAPI_OK) {
            driver_version_ = std::format("{}.{}", version / 100, version % 100);
        }
    }

    ready_ = true;
    int resolved = 0;
    for (const auto& e : entries_) resolved += e.resolved ? 1 : 0;
    log_info("NVAPI initialise : {}/{} points d'entree resolus, pilote {}", resolved,
             static_cast<int>(E_COUNT),
             driver_version_.empty() ? "inconnu" : driver_version_);
    return true;
}

// ---------------------------------------------------------------------------
namespace {

void read_clocks(NvPhysicalGpuHandle h, NvClocks& out) {
    if (!g_fn[E_GetAllClockFrequencies]) return;

    NV_GPU_CLOCK_FREQUENCIES f{};
    // Le type d'horloge occupe les deux bits de poids faible : 0 = courante.
    f.clock_type_and_reserved = 0;

    // Les pilotes recents attendent la version 3 ; on retombe sur 2 au besoin.
    for (NvU32 ver : {3u, 2u}) {
        f.version = make_version<NV_GPU_CLOCK_FREQUENCIES>(ver);
        f.clock_type_and_reserved = 0;
        const NvAPI_Status st =
            fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, NV_GPU_CLOCK_FREQUENCIES*)>(
                E_GetAllClockFrequencies)(h, &f);
        if (st != NVAPI_OK) continue;

        auto get = [&](int domain) -> NvU32 {
            return (f.domain[domain].present_and_reserved & 1) ? f.domain[domain].frequency_khz
                                                               : 0;
        };
        out.graphics_khz  = get(CLOCK_GRAPHICS);
        out.memory_khz    = get(CLOCK_MEMORY);
        out.processor_khz = get(CLOCK_PROCESSOR);
        out.video_khz     = get(CLOCK_VIDEO);
        out.valid = true;
        return;
    }
}

void read_thermal(NvPhysicalGpuHandle h, NvThermal& out) {
    if (!g_fn[E_GetThermalSettings]) return;

    NV_GPU_THERMAL_SETTINGS t{};
    t.version = make_version<NV_GPU_THERMAL_SETTINGS>(2);
    // 15 = tous les capteurs
    const NvAPI_Status st =
        fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, NvU32, NV_GPU_THERMAL_SETTINGS*)>(
            E_GetThermalSettings)(h, 15, &t);
    if (st != NVAPI_OK) return;

    out.sensor_count = static_cast<int>(t.count);
    for (NvU32 i = 0; i < t.count && i < kMaxThermalSensors; ++i) {
        switch (t.sensor[i].target) {
            case THERMAL_GPU:          out.gpu_c = t.sensor[i].current_temp; break;
            case THERMAL_MEMORY:       out.memory_c = t.sensor[i].current_temp; break;
            case THERMAL_POWER_SUPPLY: out.power_supply_c = t.sensor[i].current_temp; break;
            case THERMAL_BOARD:        out.board_c = t.sensor[i].current_temp; break;
            default: break;
        }
    }
    out.valid = true;
}

void read_power(NvPhysicalGpuHandle h, NvPowerLimit& out) {
    if (!g_fn[E_PowerPoliciesGetInfo] || !g_fn[E_PowerPoliciesGetStatus]) return;

    NV_GPU_POWER_INFO info{};
    info.version = make_version<NV_GPU_POWER_INFO>(1);
    if (fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, NV_GPU_POWER_INFO*)>(
            E_PowerPoliciesGetInfo)(h, &info) != NVAPI_OK) {
        return;
    }
    const NvU32 count = info.valid_and_count >> 1;
    if ((info.valid_and_count & 1) == 0 || count == 0) return;

    out.raw_min      = info.entries[0].min_permille;
    out.raw_max      = info.entries[0].max_permille;
    out.min_pct      = info.entries[0].min_permille / 1000.0f;
    out.default_pct  = info.entries[0].default_permille / 1000.0f;
    out.max_pct      = info.entries[0].max_permille / 1000.0f;

    NV_GPU_POWER_STATUS status{};
    status.version = make_version<NV_GPU_POWER_STATUS>(1);
    if (fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, NV_GPU_POWER_STATUS*)>(
            E_PowerPoliciesGetStatus)(h, &status) != NVAPI_OK) {
        return;
    }
    if (status.count == 0) return;

    out.raw_current  = status.entries[0].power_permille;
    out.current_pct  = status.entries[0].power_permille / 1000.0f;
    out.editable = out.raw_max > out.raw_min;
    out.valid = true;
}

void read_coolers(NvPhysicalGpuHandle h, NvCoolers& out) {
    // Interface recente d'abord : sur les pilotes actuels, GetCoolerSettings
    // est resolu mais repond « non pris en charge ».
    if (g_fn[E_FanCoolersGetStatus]) {
        // La disposition exacte de cette structure n'est pas publiee et le
        // nombre d'entrees varie selon les sources. Le pilote valide la taille
        // encodee dans le champ version : on essaie donc les tailles plausibles
        // jusqu'a ce qu'il en accepte une. L'appel est en lecture seule, le
        // sondage est sans consequence.
        // Disposition etablie par sondage PUIS lecture du contenu reel sur
        // pilote 616.64 : 1704 octets en version 1, soit 40 + 32 x 52.
        //   en-tete : version(4) count(4) reserved[8](32)           = 40
        //   entree  : index(+0) rpm(+4) min(+8) max(+12) level(+16) = 52
        // Les valeurs 30 et 100 lues aux offsets +8 et +12 de chaque entree
        // confirment la lecture : ce sont les bornes de la carte.
        constexpr NvU32 kHeader = 40;
        constexpr NvU32 kItem   = 52;
        constexpr NvU32 kItems  = 32;
        constexpr NvU32 kSize   = kHeader + kItem * kItems;
        static_assert(kSize == 1704, "taille de structure ventilateur inattendue");

        auto call = fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, void*)>(
            E_FanCoolersGetStatus);

        std::vector<uint8_t> buf(kSize, 0);
        NvU32 version = kSize | (1u << 16);
        std::memcpy(buf.data(), &version, sizeof(version));

        const NvAPI_Status st = call(h, buf.data());
        if (st == NVAPI_OK) {
            NvU32 count = 0;
            std::memcpy(&count, buf.data() + 4, sizeof(count));
            for (NvU32 i = 0; i < count && i < kItems; ++i) {
                const uint8_t* e = buf.data() + kHeader + kItem * i;
                NvU32 index = 0, rpm = 0, mn = 0, mx = 0, lvl = 0;
                std::memcpy(&index, e + 0,  4);
                std::memcpy(&rpm,   e + 4,  4);
                std::memcpy(&mn,    e + 8,  4);
                std::memcpy(&mx,    e + 12, 4);
                std::memcpy(&lvl,   e + 16, 4);

                NvCooler item;
                item.index         = index;
                item.current_level = lvl;
                item.min_level     = mn;
                item.max_level     = mx;
                item.range_known   = mx > mn;
                item.policy_known  = false;  // absente de cette interface
                item.active        = rpm > 0;
                out.items.push_back(item);
                if (rpm > out.tach_rpm) {
                    out.tach_rpm = rpm;
                    out.tach_valid = true;
                }
            }
            out.valid = !out.items.empty();
            if (out.valid) return;
        } else {
            log_debug("NvAPI_GPU_ClientFanCoolersGetStatus : {}", nvapi_status_text(st));
        }
    }

    if (g_fn[E_GetCoolerSettings]) {
        NV_GPU_COOLER_SETTINGS c{};
        c.version = make_version<NV_GPU_COOLER_SETTINGS>(2);
        // 7 = tous les ventilateurs
        if (fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, NvU32, NV_GPU_COOLER_SETTINGS*)>(
                E_GetCoolerSettings)(h, 7, &c) == NVAPI_OK) {
            for (NvU32 i = 0; i < c.count && i < kMaxCoolersPerGpu; ++i) {
                NvCooler item;
                item.index          = i;
                item.current_level  = c.cooler[i].current_level;
                item.min_level      = c.cooler[i].current_min_level;
                item.max_level      = c.cooler[i].current_max_level;
                item.current_policy = c.cooler[i].current_policy;
                item.policy_known   = true;
                item.range_known    = c.cooler[i].current_max_level >
                                      c.cooler[i].current_min_level;
                item.active         = c.cooler[i].active != 0;
                out.items.push_back(item);
            }
            out.valid = !out.items.empty();
        }
    }

    if (g_fn[E_GetTachReading]) {
        NvU32 rpm = 0;
        const NvAPI_Status st =
            fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, NvU32*)>(E_GetTachReading)(h, &rpm);
        if (st == NVAPI_OK) {
            out.tach_rpm = rpm;
            out.tach_valid = true;
        } else {
            log_debug("NvAPI_GPU_GetTachReading : {}", nvapi_status_text(st));
        }
    }
}

// Disposition etablie par sondage puis lecture du contenu : le pilote accepte
// 76 octets en version 1, et la tension en microvolts se trouve au mot 10.
// Verifiee vivante : la valeur suit la charge du GPU (0,925 V au repos,
// 0,965 V en charge).
void read_voltage(NvPhysicalGpuHandle h, NvVoltage& out) {
    if (!g_fn[E_VoltRailsGetStatus]) return;

    constexpr NvU32 kSize = 76;
    constexpr NvU32 kVoltageWord = 10;

    std::vector<uint8_t> buf(kSize, 0);
    NvU32 version = kSize | (1u << 16);
    std::memcpy(buf.data(), &version, sizeof(version));

    const NvAPI_Status st =
        fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, void*)>(E_VoltRailsGetStatus)(
            h, buf.data());
    if (st != NVAPI_OK) {
        log_debug("NvAPI_GPU_ClientVoltRailsGetStatus : {}", nvapi_status_text(st));
        return;
    }
    NvU32 uv = 0;
    std::memcpy(&uv, buf.data() + kVoltageWord * 4, sizeof(uv));
    if (uv == 0) return;  // une tension nulle n'a pas de sens : on ne l'affiche pas
    out.microvolts = uv;
    out.valid = true;
}

void read_offsets(NvPhysicalGpuHandle h, NvClockOffsets& out) {
    if (!g_fn[E_GetPstates20]) return;

    NV_GPU_PERF_PSTATES20_INFO_V2 info{};
    auto call = fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle,
                                             NV_GPU_PERF_PSTATES20_INFO_V2*)>(E_GetPstates20);

    // On tente les versions decroissantes : la structure a grandi avec le temps
    // et le pilote refuse toute version qu'il ne connait pas.
    NvAPI_Status st = -9;
    NvU32        accepted = 0;
    for (NvU32 ver : {3u, 2u}) {
        std::memset(&info, 0, sizeof(info));
        accepted = make_version<NV_GPU_PERF_PSTATES20_INFO_V2>(ver);
        info.base.version = accepted;
        st = call(h, &info);
        if (st == NVAPI_OK) break;
    }
    if (st != NVAPI_OK) {
        std::memset(&info, 0, sizeof(info));
        accepted = make_version<NV_GPU_PERF_PSTATES20_INFO_V1>(1);
        info.base.version = accepted;
        st = call(h, &info);
    }
    if (st == NVAPI_OK) out.accepted_version = accepted;
    if (st != NVAPI_OK) {
        log_debug("NvAPI_GPU_GetPstates20 : {}", nvapi_status_text(st));
        return;
    }

    out.pstate_count = info.base.num_pstates;
    out.editable = (info.base.editable_and_reserved & 1) != 0;

    // Seul le P-state le plus performant (index 0) porte les decalages utiles.
    if (info.base.num_pstates == 0) return;
    const auto& p0 = info.base.pstates[0];
    for (NvU32 c = 0; c < info.base.num_clocks && c < kMaxPstateClocks; ++c) {
        const auto& clk = p0.clocks[c];
        if (clk.domain_id == CLOCK_GRAPHICS) {
            out.graphics_delta_khz = clk.freq_delta_khz.value;
            out.graphics_min_khz   = clk.freq_delta_khz.value_range.min;
            out.graphics_max_khz   = clk.freq_delta_khz.value_range.max;
        } else if (clk.domain_id == CLOCK_MEMORY) {
            out.memory_delta_khz = clk.freq_delta_khz.value;
            out.memory_min_khz   = clk.freq_delta_khz.value_range.min;
            out.memory_max_khz   = clk.freq_delta_khz.value_range.max;
        }
    }
    out.valid = true;
}

} // namespace

// ---------------------------------------------------------------------------
bool Nvapi::can_set_power_limit() const {
    return ready_ && g_fn[E_PowerPoliciesSetStatus] != nullptr;
}

Result Nvapi::set_power_limit_pct(size_t gpu_index, float pct) {
    if (!ready_) return Result::fail("NVAPI non initialise");
    if (!g_fn[E_PowerPoliciesSetStatus]) {
        return Result::fail("NvAPI_GPU_ClientPowerPoliciesSetStatus non resolu");
    }
    if (gpu_index >= gpus_.size()) return Result::fail("indice de GPU invalide");

    const NvGpu& g = gpus_[gpu_index];
    if (!g.power.valid) {
        return Result::fail("limite de puissance non lisible sur ce GPU : ecriture refusee");
    }

    // On n'ecrit jamais hors des bornes annoncees par le pilote.
    if (pct < g.power.min_pct - 0.001f || pct > g.power.max_pct + 0.001f) {
        return Result::fail(std::format("{:.0f} % hors des bornes autorisees ({:.0f} a {:.0f} %)",
                                        pct, g.power.min_pct, g.power.max_pct));
    }

    NV_GPU_POWER_STATUS status{};
    status.version = make_version<NV_GPU_POWER_STATUS>(1);
    status.count = 1;
    status.entries[0].power_permille = static_cast<NvU32>(pct * 1000.0f + 0.5f);

    const NvAPI_Status st =
        fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, NV_GPU_POWER_STATUS*)>(
            E_PowerPoliciesSetStatus)(g.handle, &status);
    if (st != NVAPI_OK) {
        std::string hint;
        if (st == -137) hint = " — relancez Tuneforge en tant qu'administrateur";
        return Result::fail("ecriture refusee par le pilote : " + nvapi_status_text(st) + hint);
    }

    // Verification : on relit et on compare. Un pilote peut accepter l'appel
    // sans appliquer la valeur.
    refresh();
    if (gpu_index >= gpus_.size() || !gpus_[gpu_index].power.valid) {
        return Result::fail("valeur ecrite mais relecture impossible");
    }
    const float now = gpus_[gpu_index].power.current_pct;
    if (std::fabs(now - pct) > 1.0f) {
        return Result::fail(std::format("valeur ecrite ({:.0f} %) mais le pilote rapporte "
                                        "{:.0f} %",
                                        pct, now));
    }
    log_info("limite de puissance GPU {} reglee a {:.0f} %", gpu_index, now);
    return Result::success();
}

// ---------------------------------------------------------------------------
bool Nvapi::can_set_clock_offset() const {
    return ready_ && g_fn[E_SetPstates20] != nullptr;
}

Result Nvapi::set_clock_offset_mhz(size_t gpu_index, NvClockDomain domain, int32_t mhz) {
    if (!ready_) return Result::fail("NVAPI non initialise");
    if (!g_fn[E_SetPstates20]) return Result::fail("NvAPI_GPU_SetPstates20 non resolu");
    if (gpu_index >= gpus_.size()) return Result::fail("indice de GPU invalide");

    const NvGpu& g = gpus_[gpu_index];
    if (!g.offsets.valid || g.offsets.accepted_version == 0) {
        return Result::fail("decalages non lisibles sur ce GPU : ecriture refusee");
    }
    if (!g.offsets.editable) {
        return Result::fail("le pilote declare les P-states non modifiables");
    }

    const bool is_mem = domain == NvClockDomain::Memory;
    const int32_t min_mhz = (is_mem ? g.offsets.memory_min_khz : g.offsets.graphics_min_khz) / 1000;
    const int32_t max_mhz = (is_mem ? g.offsets.memory_max_khz : g.offsets.graphics_max_khz) / 1000;

    // On n'ecrit jamais hors des bornes que le pilote annonce lui-meme.
    if (mhz < min_mhz || mhz > max_mhz) {
        return Result::fail(std::format("{:+} MHz hors des bornes autorisees ({} a {} MHz)", mhz,
                                        min_mhz, max_mhz));
    }

    // Structure minimale : un seul P-state, une seule horloge. On ne remplace
    // pas la courbe, on lui applique un delta — le pilote continue de gerer
    // les tensions correspondantes.
    NV_GPU_PERF_PSTATES20_INFO_V2 info{};
    info.base.version           = g.offsets.accepted_version;
    info.base.num_pstates       = 1;
    info.base.num_clocks        = 1;
    info.base.num_base_voltages = 0;
    info.base.pstates[0].pstate_id = 0;  // P0 : l'etat le plus performant
    info.base.pstates[0].clocks[0].domain_id = is_mem ? CLOCK_MEMORY : CLOCK_GRAPHICS;
    info.base.pstates[0].clocks[0].freq_delta_khz.value = mhz * 1000;

    const NvAPI_Status st =
        fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, NV_GPU_PERF_PSTATES20_INFO_V2*)>(
            E_SetPstates20)(g.handle, &info);
    if (st != NVAPI_OK) {
        std::string hint;
        if (st == -137) hint = " — relancez Tuneforge en tant qu'administrateur";
        return Result::fail("ecriture refusee par le pilote : " + nvapi_status_text(st) + hint);
    }

    // Relecture : le pilote peut accepter l'appel sans appliquer le delta.
    refresh();
    if (gpu_index >= gpus_.size() || !gpus_[gpu_index].offsets.valid) {
        return Result::fail("valeur ecrite mais relecture impossible");
    }
    const int32_t now = (is_mem ? gpus_[gpu_index].offsets.memory_delta_khz
                                : gpus_[gpu_index].offsets.graphics_delta_khz) / 1000;
    if (now != mhz) {
        return Result::fail(std::format("delta ecrit ({:+} MHz) mais le pilote rapporte {:+} MHz",
                                        mhz, now));
    }
    log_info("decalage {} du GPU {} regle a {:+} MHz", is_mem ? "memoire" : "graphique",
             gpu_index, now);
    return Result::success();
}

// ---------------------------------------------------------------------------
// Liste blanche : uniquement des fonctions de lecture.
std::vector<std::string> Nvapi::probeable_functions() {
    return {"NvAPI_GPU_ClientFanCoolersGetStatus", "NvAPI_GPU_ClientFanCoolersGetInfo",
            "NvAPI_GPU_ClientFanCoolersGetControl", "NvAPI_GPU_GetVFPCurve",
            "NvAPI_GPU_ClientVoltRailsGetStatus", "NvAPI_GPU_GetCoolerSettings"};
}

std::vector<Nvapi::ProbeHit> Nvapi::probe_layout(const std::string& function_name) {
    std::vector<ProbeHit> hits;
    if (!ready_) return hits;

    const auto allowed = probeable_functions();
    if (std::find(allowed.begin(), allowed.end(), function_name) == allowed.end()) {
        log_warn("sondage refuse pour « {} » : hors de la liste blanche de lecture",
                 function_name);
        return hits;
    }

    int index = -1;
    for (int i = 0; i < E_COUNT; ++i) {
        if (function_name == kEntries[i].name) { index = i; break; }
    }
    if (index < 0 || !g_fn[index]) return hits;

    NvPhysicalGpuHandle handles[kMaxPhysicalGpus] = {};
    NvU32               count = 0;
    if (fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle*, NvU32*)>(E_EnumPhysicalGPUs)(
            handles, &count) != NVAPI_OK ||
        count == 0) {
        return hits;
    }

    // Certaines de ces fonctions prennent un parametre supplementaire avant la
    // structure. On essaie les deux formes.
    auto call1 = reinterpret_cast<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, void*)>(
        g_fn[index]);
    auto call2 = reinterpret_cast<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, NvU32, void*)>(
        g_fn[index]);

    constexpr uint32_t kMaxDeclared = 32768;
    std::vector<uint8_t> buf(static_cast<size_t>(kMaxDeclared) * 4, 0);

    for (int form = 0; form < 2; ++form) {
        for (uint32_t ver = 1; ver <= 4; ++ver) {
            for (uint32_t size = 8; size <= kMaxDeclared; size += 4) {
                std::memset(buf.data(), 0, buf.size());
                const uint32_t version = size | (ver << 16);
                std::memcpy(buf.data(), &version, sizeof(version));

                const NvAPI_Status st = (form == 0) ? call1(handles[0], buf.data())
                                                    : call2(handles[0], 0, buf.data());
                if (st != NVAPI_OK) continue;

                uint32_t reported = 0;
                std::memcpy(&reported, buf.data() + 4, sizeof(reported));
                hits.push_back({size, ver, reported});
                log_info("{} : taille {} acceptee en version {} (forme {}, champ suivant = {})",
                         function_name, size, ver, form == 0 ? "handle+struct" : "handle+arg+struct",
                         reported);
                break;
            }
        }
        if (!hits.empty()) break;
    }
    return hits;
}

// ---------------------------------------------------------------------------
// Ventilateurs
//
// Disposition etablie par sondage puis lecture : 1452 octets en version 1,
// soit un en-tete de 44 octets (version, reserve, count, reserve[8]) suivi de
// 32 entrees de 44 octets dont le premier champ est l'index du ventilateur.
// ---------------------------------------------------------------------------
namespace {

constexpr NvU32 kFanCtrlSize   = 1452;
constexpr NvU32 kFanCtrlHeader = 44;
constexpr NvU32 kFanCtrlItem   = 44;
constexpr NvU32 kFanCtrlItems  = 32;
constexpr NvU32 kFanCountWord  = 2;   // offset 8
constexpr NvU32 kFanItemLevel  = 4;   // offset relatif dans l'entree
constexpr NvU32 kFanItemMode   = 8;
static_assert(kFanCtrlHeader + kFanCtrlItem * kFanCtrlItems == kFanCtrlSize,
              "disposition de controle ventilateur incoherente");

// Recupere la structure telle que le pilote la produit.
bool fan_control_read(NvPhysicalGpuHandle h, std::vector<uint8_t>& buf, NvAPI_Status& st) {
    buf.assign(kFanCtrlSize, 0);
    const NvU32 version = kFanCtrlSize | (1u << 16);
    std::memcpy(buf.data(), &version, sizeof(version));
    st = fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, void*)>(E_FanCoolersGetControl)(
        h, buf.data());
    return st == NVAPI_OK;
}

} // namespace

bool Nvapi::can_set_fan() const {
    return ready_ && g_fn[E_FanCoolersGetControl] && g_fn[E_FanCoolersSetControl];
}

Result Nvapi::set_fan_auto(size_t gpu_index) { return set_fan_level_pct(gpu_index, 0); }

Result Nvapi::set_fan_level_pct(size_t gpu_index, uint32_t level_pct) {
    if (!ready_) return Result::fail("NVAPI non initialise");
    if (!can_set_fan()) return Result::fail("controle des ventilateurs non resolu");
    if (gpu_index >= gpus_.size()) return Result::fail("indice de GPU invalide");

    const bool automatic = level_pct == 0;
    if (!automatic && (level_pct < kMinManualFanPct || level_pct > 100)) {
        return Result::fail(std::format("niveau {} % refuse : le mode manuel est limite a "
                                        "{} - 100 % pour ne jamais arreter les ventilateurs",
                                        level_pct, kMinManualFanPct));
    }

    NvPhysicalGpuHandle h = gpus_[gpu_index].handle;
    std::vector<uint8_t> buf;
    NvAPI_Status         st = 0;
    if (!fan_control_read(h, buf, st)) {
        return Result::fail("lecture du controle impossible : " + nvapi_status_text(st));
    }

    NvU32 count = 0;
    std::memcpy(&count, buf.data() + kFanCountWord * 4, sizeof(count));
    if (count == 0 || count > kFanCtrlItems) {
        return Result::fail(std::format("nombre de ventilateurs incoherent ({})", count));
    }

    // La position exacte du niveau et du mode dans l'entree n'a pas pu etre
    // deduite des donnees : les deux champs valaient zero au repos. On essaie
    // donc les dispositions plausibles et on VERIFIE chacune par GetStatus,
    // qui est une fonction distincte de celle qu'on vient d'ecrire. Si aucune
    // ne produit l'effet demande, on rend la main au pilote.
    struct Mapping { NvU32 level_off; NvU32 mode_off; const char* label; };
    static constexpr Mapping kMappings[] = {
        {4, 8, "niveau@+4 mode@+8"},
        {8, 4, "niveau@+8 mode@+4"},
        {4, 12, "niveau@+4 mode@+12"},
    };

    // Le mode voulu est un PARAMETRE explicite : le lire depuis la variable
    // englobante avait pour effet que le chemin de secours reecrivait le mode
    // manuel au lieu de rendre la main au pilote.
    auto write_with = [&](const Mapping& m, bool want_auto, NvU32 want_level) -> NvAPI_Status {
        std::vector<uint8_t> local;
        NvAPI_Status         rs = 0;
        if (!fan_control_read(h, local, rs)) return rs;
        for (NvU32 i = 0; i < count; ++i) {
            uint8_t* item = local.data() + kFanCtrlHeader + kFanCtrlItem * i;
            const NvU32 level = want_auto ? 0u : want_level;
            const NvU32 mode  = want_auto ? 0u : 1u;
            std::memcpy(item + m.level_off, &level, sizeof(level));
            std::memcpy(item + m.mode_off, &mode, sizeof(mode));
        }
        return fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, void*)>(
            E_FanCoolersSetControl)(h, local.data());
    };

    // Remet le pilote aux commandes sur toutes les dispositions essayees :
    // on ne sait pas laquelle a pris effet, on les neutralise donc toutes.
    auto restore_auto = [&]() {
        for (const auto& m : kMappings) write_with(m, true, 0);
    };

    if (automatic) {
        restore_auto();
        ::Sleep(600);
        refresh();  // sinon l'appelant reaffiche l'ancien niveau
        log_info("ventilateurs du GPU {} rendus au mode automatique", gpu_index);
        return Result::success();
    }

    std::string tried;
    for (const auto& m : kMappings) {
        st = write_with(m, false, level_pct);
        if (st != NVAPI_OK) {
            std::string hint;
            if (st == -137) hint = " — relancez Tuneforge en tant qu'administrateur";
            return Result::fail("ecriture refusee : " + nvapi_status_text(st) + hint);
        }

        ::Sleep(900);  // laisse le temps a la regulation de reagir
        refresh();
        if (gpu_index >= gpus_.size() || !gpus_[gpu_index].coolers.valid) {
            return Result::fail("ecrit, mais relecture de l'etat impossible");
        }

        bool ok = !gpus_[gpu_index].coolers.items.empty();
        for (const auto& c : gpus_[gpu_index].coolers.items) {
            if (c.current_level != level_pct) ok = false;
        }
        if (ok) {
            log_info("ventilateurs du GPU {} regles a {} % ({})", gpu_index, level_pct, m.label);
            return Result::success();
        }
        if (!tried.empty()) tried += ", ";
        tried += m.label;
    }

    // Aucune disposition n'a produit l'effet demande : on rend la main.
    restore_auto();
    return Result::fail(std::format(
        "aucune disposition testee n'a produit l'effet demande ({}). Le pilote accepte "
        "l'ecriture mais n'applique rien : les ventilateurs sont rendus au mode automatique",
        tried));
}

// ---------------------------------------------------------------------------
std::vector<uint8_t> Nvapi::dump_layout(const std::string& function_name) {
    std::vector<uint8_t> out;
    const auto hits = probe_layout(function_name);
    if (hits.empty()) return out;

    int index = -1;
    for (int i = 0; i < E_COUNT; ++i) {
        if (function_name == kEntries[i].name) { index = i; break; }
    }
    if (index < 0 || !g_fn[index]) return out;

    NvPhysicalGpuHandle handles[kMaxPhysicalGpus] = {};
    NvU32               count = 0;
    if (fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle*, NvU32*)>(E_EnumPhysicalGPUs)(
            handles, &count) != NVAPI_OK ||
        count == 0) {
        return out;
    }

    const auto& h0 = hits.front();
    out.assign(h0.size, 0);
    const uint32_t version = h0.size | (h0.version << 16);
    std::memcpy(out.data(), &version, sizeof(version));

    auto call1 = reinterpret_cast<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, void*)>(
        g_fn[index]);
    auto call2 = reinterpret_cast<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, NvU32, void*)>(
        g_fn[index]);

    if (call1(handles[0], out.data()) != NVAPI_OK) {
        std::memset(out.data(), 0, out.size());
        std::memcpy(out.data(), &version, sizeof(version));
        if (call2(handles[0], 0, out.data()) != NVAPI_OK) out.clear();
    }
    return out;
}

// ---------------------------------------------------------------------------
std::vector<Nvapi::ProbeHit> Nvapi::probe_fan_status_layout() {
    std::vector<ProbeHit> hits;
    if (!ready_ || !g_fn[E_FanCoolersGetStatus] || !g_fn[E_EnumPhysicalGPUs]) return hits;

    NvPhysicalGpuHandle handles[kMaxPhysicalGpus] = {};
    NvU32               count = 0;
    if (fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle*, NvU32*)>(E_EnumPhysicalGPUs)(
            handles, &count) != NVAPI_OK ||
        count == 0) {
        return hits;
    }

    auto call = fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, void*)>(E_FanCoolersGetStatus);

    // Tampon tres large : on ne declare jamais une taille superieure a la
    // moitie, le pilote ne peut donc pas ecrire hors limites.
    constexpr uint32_t kMaxDeclared = 8192;
    std::vector<uint8_t> buf(kMaxDeclared * 4, 0);

    for (uint32_t ver = 1; ver <= 4 && hits.size() < 8; ++ver) {
        for (uint32_t size = 8; size <= kMaxDeclared; size += 4) {
            std::memset(buf.data(), 0, buf.size());
            const uint32_t version = size | (ver << 16);
            std::memcpy(buf.data(), &version, sizeof(version));

            if (call(handles[0], buf.data()) != NVAPI_OK) continue;

            uint32_t reported = 0;
            std::memcpy(&reported, buf.data() + 4, sizeof(reported));
            hits.push_back({size, ver, reported});
            log_info("ventilateurs : taille {} acceptee en version {} (count = {})", size, ver,
                     reported);
            break;  // une taille par version suffit
        }
    }
    return hits;
}

// ---------------------------------------------------------------------------
bool Nvapi::refresh() {
    if (!ready_) return false;
    if (!g_fn[E_EnumPhysicalGPUs]) return false;

    NvPhysicalGpuHandle handles[kMaxPhysicalGpus] = {};
    NvU32               count = 0;
    const NvAPI_Status  st =
        fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle*, NvU32*)>(E_EnumPhysicalGPUs)(handles,
                                                                                       &count);
    if (st != NVAPI_OK) {
        log_warn("NvAPI_EnumPhysicalGPUs : {}", nvapi_status_text(st));
        return false;
    }

    gpus_.clear();
    gpus_.reserve(count);
    for (NvU32 i = 0; i < count; ++i) {
        NvGpu g;
        g.handle = handles[i];

        if (g_fn[E_GetFullName]) {
            char name[kShortStringMax] = {};
            if (fn_as<NvAPI_Status(__cdecl*)(NvPhysicalGpuHandle, char*)>(E_GetFullName)(
                    handles[i], name) == NVAPI_OK) {
                g.name = name;
            }
        }
        read_clocks(handles[i], g.clocks);
        read_voltage(handles[i], g.voltage);
        read_thermal(handles[i], g.thermal);
        read_power(handles[i], g.power);
        read_coolers(handles[i], g.coolers);
        read_offsets(handles[i], g.offsets);

        gpus_.push_back(std::move(g));
    }
    return true;
}

} // namespace tf::hw
