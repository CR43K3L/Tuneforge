#pragma once
//
// Telemetrie. Trois sources, toutes optionnelles et degradees proprement :
//   1. Compteurs Windows  — toujours disponibles (charge CPU, memoire)
//   2. NVML               — GPU NVIDIA, chargement dynamique de nvml.dll
//   3. Memoire partagee HWiNFO — capteurs detailles (VRM, puissances, ventilos)
//      sans avoir a ecrire le moindre driver noyau.
//
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tf::hw {

// --- Compteurs systeme -----------------------------------------------------
struct SystemSnapshot {
    double   cpu_load_pct = 0.0;
    uint64_t ram_used_bytes = 0;
    uint64_t ram_total_bytes = 0;
    double   ram_pct = 0.0;
    uint32_t process_count = 0;
    uint64_t uptime_seconds = 0;
};

// Necessite deux appels espaces pour que la charge CPU soit significative.
class SystemCounters {
public:
    SystemSnapshot sample();

private:
    uint64_t prev_idle_ = 0, prev_kernel_ = 0, prev_user_ = 0;
    bool     primed_ = false;
};

// --- Frequence processeur --------------------------------------------------
// La frequence reelle (turbo compris) s'obtient en usermode via le compteur de
// performance « % Processor Performance », que Windows exprime en pourcentage
// de la frequence de base. C'est la methode qu'utilise le Gestionnaire des
// taches, et elle ne demande aucun driver.
class CpuFrequency {
public:
    ~CpuFrequency();
    CpuFrequency() = default;
    CpuFrequency(const CpuFrequency&) = delete;
    CpuFrequency& operator=(const CpuFrequency&) = delete;

    bool  init(uint32_t base_mhz);
    bool  available() const { return counter_ != nullptr; }
    // Frequence courante en MHz, ou -1 si la mesure n'est pas encore prete.
    float sample_mhz();
    uint32_t base_mhz() const { return base_mhz_; }

private:
    void*    query_ = nullptr;
    void*    counter_ = nullptr;
    uint32_t base_mhz_ = 0;
    bool     primed_ = false;
};

// --- NVML (NVIDIA) ---------------------------------------------------------
struct GpuTelemetry {
    std::string name;
    bool        valid = false;
    uint32_t    temperature_c = 0;
    uint32_t    power_mw = 0;
    uint32_t    clock_graphics_mhz = 0;
    uint32_t    clock_memory_mhz = 0;
    uint32_t    utilization_pct = 0;
    uint32_t    memory_util_pct = 0;
    uint32_t    fan_pct = 0;
    uint64_t    vram_used_bytes = 0;
    uint64_t    vram_total_bytes = 0;
};

class Nvml {
public:
    Nvml() = default;
    ~Nvml();
    Nvml(const Nvml&) = delete;
    Nvml& operator=(const Nvml&) = delete;

    bool                      available() const { return ready_; }
    bool                      init();
    uint32_t                  device_count() const { return count_; }
    std::optional<GpuTelemetry> sample(uint32_t index);

private:
    void*    module_ = nullptr;
    bool     ready_ = false;
    uint32_t count_ = 0;
};

// --- Memoire partagee HWiNFO ----------------------------------------------
enum class ReadingType { None, Temperature, Voltage, Fan, Current, Power, Clock, Usage, Other };

struct Reading {
    ReadingType type = ReadingType::None;
    std::string sensor;   // "CPU [#0]: AMD Ryzen 7 7800X3D"
    std::string label;    // "CPU (Tctl/Tdie)"
    std::string unit;     // "°C"
    double      value = 0.0;
    double      value_min = 0.0;
    double      value_max = 0.0;
    double      value_avg = 0.0;
};

class HwInfoSensors {
public:
    // true si la memoire partagee est ouverte et le contenu coherent.
    bool open();
    bool refresh();
    bool available() const { return view_ != nullptr; }
    void close();
    ~HwInfoSensors();

    const std::vector<Reading>& readings() const { return readings_; }

    // Recherche insensible a la casse sur des sous-chaines.
    const Reading* find(std::string_view sensor_contains,
                        std::string_view label_contains) const;
    const Reading* find_any(std::string_view label_contains,
                            ReadingType type = ReadingType::None) const;

private:
    void*                handle_ = nullptr;
    const unsigned char* view_ = nullptr;
    std::vector<Reading> readings_;
};

const char* to_string(ReadingType t);

} // namespace tf::hw
