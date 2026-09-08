#pragma once
//
// Elevation : detection et relance UAC. L'app n'exige PAS l'admin dans son
// manifeste — « detect », « monitor » et « list » fonctionnent sans elevation.
//
#include <string>
#include <vector>

#include "common/util.hpp"

namespace tf {

bool is_elevated();

// Relance le processus courant avec elevation en lui repassant les arguments.
// Retourne true si le processus eleve a ete lance (l'appelant doit alors sortir).
bool relaunch_elevated(const std::vector<std::string>& args, bool wait_for_exit = true);

// Empeche deux instances d'appliquer des tweaks en meme temps.
class SingleInstance {
public:
    explicit SingleInstance(const std::wstring& name);
    ~SingleInstance();
    bool acquired() const { return acquired_; }

private:
    void* handle_ = nullptr;
    bool  acquired_ = false;
};

} // namespace tf
