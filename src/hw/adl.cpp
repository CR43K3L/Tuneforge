//
// Sondage AMD par ADL. Voir adl.hpp pour le raisonnement.
//
#include "hw/adl.hpp"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "common/util.hpp"

namespace tf::hw {
namespace {

constexpr int kAdlOk = 0;

// ADL alloue par l'intermediaire d'un rappel fourni par l'appelant. Sur x64
// Windows il n'existe qu'une convention d'appel, mais __stdcall est conserve
// pour rester fidele a la declaration d'AMD.
void* __stdcall adl_alloc(int size) {
    return size > 0 ? std::malloc(static_cast<size_t>(size)) : nullptr;
}

using ADL_CONTEXT_HANDLE = void*;
using PFN_MallocCallback = void* (__stdcall*)(int);

using PFN_Main_Control_Create  = int (*)(PFN_MallocCallback, int, ADL_CONTEXT_HANDLE*);
using PFN_Main_Control_Destroy = int (*)(ADL_CONTEXT_HANDLE);
using PFN_NumberOfAdapters_Get = int (*)(ADL_CONTEXT_HANDLE, int*);
using PFN_Overdrive_Caps       = int (*)(ADL_CONTEXT_HANDLE, int, int*, int*, int*);
using PFN_Adapter_Active_Get   = int (*)(ADL_CONTEXT_HANDLE, int, int*);

// amdadlx64.dll n'exporte qu'une poignee de fonctions plates ; celle-ci rend
// la version du composant ADLX. Aucun objet, aucune table virtuelle.
using PFN_ADLXQueryFullVersion = long (*)(unsigned long long*);

template <typename T>
T resolve(HMODULE m, const char* name) {
    return reinterpret_cast<T>(reinterpret_cast<void*>(::GetProcAddress(m, name)));
}

// La version ADLX est empaquetee : majeur<<48 | mineur<<32 | correctif<<16 |
// compilation. Telle quelle, c'est un entier de quinze chiffres illisible.
std::string adlx_version_text(uint64_t v) {
    if (!v) return {};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%llu.%llu.%llu.%llu",
                  (v >> 48) & 0xFFFF, (v >> 32) & 0xFFFF, (v >> 16) & 0xFFFF, v & 0xFFFF);
    return buf;
}

}  // namespace

const char* adl_status_text(int status) {
    switch (status) {
        case 0:   return "OK";
        case 1:   return "OK, avec avertissement";
        case -1:  return "erreur generique";
        case -2:  return "ADL non initialise";
        case -3:  return "parametre invalide";
        case -4:  return "taille de parametre invalide";
        case -5:  return "indice d'adaptateur invalide";
        case -6:  return "indice de controleur invalide";
        case -7:  return "indice d'affichage invalide";
        case -8:  return "non supporte par cet adaptateur";
        case -9:  return "pointeur nul";
        case -10: return "adaptateur desactive";
        case -11: return "fonction non prise en charge par le pilote";
        case -12: return "memoire insuffisante";
        case -20: return "erreur d'initialisation";
        default:  return "code inconnu";
    }
}

AdlProbe probe_adl() {
    AdlProbe p;

    // --- ADLX : simple constat de presence ----------------------------------
    if (HMODULE adlx = ::LoadLibraryW(L"amdadlx64.dll")) {
        p.adlx_present = true;
        if (auto q = resolve<PFN_ADLXQueryFullVersion>(adlx, "ADLXQueryFullVersion")) {
            unsigned long long v = 0;
            if (q(&v) == 0) {
                p.adlx_version = v;
                p.adlx_version_text = adlx_version_text(v);
            }
        }
        ::FreeLibrary(adlx);
    }

    // --- ADL ----------------------------------------------------------------
    HMODULE lib = ::LoadLibraryW(L"atiadlxx.dll");
    if (!lib) {
        p.error = "atiadlxx.dll introuvable (pilote AMD absent ?)";
        return p;
    }
    p.loaded = true;

    auto create  = resolve<PFN_Main_Control_Create>(lib, "ADL2_Main_Control_Create");
    auto destroy = resolve<PFN_Main_Control_Destroy>(lib, "ADL2_Main_Control_Destroy");
    auto count   = resolve<PFN_NumberOfAdapters_Get>(lib, "ADL2_Adapter_NumberOfAdapters_Get");
    auto caps    = resolve<PFN_Overdrive_Caps>(lib, "ADL2_Overdrive_Caps");
    auto active  = resolve<PFN_Adapter_Active_Get>(lib, "ADL2_Adapter_Active_Get");

    if (!create || !destroy || !count) {
        p.error = "points d'entree ADL2 de base non resolus";
        ::FreeLibrary(lib);
        return p;
    }

    ADL_CONTEXT_HANDLE ctx = nullptr;
    // Le second parametre demande a ADL de n'enumerer que les adaptateurs
    // connectes. Mesure faite sur un Radeon integre : il rapporte quand meme
    // neuf entrees, dont quatre repondent « indice invalide ». Le compte rendu
    // et l'espace d'indices ne coincident donc PAS — il faut interroger chaque
    // indice et garder ceux qui repondent, pas iterer aveuglement.
    const int rc = create(adl_alloc, 1, &ctx);
    if (rc != kAdlOk || !ctx) {
        p.error = "ADL2_Main_Control_Create a repondu " + std::to_string(rc);
        ::FreeLibrary(lib);
        return p;
    }
    p.context_created = true;

    if (count(ctx, &p.adapter_count) != kAdlOk) {
        p.error = "ADL2_Adapter_NumberOfAdapters_Get a echoue";
        p.adapter_count = 0;
    }

    // Overdrive_Caps ne prend que des entiers : aucune disposition de
    // structure n'est supposee, donc rien a deviner. C'est precisement ce qui
    // permet de lancer ce sondage sur une machine inconnue.
    if (caps) {
        for (int i = 0; i < p.adapter_count && i < 32; ++i) {
            AdlProbe::Adapter a;
            a.index = i;

            if (active) {
                int st = 0;
                a.active_status = active(ctx, i, &st);
                a.active = (a.active_status == kAdlOk && st != 0);
            }

            int supported = 0, enabled = 0, version = 0;
            a.caps_status = caps(ctx, i, &supported, &enabled, &version);
            if (a.caps_status == kAdlOk) {
                a.overdrive_supported = supported != 0;
                a.overdrive_enabled = enabled != 0;
                a.overdrive_version = version;
            }
            p.adapters.push_back(a);
        }
    } else if (p.error.empty()) {
        p.error = "ADL2_Overdrive_Caps non resolu";
    }

    destroy(ctx);
    ::FreeLibrary(lib);

    log_info("sondage ADL : {} adaptateur(s), ADLX {}", p.adapter_count,
             p.adlx_present ? "presente" : "absente");
    return p;
}

Json adl_probe_json(const AdlProbe& p) {
    Json j = Json::object();
    j.set("adl_loaded", p.loaded);
    j.set("adl_context", p.context_created);
    if (!p.error.empty()) j.set("error", p.error);
    j.set("adapter_count", static_cast<int64_t>(p.adapter_count));
    j.set("adlx_present", p.adlx_present);
    if (!p.adlx_version_text.empty()) j.set("adlx_version", p.adlx_version_text);

    Json arr = Json::array();
    for (const auto& a : p.adapters) {
        Json e = Json::object();
        e.set("index", static_cast<int64_t>(a.index));
        e.set("active_status", static_cast<int64_t>(a.active_status));
        e.set("active_status_text", adl_status_text(a.active_status));
        e.set("active", a.active);
        e.set("caps_status", static_cast<int64_t>(a.caps_status));
        e.set("caps_status_text", adl_status_text(a.caps_status));
        e.set("overdrive_supported", a.overdrive_supported);
        e.set("overdrive_enabled", a.overdrive_enabled);
        e.set("overdrive_version", static_cast<int64_t>(a.overdrive_version));
        arr.push(std::move(e));
    }
    j.set("adapters", std::move(arr));
    return j;
}

} // namespace tf::hw
