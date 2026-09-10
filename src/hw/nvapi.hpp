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
#include "hw/gpu.hpp"

namespace tf::hw {

// Une fonction NVAPI resolue, ou la raison de son absence.
struct NvapiEntryPoint {
    std::string name;
    uint32_t    id = 0;
    bool        resolved = false;
};

// Vocabulaire commun a tous les fabricants. NVAPI a servi de modele a ces
// structures : les noms d'origine restent en alias, ce qui evite de toucher a
// nvapi.cpp — mille lignes de retro-ingenierie deja validees sur une vraie
// carte, qu'un renommage massif ne ferait que fragiliser.
using NvClocks       = GpuClocks;
using NvThermal      = GpuThermal;
using NvPowerLimit   = GpuPowerLimit;
using NvCooler       = GpuCooler;
using NvCoolers      = GpuCoolers;
using NvClockOffsets = GpuClockOffsets;
using NvVoltage      = GpuVoltage;
using NvClockDomain  = GpuClockDomain;
using NvGpu          = GpuReading;

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
    static constexpr uint32_t kMinManualFanPct = IGpuController::kMinManualFanPct;
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

// ---------------------------------------------------------------------------
// Adaptateur vers l'interface commune
// ---------------------------------------------------------------------------
// Nvapi reste utilisable directement pour ce qui lui est propre : la table des
// points d'entree et les outils de sondage n'ont aucun equivalent chez AMD, et
// les faire remonter dans IGpuController aurait impose a chaque fabricant un
// vocabulaire qui n'est pas le sien.
class NvapiController final : public IGpuController {
public:
    bool init() { return nv_.init(); }

    GpuVendor   vendor() const override { return GpuVendor::Nvidia; }
    const char* backend_name() const override { return "NVAPI"; }
    std::string driver_version() const override { return nv_.driver_version(); }

    bool              refresh() override { return nv_.refresh(); }
    size_t            gpu_count() const override { return nv_.gpus().size(); }
    const GpuReading& gpu(size_t index) const override { return nv_.gpus()[index]; }
    GpuCapabilities   capabilities() const override;

    Result set_power_limit_pct(size_t i, float pct) override {
        return nv_.set_power_limit_pct(i, pct);
    }
    Result set_clock_offset_mhz(size_t i, GpuClockDomain d, int32_t mhz) override {
        return nv_.set_clock_offset_mhz(i, d, mhz);
    }
    Result set_fan_level_pct(size_t i, uint32_t lvl) override {
        return nv_.set_fan_level_pct(i, lvl);
    }
    Result set_fan_auto(size_t i) override { return nv_.set_fan_auto(i); }

    // La table des points d'entree resolus. C'est l'information la plus utile
    // d'un rapport NVIDIA : une fonction non resolue explique d'un coup
    // pourquoi telle capacite manque sur telle version de pilote.
    Json backend_details() const override;

    // Acces a la liaison brute, pour les diagnostics propres a NVIDIA.
    Nvapi&       raw() { return nv_; }
    const Nvapi& raw() const { return nv_; }

private:
    Nvapi nv_;
};

} // namespace tf::hw
