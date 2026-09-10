#pragma once
//
// Sondage AMD par ADL — v0.5, etape 1 : RECONNAISSANCE.
//
// Pourquoi ADL et pas ADLX
// ------------------------
// ADLX est l'API moderne d'AMD, mais elle est distribuee sous un accord de
// licence proprietaire (un PDF, pas une licence libre) et son interface est
// faite d'objets a table de fonctions virtuelles. En reconstruire les tables
// sans les en-tetes officielles reviendrait a deviner un ordre d'appel : se
// tromper d'une entree, c'est appeler une autre fonction que celle voulue.
// Or ce projet s'interdit deja exactement cela cote NVIDIA.
//
// ADL, elle, est une API C plate : des fonctions independantes resolues une a
// une par GetProcAddress, exactement comme nvapi64.dll. Le meme precedent
// s'applique donc — liaison dynamique, aucune en-tete du fabricant embarquee.
//
// Ce que fait cette etape, et ce qu'elle ne fait pas
// --------------------------------------------------
// Elle n'appelle QUE des fonctions dont les parametres sont des entiers : le
// nombre d'adaptateurs et les capacites Overdrive. Aucune structure n'est
// echangee avec le pilote, donc aucune disposition n'est supposee. C'est ce
// qui la rend sure a executer sur une machine qu'on n'a pas.
//
// Elle ne regle rien. Elle repond a une seule question, celle qui manque
// aujourd'hui pour aller plus loin : « quelle generation d'Overdrive cette
// carte expose-t-elle ? » La reponse decide de ce qu'il faudra implementer,
// et elle ne peut venir que de vraies machines AMD.
//
#include <cstdint>
#include <string>
#include <vector>

#include "common/json.hpp"

namespace tf::hw {

struct AdlProbe {
    bool        loaded = false;        // atiadlxx.dll chargee
    bool        context_created = false;
    std::string error;                 // pourquoi, le cas echeant
    int         adapter_count = 0;

    // Par adaptateur actif : ce que ADL2_Overdrive_Caps a repondu.
    struct Adapter {
        int  index = 0;
        // Codes bruts des deux interrogations. Mesure faite sur un Radeon
        // integre : Active_Get accepte des indices qu'Overdrive_Caps rejette
        // ensuite comme invalides. Aucune des deux ne suffit donc a decider
        // seule ce qu'est un adaptateur reel — on rapporte les deux reponses
        // telles quelles plutot que de trancher avec une regle inventee.
        int  active_status = 0;
        bool active = false;
        bool overdrive_supported = false;
        bool overdrive_enabled = false;
        int  overdrive_version = 0;   // 5, 6, 7 (OverdriveN) ou 8
        int  caps_status = 0;         // code de retour ADL brut
    };
    std::vector<Adapter> adapters;

    // Version d'ADLX si amdadlx64.dll est presente. Simple constat : sa seule
    // fonction plate exportee est une requete de version, elle ne coute rien
    // et dit si la machine pourrait un jour servir de cible ADLX.
    bool        adlx_present = false;
    uint64_t    adlx_version = 0;
    std::string adlx_version_text;   // « 1.4.0.121 »
};

// Traduction d'un code de retour ADL. Un « -8 » brut dans un rapport envoye
// par un testeur n'apprend rien ; « non supporte » repond a la question.
const char* adl_status_text(int status);

// Ne modifie rien, n'echange aucune structure avec le pilote.
AdlProbe probe_adl();

Json adl_probe_json(const AdlProbe& p);

} // namespace tf::hw
