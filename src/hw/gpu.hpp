#pragma once
//
// Vocabulaire GPU independant du fabricant — v0.5.
//
// Jusqu'ici l'interface parlait NVAPI directement. Ajouter AMD dans ces
// conditions aurait duplique chaque page et chaque commande. Ce fichier
// definit ce dont l'application a besoin ; les liaisons (NVAPI, ADLX plus
// tard) s'y conforment.
//
// Le principe de conception qui gouverne tout le reste : **rien n'est
// suppose**. Un controleur n'annonce une capacite qu'apres avoir reellement
// resolu ses points d'entree et obtenu une reponse du pilote. Une capacite
// absente desactive la commande correspondante, elle ne la fait pas echouer
// au dernier moment.
//
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/json.hpp"
#include "common/util.hpp"
// GpuVendor et to_string(GpuVendor) y sont deja definis par la detection PCI :
// en redeclarer une copie ici ferait exister deux notions de « fabricant » qui
// finiraient par diverger.
#include "hw/hardware.hpp"

namespace tf::hw {

// ---------------------------------------------------------------------------
// Mesures
// ---------------------------------------------------------------------------
struct GpuClocks {
    bool     valid = false;
    uint32_t graphics_khz = 0;
    uint32_t memory_khz = 0;
    uint32_t processor_khz = 0;
    uint32_t video_khz = 0;
};

struct GpuThermal {
    bool    valid = false;
    int32_t gpu_c = 0;      // capteur GPU
    int32_t memory_c = 0;   // -1 si absent
    int32_t power_supply_c = 0;
    int32_t board_c = 0;
    int     sensor_count = 0;
};

// Limite de puissance, exprimee en pourcentage de la valeur par defaut. Les
// champs `raw_*` gardent l'unite du pilote (NVAPI compte en millipourcents :
// 100000 = 100 %), parce que l'ecriture doit lui rendre exactement ce qu'il a
// donne.
struct GpuPowerLimit {
    bool     valid = false;
    float    current_pct = 0.0f;
    float    min_pct = 0.0f;
    float    default_pct = 0.0f;
    float    max_pct = 0.0f;
    uint32_t raw_current = 0;
    uint32_t raw_min = 0;
    uint32_t raw_max = 0;
    bool     editable = false;
};

struct GpuCooler {
    uint32_t index = 0;
    uint32_t current_level = 0;   // %
    uint32_t min_level = 0;
    uint32_t max_level = 0;
    uint32_t current_policy = 0;      // 1 = manuel, 32 = automatique (NVAPI)
    bool     policy_known = false;    // l'API recente ne la rapporte pas
    bool     range_known = false;     // idem pour les bornes de niveau
    bool     active = false;
};

struct GpuCoolers {
    bool                   valid = false;
    std::vector<GpuCooler> items;
    uint32_t               tach_rpm = 0;
    bool                   tach_valid = false;
};

// Decalages appliques par rapport a la courbe tension/frequence d'origine, en
// kHz. La courbe n'est jamais remplacee : on lui ajoute un delta, ce qui laisse
// le pilote gerer les tensions correspondantes.
struct GpuClockOffsets {
    bool    valid = false;
    bool    editable = false;
    int32_t graphics_delta_khz = 0;
    int32_t memory_delta_khz = 0;
    int32_t graphics_min_khz = 0;
    int32_t graphics_max_khz = 0;
    int32_t memory_min_khz = 0;
    int32_t memory_max_khz = 0;
    uint32_t pstate_count = 0;
    // Valeur du champ version que le pilote a acceptee en lecture : l'ecriture
    // doit employer exactement la meme, sinon elle est rejetee.
    uint32_t accepted_version = 0;
};

// Tension du rail principal. C'est la mesure de reference pour juger d'un
// undervolt : la frequence seule ne dit rien.
struct GpuVoltage {
    bool     valid = false;
    uint32_t microvolts = 0;
    float    volts() const { return microvolts / 1e6f; }
};

enum class GpuClockDomain { Graphics, Memory };

// Tout ce qui a pu etre lu sur une carte. `handle` appartient au pilote qui
// l'a fourni : opaque ici, jamais interprete.
struct GpuReading {
    void*           handle = nullptr;
    std::string     name;
    GpuVoltage      voltage;
    GpuClocks       clocks;
    GpuThermal      thermal;
    GpuPowerLimit   power;
    GpuCoolers      coolers;
    GpuClockOffsets offsets;
};

// ---------------------------------------------------------------------------
// Capacites
// ---------------------------------------------------------------------------
// Ce que le controleur a REELLEMENT obtenu du pilote, pas ce que le fabricant
// promet dans sa documentation. Une carte d'entree de gamme sans ventilateur
// pilotable et une carte dont le point d'entree n'a pas ete resolu se
// presentent de la meme facon : capacite absente.
struct GpuCapabilities {
    bool set_power_limit = false;
    bool set_clock_offset = false;
    bool set_fan = false;

    bool any_write() const { return set_power_limit || set_clock_offset || set_fan; }
};

// ---------------------------------------------------------------------------
// Controleur
// ---------------------------------------------------------------------------
class IGpuController {
public:
    virtual ~IGpuController() = default;

    virtual GpuVendor   vendor() const = 0;
    // Nom de la liaison employee (« NVAPI », « ADLX »). Affiche tel quel dans
    // les diagnostics : savoir PAR QUOI on parle a la carte est la premiere
    // question quand un rapport arrive d'une machine qu'on n'a pas.
    virtual const char* backend_name() const = 0;
    virtual std::string driver_version() const = 0;

    virtual bool                     refresh() = 0;
    virtual size_t                   gpu_count() const = 0;
    virtual const GpuReading&        gpu(size_t index) const = 0;
    virtual GpuCapabilities          capabilities() const = 0;

    // Chaque ecriture est bornee avant l'appel, relue apres, et volatile :
    // elle disparait au redemarrage. C'est ce qui la rend sure.
    virtual Result set_power_limit_pct(size_t index, float pct) = 0;
    virtual Result set_clock_offset_mhz(size_t index, GpuClockDomain d, int32_t mhz) = 0;
    virtual Result set_fan_level_pct(size_t index, uint32_t level_pct) = 0;
    virtual Result set_fan_auto(size_t index) = 0;

    // Detail propre a la liaison, verse dans le rapport de diagnostic. Ce
    // qu'une liaison expose n'a pas d'equivalent chez l'autre : chercher un
    // denominateur commun ferait perdre justement ce qui sert a comprendre
    // une machine qu'on n'a pas.
    virtual Json backend_details() const { return Json::object(); }

    // Ce qui a ete lu, sous une forme comparable d'un fabricant a l'autre.
    Json reading_to_json(size_t index) const;

    // Plancher de consigne manuelle. Arreter les ventilateurs sous charge est
    // le seul moyen d'abimer physiquement quelque chose ici.
    static constexpr uint32_t kMinManualFanPct = 30;
};

// Cherche un controleur pour la carte principale. Renvoie nullptr si aucun
// fabricant reconnu n'a repondu ; `why_not` recoit alors la raison, destinee a
// etre affichee ET journalisee — sur une machine qu'on n'a pas sous la main,
// c'est la seule chose qui permettra de comprendre.
std::unique_ptr<IGpuController> make_gpu_controller(std::string* why_not);

// Section GPU du rapport de diagnostic. Construit son propre controleur :
// elle doit pouvoir etre produite meme quand l'application n'en a pas ouvert,
// et surtout DIRE qu'elle n'a pas pu en ouvrir.
Json gpu_diagnostic_json();

} // namespace tf::hw
