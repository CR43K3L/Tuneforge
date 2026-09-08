#pragma once
//
// Liaison NVAPI — v0.3, etape 1 : LECTURE SEULE.
//
// NVAPI n'exporte qu'un seul symbole utile, nvapi_QueryInterface. Toutes les
// fonctions s'obtiennent en lui passant un identifiant numerique. Une partie
// d'entre elles (offsets d'horloge, limites de puissance, ventilateurs) n'est
// pas documentee par NVIDIA : leurs identifiants et la disposition de leurs
// structures viennent de la retro-ingenierie, comme dans tous les outils du
// genre.
//
// Consequence directe sur la conception : un identifiant inconnu du pilote
// renvoie un pointeur nul, et une structure mal versionnee renvoie un code
// d'erreur. Aucun des deux ne plante. On enregistre donc precisement ce qui a
// pu etre resolu et ce qui a repondu, et tout le reste est declare
// indisponible plutot que devine.
//
// Aucune ecriture n'est possible depuis ce fichier : c'est deliberé tant que la
// lecture n'a pas ete validee sur plusieurs cartes.
//
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common/util.hpp"

namespace tf::hw {

// Une fonction NVAPI resolue, ou la raison de son absence.
struct NvapiEntryPoint {
    std::string name;
    uint32_t    id = 0;
    bool        resolved = false;
};

struct NvClocks {
    bool     valid = false;
    uint32_t graphics_khz = 0;
    uint32_t memory_khz = 0;
    uint32_t processor_khz = 0;
    uint32_t video_khz = 0;
};

struct NvThermal {
    bool    valid = false;
    int32_t gpu_c = 0;      // capteur GPU
    int32_t memory_c = 0;   // -1 si absent
    int32_t power_supply_c = 0;
    int32_t board_c = 0;
    int     sensor_count = 0;
};

// Limite de puissance. NVAPI la compte en millipourcents de la valeur par
// defaut : 100000 = 100 %. On expose des pourcentages, et on conserve la
// valeur brute pour l'ecriture a venir.
struct NvPowerLimit {
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

struct NvCooler {
    uint32_t index = 0;
    uint32_t current_level = 0;   // %
    uint32_t min_level = 0;
    uint32_t max_level = 0;
    uint32_t current_policy = 0;      // 1 = manuel, 32 = automatique
    bool     policy_known = false;    // l'API recente ne la rapporte pas
    bool     range_known = false;     // idem pour les bornes de niveau
    bool     active = false;
};

struct NvCoolers {
    bool                  valid = false;
    std::vector<NvCooler> items;
    uint32_t              tach_rpm = 0;
    bool                  tach_valid = false;
};

// Decalages d'horloge appliques par rapport a la courbe d'origine, en kHz.
// C'est ce que manipule un undervolt ou un overclock : la courbe elle-meme
// n'est jamais remplacee, on lui applique un delta.
struct NvClockOffsets {
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

enum class NvClockDomain { Graphics, Memory };

// Tension du rail principal, lue en direct. C'est la mesure de reference pour
// juger d'un undervolt : la frequence seule ne dit rien.
struct NvVoltage {
    bool     valid = false;
    uint32_t microvolts = 0;
    float    volts() const { return microvolts / 1e6f; }
};

struct NvGpu {
    void*       handle = nullptr;
    std::string name;
    NvVoltage   voltage;
    NvClocks       clocks;
    NvThermal      thermal;
    NvPowerLimit   power;
    NvCoolers      coolers;
    NvClockOffsets offsets;
};

class Nvapi {
public:
    Nvapi() = default;
    ~Nvapi();
    Nvapi(const Nvapi&) = delete;
    Nvapi& operator=(const Nvapi&) = delete;

    // Charge nvapi64.dll et resout les points d'entree. Ne touche a rien.
    bool init();
    bool available() const { return ready_; }
    const std::string& load_error() const { return load_error_; }

    // Ce qui a pu etre resolu : sert au diagnostic et a l'affichage honnete
    // des capacites.
    const std::vector<NvapiEntryPoint>& entry_points() const { return entries_; }

    // Rafraichit toutes les mesures. Chaque bloc echoue independamment.
    bool refresh();
    const std::vector<NvGpu>& gpus() const { return gpus_; }

    // Version du pilote telle que NVAPI la rapporte.
    const std::string& driver_version() const { return driver_version_; }

    // --- Ecriture ----------------------------------------------------------
    // Regle la limite de puissance, en pourcentage de la valeur par defaut.
    // La valeur est refusee si elle sort des bornes que le pilote annonce :
    // on ne tente jamais d'ecrire hors plage. Apres ecriture, la valeur est
    // relue et comparee ; un ecart fait echouer l'appel.
    //
    // Ce reglage est VOLATILE : il disparait au redemarrage et a chaque
    // rechargement du pilote. C'est ce qui le rend sur — un mauvais reglage se
    // corrige en rebootant.
    Result set_power_limit_pct(size_t gpu_index, float pct);
    bool   can_set_power_limit() const;

    // Applique un decalage d'horloge, en MHz, par rapport a la courbe d'origine.
    // La courbe n'est jamais remplacee : on lui ajoute un delta, ce qui laisse
    // le pilote gerer les tensions. Volatile, comme la limite de puissance.
    Result set_clock_offset_mhz(size_t gpu_index, NvClockDomain domain, int32_t mhz);
    bool   can_set_clock_offset() const;

    // Ventilateurs. La structure de controle n'est pas publiee : plutot que de
    // la reconstruire, on demande au pilote la sienne, on n'y modifie que le
    // niveau et le mode, et on la lui rend. Rien n'est suppose sur le reste.
    //
    // Un niveau manuel inferieur a 30 % est refuse : arreter les ventilateurs
    // pendant une charge est le seul moyen d'abimer quelque chose ici.
    static constexpr uint32_t kMinManualFanPct = 30;
    Result set_fan_level_pct(size_t gpu_index, uint32_t level_pct);
    Result set_fan_auto(size_t gpu_index);
    bool   can_set_fan() const;

    // Outil de mise au point : cherche la taille de structure qu'accepte
    // NvAPI_GPU_ClientFanCoolersGetStatus. Le pilote valide la taille encodee
    // dans le champ version ; on balaie donc les tailles jusqu'a en trouver une
    // qu'il accepte. Strictement en lecture, et jamais appele automatiquement.
    struct ProbeHit { uint32_t size; uint32_t version; uint32_t reported_count; };
    std::vector<ProbeHit> probe_fan_status_layout();

    // Sondage generalise : cherche la taille de structure qu'accepte une
    // fonction. Restreint a une liste blanche de fonctions de LECTURE — sonder
    // une fonction d'ecriture avec une disposition inconnue reviendrait a lui
    // envoyer des octets arbitraires.
    static std::vector<std::string> probeable_functions();
    std::vector<ProbeHit>           probe_layout(const std::string& function_name);

    // Contenu brut renvoye par une fonction de lecture, une fois sa taille
    // trouvee. C'est en lisant les donnees reelles qu'on deduit la disposition,
    // plutot qu'en la devinant.
    std::vector<uint8_t> dump_layout(const std::string& function_name);

private:
    bool                         ready_ = false;
    void*                        module_ = nullptr;
    std::string                  load_error_;
    std::string                  driver_version_;
    std::vector<NvapiEntryPoint> entries_;
    std::vector<NvGpu>           gpus_;
};

// Texte lisible pour un code de retour NVAPI.
std::string nvapi_status_text(int status);

} // namespace tf::hw
