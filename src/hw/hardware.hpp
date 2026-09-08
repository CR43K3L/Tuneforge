#pragma once
//
// Couche d'abstraction materielle.
//
// Regle fondamentale du projet : materiel inconnu = fonctionnalite desactivee.
// Rien n'est applique « au cas ou ». Les capacites sont derivees de la detection
// et chaque tweak declare ce dont il a besoin.
//
#include <cstdint>
#include <string>
#include <vector>

#include "common/json.hpp"

namespace tf::hw {

enum class CpuVendor { Unknown, Intel, Amd };
enum class GpuVendor { Unknown, Nvidia, Amd, Intel };

struct CpuInfo {
    CpuVendor   vendor = CpuVendor::Unknown;
    std::string vendor_id;      // "AuthenticAMD"
    std::string brand;          // "AMD Ryzen 7 7800X3D 8-Core Processor"
    uint32_t    family = 0;     // family etendue deja combinee
    uint32_t    model = 0;      // model etendu deja combine
    uint32_t    stepping = 0;
    uint32_t    physical_cores = 0;
    uint32_t    logical_cores = 0;
    uint32_t    base_mhz = 0;
    bool        hyperthreading = false;
    bool        hybrid = false; // Intel P/E-cores (CPUID.7.0:EDX[15])
    bool        avx2 = false;
    bool        avx512f = false;
    bool        has_x3d = false; // heuristique sur le nom commercial

    std::string codename() const; // "Zen 4", "Raptor Lake", ... ou ""
};

struct GpuInfo {
    GpuVendor   vendor = GpuVendor::Unknown;
    std::string description;
    uint32_t    vendor_id = 0;
    uint32_t    device_id = 0;
    uint64_t    dedicated_vram = 0;
    uint64_t    shared_memory = 0;
    std::string driver_version;
    bool        is_integrated = false;
};

struct SystemInfo {
    std::string manufacturer;
    std::string product;
    std::string baseboard_vendor;
    std::string baseboard_product;
    std::string bios_vendor;
    std::string bios_version;
    std::string bios_date;
    uint8_t     chassis_type = 0;
    bool        is_laptop = false;
    bool        has_battery = false;
};

struct MemoryModule {
    uint64_t    size_bytes = 0;
    uint32_t    speed_mts = 0;
    uint32_t    configured_mts = 0;
    std::string manufacturer;
    std::string part_number;
    std::string locator;
};

struct MemoryInfo {
    uint64_t                  total_bytes = 0;
    std::vector<MemoryModule> modules;
};

struct OsInfo {
    std::string product_name;
    std::string display_version;  // "24H2"
    uint32_t    build = 0;
    uint32_t    ubr = 0;
    bool        is_windows11 = false;
};

struct SecurityInfo {
    bool secure_boot = false;
    bool hvci = false;   // Integrite de la memoire
    bool vbs = false;    // Securite basee sur la virtualisation
    // Anticheats a driver noyau connus pour entrer en conflit avec les outils
    // de monitoring/OC. Purement informatif : on ne desactive jamais rien.
    std::vector<std::string> kernel_anticheats;
};

// Ce que la machine autorise reellement. Un champ a false = onglet grise.
struct Capabilities {
    bool power_plans = false;       // API powrprof accessible
    bool core_parking = false;      // reglage present dans le plan actif
    bool perf_boost_mode = false;
    bool usb_selective_suspend = false;
    bool pcie_aspm = false;
    bool hags = false;              // Planification GPU acceleree supportee
    bool network_tweaks = false;    // interfaces TCP/IP trouvees
    bool timer_resolution = false;
    bool hwinfo_shared_memory = false;
    bool nvml = false;              // nvml.dll chargeable (telemetrie NVIDIA)
    // v0.1 : aucun acces ring 0. Ces champs existent pour la suite.
    bool msr_read = false;
    bool msr_write = false;
    bool smu_mailbox = false;

    std::vector<std::string> blocked_reasons;  // pourquoi telle capacite est off
};

struct Profile {
    CpuInfo              cpu;
    std::vector<GpuInfo> gpus;
    SystemInfo           system;
    MemoryInfo           memory;
    OsInfo               os;
    SecurityInfo         security;
    Capabilities         caps;

    const GpuInfo* primary_gpu() const;
    Json           to_json() const;
    std::string    summary() const;   // rendu console lisible
};

// Detection complete (quelques dizaines de ms). Ne necessite pas l'elevation.
const Profile& detect();
void           invalidate_cache();

const char* to_string(CpuVendor v);
const char* to_string(GpuVendor v);

} // namespace tf::hw
