#pragma once
//
// Contrat commun a tous les reglages.
//
// Invariant du projet : rien ne s'applique sans que l'etat precedent ait ete
// capture d'abord. capture() est appele AVANT apply(), son resultat est
// persiste, et restore() doit savoir remettre exactement cet etat — y compris
// « la valeur n'existait pas », qui se restaure par une suppression.
//
#include <memory>
#include <string>
#include <vector>

#include "common/json.hpp"
#include "common/util.hpp"
#include "hw/hardware.hpp"

namespace tf {

// Niveau d'exposition : voir la doctrine « trois niveaux d'interface ».
enum class Tier {
    Basic,     // 100 % reversible, aucun risque materiel
    Advanced,  // effets de bord possibles, demande une confirmation
    Expert     // reserve aux utilisateurs qui savent ce qu'ils font
};

enum class TweakState {
    Unknown,
    Applied,
    NotApplied,
    Partial,      // certains sous-reglages seulement
    Unavailable
};

struct TweakMeta {
    std::string id;
    std::string title;
    std::string what;         // ce que ca fait, en une phrase
    std::string why;          // gain reel attendu, sans promesse chiffree
    Tier        tier = Tier::Basic;
    bool        needs_admin = true;
    bool        needs_reboot = false;
    std::string caveat;       // contrepartie honnete (chaleur, bruit, ...)
};

class ITweak {
public:
    virtual ~ITweak() = default;

    virtual const TweakMeta& meta() const = 0;

    // Ce materiel / cet OS supporte-t-il le reglage ?
    virtual bool        available(const hw::Profile& p) const = 0;
    virtual std::string unavailable_reason(const hw::Profile& p) const { (void)p; return {}; }

    // Etat courant, pour affichage et pour decider s'il y a quelque chose a faire.
    virtual TweakState  state() const = 0;
    virtual std::string current_value() const = 0;
    virtual std::string target_value() const = 0;

    // Instantane de l'etat courant (persiste avant toute modification).
    virtual Json   capture() const = 0;
    virtual Result apply() = 0;
    virtual Result restore(const Json& snapshot) = 0;
};

using TweakPtr = std::unique_ptr<ITweak>;

// Construit le catalogue complet. L'ordre est celui de l'affichage.
std::vector<TweakPtr> build_all_tweaks();

const char* to_string(Tier t);
const char* to_string(TweakState s);

} // namespace tf
