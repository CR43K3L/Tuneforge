//
// tuneforge-reset — outil de secours autonome.
//
// Il existe pour un seul scenario : Tuneforge ne demarre plus, ou la machine
// se comporte mal et l'utilisateur veut tout remettre en etat sans reflechir.
// Il ne depend d'aucun profil, ne lit aucune configuration, ne pose qu'une
// question, et n'a qu'un seul chemin d'execution.
//
#include <windows.h>

#include <cstdio>
#include <iostream>
#include <string>

#include "core/engine.hpp"
#include "hw/nvapi.hpp"
#include "platform/elevation.hpp"

using namespace tf;

namespace {

void line(const std::string& s = {}) {
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fputc('\n', stdout);
}

} // namespace

int main(int argc, char** argv) {
    enable_vt_console();
    log_init(data_dir() + L"\\tuneforge.log");

    bool assume_yes = false;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--yes" || s == "-y") assume_yes = true;
    }

    line();
    line("  TUNEFORGE — RESTAURATION COMPLETE");
    line("  ---------------------------------");
    line();
    line("  Cet outil remet tous les reglages modifies par Tuneforge dans leur");
    line("  etat d'origine, tel qu'il a ete capture avant chaque modification.");
    line();

    if (!is_elevated()) {
        line("  Elevation necessaire : relance en administrateur...");
        if (relaunch_elevated({"--yes"}, true)) return 0;
        line("  Elevation refusee. Faites un clic droit > Executer en tant qu'administrateur.");
        line();
        std::string dummy;
        std::getline(std::cin, dummy);
        return 3;
    }

    // Les ventilateurs d abord, avant meme de regarder l etat : la courbe de
    // Tuneforge n existe que tant que l interface tourne, et un arret brutal
    // les laisse bloques au dernier niveau ecrit. C est exactement le genre de
    // situation pour laquelle cet outil existe.
    {
        hw::Nvapi nv;
        if (nv.init() && nv.refresh() && !nv.gpus().empty() && nv.can_set_fan()) {
            if (nv.set_fan_auto(0)) {
                line("  Ventilateurs du GPU rendus au pilote.");
                line();
            }
        }
    }

    Engine engine;
    auto   ids = engine.state().applied_ids();

    if (ids.empty() && !DirtyFlag::exists()) {
        line("  Rien a restaurer : aucun reglage n'a ete modifie par Tuneforge.");
        line();
        if (!assume_yes) {
            std::string dummy;
            std::getline(std::cin, dummy);
        }
        return 0;
    }

    line(std::format("  {} reglage(s) a restaurer :", ids.size()));
    for (const auto& id : ids) line("    - " + id);
    line();

    if (!assume_yes) {
        std::fputs("  Confirmer la restauration ? (O/n) ", stdout);
        std::fflush(stdout);
        std::string answer;
        std::getline(std::cin, answer);
        if (!answer.empty() && answer != "O" && answer != "o") {
            line("  Annule.");
            return 0;
        }
    }

    Engine::Outcome o = engine.revert_all();

    line();
    if (!o.applied.empty()) {
        line(std::format("  {} reglage(s) restaure(s).", o.applied.size()));
    }
    if (!o.failed.empty()) {
        line(std::format("  {} echec(s) :", o.failed.size()));
        for (const auto& f : o.failed) line("    ! " + f);
        line();
        line("  Les valeurs d'origine restent dans l'etat, vous pouvez relancer cet outil.");
    }
    if (o.needs_reboot) {
        line();
        line("  Redemarrez pour que la restauration soit complete.");
    }
    line();
    line(std::format("  Journal : {}", narrow(data_dir() + L"\\tuneforge.log")));
    line();

    if (!assume_yes) {
        std::fputs("  Appuyez sur Entree pour fermer...", stdout);
        std::fflush(stdout);
        std::string dummy;
        std::getline(std::cin, dummy);
    }
    log_close();
    return o.failed.empty() ? 0 : 1;
}
