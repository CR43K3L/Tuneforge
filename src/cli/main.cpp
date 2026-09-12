//
// Tuneforge — interface en ligne de commande de la v0.1.
//
// Toute la logique vit dans tfcore : cette couche ne fait que presenter.
// L'interface graphique de la v0.2 consommera exactement les memes appels.
//
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "core/engine.hpp"
#include "hw/gpu.hpp"
#include "hw/nvapi.hpp"
#include "hw/sensors.hpp"
#include "platform/elevation.hpp"

using namespace tf;

namespace {

bool g_color = false;
volatile bool g_stop = false;

const char* C_RESET  = "";
const char* C_BOLD   = "";
const char* C_DIM    = "";
const char* C_GREEN  = "";
const char* C_YELLOW = "";
const char* C_RED    = "";
const char* C_CYAN   = "";

void init_colors() {
    g_color = enable_vt_console();
    if (!g_color) return;
    C_RESET = "\x1b[0m";  C_BOLD = "\x1b[1m";   C_DIM = "\x1b[90m";
    C_GREEN = "\x1b[32m"; C_YELLOW = "\x1b[33m"; C_RED = "\x1b[31m";
    C_CYAN = "\x1b[36m";
}

void out(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stdout); }
void outln(const std::string& s = {}) { out(s); out("\n"); }

BOOL WINAPI ctrl_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        g_stop = true;
        return TRUE;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
struct Args {
    std::string              command;
    std::vector<std::string> positional;
    bool advanced = false, expert = false, dry_run = false, all = false, yes = false;
    bool pause_at_exit = false, no_elevate = false;
    int  watchdog = 0;
    int  interval_ms = 1000;
    std::vector<std::string> raw;
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        a.raw.push_back(s);
        if (s == "--advanced" || s == "-a")      a.advanced = true;
        else if (s == "--expert")                a.expert = true;
        else if (s == "--dry-run" || s == "-n")  a.dry_run = true;
        else if (s == "--all")                   a.all = true;
        else if (s == "--yes" || s == "-y")      a.yes = true;
        else if (s == "--pause")                 a.pause_at_exit = true;
        else if (s == "--no-elevate")            a.no_elevate = true;
        else if (s == "--watchdog" && i + 1 < argc) {
            a.watchdog = std::atoi(argv[++i]);
            a.raw.push_back(argv[i]);
        } else if (s == "--interval" && i + 1 < argc) {
            a.interval_ms = std::atoi(argv[++i]);
            a.raw.push_back(argv[i]);
        }
        else if (!s.empty() && s[0] == '-')       { /* option inconnue, ignoree */ }
        else if (a.command.empty())               a.command = s;
        else                                      a.positional.push_back(s);
    }
    return a;
}

void print_help() {
    outln();
    outln(std::string(C_BOLD) + "Tuneforge " + TF_VERSION + C_RESET +
          "  —  optimisation Windows reversible, 100 % usermode");
    outln();
    outln(std::string(C_BOLD) + "USAGE" + C_RESET);
    outln("  tuneforge <commande> [options]");
    outln();
    outln(std::string(C_BOLD) + "LECTURE (aucune elevation requise)" + C_RESET);
    outln("  detect              Materiel, systeme, securite et capacites detectees");
    outln("  list [--all]        Catalogue des reglages et leur etat actuel");
    outln("  status              Ce que Tuneforge a modifie sur cette machine");
    outln("  monitor             Telemetrie temps reel (Ctrl+C pour quitter)");
    outln("  report [fichier]    Rapport de diagnostic JSON, anonyme");
    outln("  gpu                 Ce que NVAPI expose sur la carte NVIDIA (lecture seule)");
    outln();
    outln(std::string(C_BOLD) + "MODIFICATION (elevation requise)" + C_RESET);
    outln("  apply <id> [id...]  Applique des reglages precis");
    outln("  revert <id> [id...] Restaure les reglages indiques");
    outln("  revert --all        Restaure tout ce que Tuneforge a modifie");
    outln("  recover             Repare un lot interrompu (crash, coupure)");
    outln("  gpu-power <pct>     Regle la limite de puissance GPU (volatile, --reset pour");
    outln("                      revenir au defaut)");
    outln("  gpu-clock           Decalages d'horloge : --core <MHz> --mem <MHz>, --reset");
    outln("  gpu-fan <pct>       Ventilateurs en manuel (30 a 100), --auto pour rendre la main");
    outln();
    outln(std::string(C_BOLD) + "OPTIONS" + C_RESET);
    outln("  -n, --dry-run       Montre ce qui serait fait, n'ecrit rien");
    outln("  -a, --advanced      Autorise les reglages de niveau avance");
    outln("      --expert        Autorise les reglages de niveau expert");
    outln("      --watchdog N    Annule automatiquement si non confirme en N secondes");
    outln("      --interval MS   Periode de rafraichissement de monitor (defaut 1000)");
    outln("  -y, --yes           Ne pose pas de question");
    outln("      --no-elevate    N'essaie pas de relancer en administrateur");
    outln();
    outln(std::string(C_DIM) +
          "Tuneforge ne touche ni a la tension ni a la frequence du processeur, et "
          "n'installe aucun driver noyau.\n"
          "Les reglages GPU (puissance, horloges, ventilateurs) sont bornes par le "
          "pilote et perdus au redemarrage." + C_RESET);
    outln();
}

std::string state_colored(TweakState s) {
    switch (s) {
        case TweakState::Applied:     return std::string(C_GREEN)  + "applique" + C_RESET;
        case TweakState::NotApplied:  return std::string(C_DIM)    + "non applique" + C_RESET;
        case TweakState::Partial:     return std::string(C_YELLOW) + "partiel" + C_RESET;
        case TweakState::Unavailable: return std::string(C_RED)    + "indisponible" + C_RESET;
        default:                      return "inconnu";
    }
}

// ---------------------------------------------------------------------------
int cmd_detect(Engine& e) {
    outln();
    outln(std::string(C_BOLD) + "MATERIEL DETECTE" + C_RESET);
    outln();
    out(e.hardware().summary());
    outln();
    return 0;
}

int cmd_list(Engine& e, const Args& a) {
    const hw::Profile& p = e.hardware();
    outln();
    outln(std::string(C_BOLD) + "CATALOGUE DES REGLAGES" + C_RESET);
    outln();

    // Le catalogue est ordonne par theme, pas par niveau : on boucle sur les
    // niveaux pour ne pas reafficher « basique » apres « avance ».
    const Tier kTiers[] = {Tier::Basic, Tier::Advanced, Tier::Expert};
    for (Tier tier : kTiers) {
    bool group_open = false;
    for (const auto& t : e.tweaks()) {
            const TweakMeta& m = t->meta();
            if (m.tier != tier) continue;
            const bool avail = t->available(p);
            if (!avail && !a.all) continue;

            if (!group_open) {
                group_open = true;
                outln();
                outln(std::string(C_CYAN) + "  ── niveau " + to_string(m.tier) + " " +
                      std::string(40, '-') + C_RESET);
            }
            outln();
            outln(std::format("  {}{}{}  {}", C_BOLD, m.id, C_RESET, m.title));
            outln(std::format("    {}", m.what));
            outln(std::format("    {}Gain : {}{}", C_DIM, m.why, C_RESET));
            if (!m.caveat.empty()) {
                outln(std::format("    {}Contrepartie : {}{}", C_YELLOW, m.caveat, C_RESET));
            }
            if (!avail) {
                outln(std::format("    {}Indisponible : {}{}", C_RED,
                                  t->unavailable_reason(p), C_RESET));
                continue;
            }
            outln(std::format("    Etat : {}   actuel [{}]   cible [{}]{}",
                              state_colored(t->state()), t->current_value(), t->target_value(),
                              m.needs_reboot ? "   redemarrage requis" : ""));
            if (e.state().has_snapshot(m.id)) {
                outln(std::format("    {}Gere par Tuneforge : restaurable via revert{}", C_DIM, C_RESET));
            }
    }  // reglages du niveau
    }  // niveaux
    outln();
    if (!a.all) outln(std::string(C_DIM) + "  (--all pour voir aussi les reglages "
                                           "indisponibles sur cette machine)" + C_RESET);
    outln();
    return 0;
}

int cmd_status(Engine& e) {
    outln();
    outln(std::string(C_BOLD) + "ETAT" + C_RESET);
    outln();
    outln(std::format("  Fichier d'etat   {}", narrow(e.state().path())));
    outln(std::format("  Elevation        {}", is_elevated() ? "administrateur" : "utilisateur"));

    if (DirtyFlag::exists()) {
        outln();
        outln(std::string(C_RED) + "  Un lot d'application a ete interrompu." + C_RESET);
        outln(std::format("  Demarre a {}", DirtyFlag::read()["started"].as_string()));
        outln("  Lancez : tuneforge recover");
    }

    auto ids = e.state().applied_ids();
    outln();
    if (ids.empty()) {
        outln("  Aucun reglage modifie par Tuneforge sur cette machine.");
    } else {
        outln(std::format("  {} reglage(s) modifie(s) par Tuneforge :", ids.size()));
        for (const auto& id : ids) {
            const ITweak* t = e.find(id);
            outln(std::format("    {} {:<34} {}", "•", id,
                              t ? state_colored(t->state()) : std::string("(inconnu)")));
        }
        outln();
        outln(std::string(C_DIM) + "  tuneforge revert --all remet tout dans l'etat d'origine."
              + C_RESET);
    }
    outln();
    return 0;
}

void print_outcome(const Engine::Outcome& o, bool dry_run, bool reverting = false) {
    outln();
    if (dry_run) outln(std::string(C_YELLOW) + "  MODE SIMULATION — rien n'a ete ecrit" + C_RESET);

    if (!o.applied.empty()) {
        const char* verb = dry_run ? "seraient appliques"
                                   : (reverting ? "restaure(s)" : "appliques");
        outln(std::format("  {}{}{} reglage(s) {} :", C_GREEN, o.applied.size(), C_RESET, verb));
        for (const auto& s : o.applied) outln(std::string(reverting ? "    - " : "    + ") + s);
    }
    if (!o.already.empty()) {
        outln(std::format("  {}{} deja dans l'etat cible :{}", C_DIM, o.already.size(), C_RESET));
        for (const auto& s : o.already) outln("    = " + s);
    }
    if (!o.skipped.empty()) {
        outln(std::format("  {}{} ignore(s) :{}", C_YELLOW, o.skipped.size(), C_RESET));
        for (const auto& s : o.skipped) outln("    ~ " + s);
    }
    if (!o.failed.empty()) {
        outln(std::format("  {}{} en echec :{}", C_RED, o.failed.size(), C_RESET));
        for (const auto& s : o.failed) outln("    ! " + s);
    }
    if (o.needs_reboot && !dry_run) {
        outln();
        outln(std::string(C_YELLOW) +
              "  Un redemarrage est necessaire pour que tout prenne effet." + C_RESET);
    }
    outln();
}

int cmd_apply(Engine& e, const Args& a) {
    if (a.positional.empty()) {
        outln(std::string(C_RED) + "  Precisez un ou plusieurs identifiants de reglage." + C_RESET);
        outln("  tuneforge list   pour le catalogue complet");
        return 2;
    }
    const std::vector<std::string> ids = a.positional;

    Engine::Options opt;
    opt.dry_run        = a.dry_run;
    opt.allow_advanced = a.advanced || a.expert;
    opt.allow_expert   = a.expert;

    Engine::Outcome outcome = e.apply_ids(ids, opt);
    print_outcome(outcome, a.dry_run);

    if (a.dry_run || !outcome.any_change()) return outcome.failed.empty() ? 0 : 1;

    // Chien de garde : sans confirmation, on annule tout seul.
    if (a.watchdog > 0 && !a.yes) {
        outln(std::format("{}  Chien de garde arme : {} secondes pour confirmer.{}",
                          C_YELLOW, a.watchdog, C_RESET));
        outln("  Tapez O puis Entree pour conserver ces reglages, ou ne faites rien "
              "pour tout annuler.");

        std::vector<std::string> applied = outcome.applied;
        Watchdog wd(a.watchdog, [&e, applied]() {
            Engine::Outcome rb = e.revert_ids(applied);
            outln();
            outln(std::string(C_RED) + "  Delai ecoule : reglages annules." + C_RESET);
            print_outcome(rb, false, true);
        });

        std::string line;
        std::getline(std::cin, line);
        if (!wd.fired() && (line == "O" || line == "o" || line == "y" || line == "Y")) {
            wd.confirm();
            outln(std::string(C_GREEN) + "  Confirme." + C_RESET);
        } else if (!wd.fired()) {
            wd.confirm();
            outln("  Non confirme : annulation.");
            Engine::Outcome rb = e.revert_ids(applied);
            print_outcome(rb, false, true);
        }
    }
    return outcome.failed.empty() ? 0 : 1;
}

int cmd_revert(Engine& e, const Args& a) {
    Engine::Outcome o;
    if (a.all || (a.positional.size() == 1 && a.positional[0] == "all")) {
        o = e.revert_all();
    } else if (a.positional.empty()) {
        outln(std::string(C_RED) + "  Precisez des identifiants, ou --all." + C_RESET);
        return 2;
    } else {
        o = e.revert_ids(a.positional);
    }
    print_outcome(o, false, true);
    return o.failed.empty() ? 0 : 1;
}

int cmd_recover(Engine& e) {
    if (!DirtyFlag::exists()) {
        outln();
        outln("  Aucun lot interrompu : rien a reparer.");
        outln();
        return 0;
    }
    Engine::Outcome o = e.recover();
    print_outcome(o, false, true);
    return 0;
}

int cmd_report(Engine& e, const Args& a) {
    Json j = e.diagnostic_report();
    std::wstring path = a.positional.empty()
                            ? data_dir() + L"\\rapport-" + widen(timestamp_compact()) + L".json"
                            : widen(a.positional.front());
    if (!write_text_file(path, j.dump(2))) {
        outln(std::string(C_RED) + "  Ecriture du rapport impossible." + C_RESET);
        return 1;
    }
    outln();
    outln(std::format("  Rapport ecrit : {}", narrow(path)));
    outln(std::string(C_DIM) +
          "  Materiel, etat des reglages, capteurs et diagnostic GPU." + C_RESET);
    outln(std::string(C_DIM) +
          "  Ni nom d'utilisateur, ni numero de serie. Les identifiants propres a la "
          "machine" + C_RESET);
    outln(std::string(C_DIM) +
          "  (GUID de plan d'alimentation, d'interfaces reseau) sont remplaces par des "
          "jetons numerotes." + C_RESET);
    outln();
    return 0;
}

// ---------------------------------------------------------------------------
// v0.3 etape 1 : ce que NVAPI expose reellement sur cette carte, en lecture
// seule. Rien n'est ecrit.
int cmd_gpu(const Args& a) {
    (void)a;
    hw::Nvapi nv;
    outln();
    outln(std::string(C_BOLD) + "NVAPI — LECTURE SEULE" + C_RESET);
    outln();

    if (!nv.init()) {
        outln(std::string(C_RED) + "  Indisponible : " + nv.load_error() + C_RESET);
        outln();
        return 1;
    }

    outln(std::format("  Pilote NVIDIA    {}",
                      nv.driver_version().empty() ? "inconnu" : nv.driver_version()));
    outln();
    outln(std::string(C_CYAN) + "  Points d'entree" + C_RESET);
    for (const auto& e : nv.entry_points()) {
        outln(std::format("    {}{:<42}{} {}", e.resolved ? C_GREEN : C_RED, e.name, C_RESET,
                          e.resolved ? std::format("resolu   (0x{:08X})", e.id)
                                     : std::format("ABSENT   (0x{:08X})", e.id)));
    }

    if (!nv.refresh()) {
        outln();
        outln(std::string(C_RED) + "  Enumeration des GPU impossible." + C_RESET);
        return 1;
    }

    for (const auto& g : nv.gpus()) {
        outln();
        outln(std::string(C_BOLD) + "  " + (g.name.empty() ? "GPU" : g.name) + C_RESET);

        if (g.clocks.valid) {
            outln(std::format("    Horloges       graphique {} MHz, memoire {} MHz, "
                              "video {} MHz",
                              g.clocks.graphics_khz / 1000, g.clocks.memory_khz / 1000,
                              g.clocks.video_khz / 1000));
        } else {
            outln("    Horloges       non lisibles");
        }

        if (g.voltage.valid) {
            outln(std::format("    Tension        {:.3f} V", g.voltage.volts()));
        } else {
            outln("    Tension        non lisible");
        }

        if (g.thermal.valid) {
            outln(std::format("    Temperature    GPU {} degC ({} capteur(s))", g.thermal.gpu_c,
                              g.thermal.sensor_count));
        } else {
            outln("    Temperature    non lisible");
        }

        if (g.power.valid) {
            outln(std::format("    Puissance      limite actuelle {:.0f} %  "
                              "(min {:.0f} — defaut {:.0f} — max {:.0f})",
                              g.power.current_pct, g.power.min_pct, g.power.default_pct,
                              g.power.max_pct));
        } else {
            outln(std::string("    Limite de puissance  non lisible") +
                  C_DIM + "  (fonction non documentee : identifiant ou structure a revoir)" +
                  C_RESET);
        }

        if (g.coolers.valid) {
            for (const auto& c : g.coolers.items) {
                std::string extra;
                if (c.range_known) extra += std::format(", plage {}-{} %", c.min_level,
                                                        c.max_level);
                if (c.policy_known) {
                    extra += std::format(", politique {}",
                                         c.current_policy == 32 ? "automatique" : "manuelle");
                }
                outln(std::format("    Ventilateur {}  niveau {} %{}, {}", c.index,
                                  c.current_level, extra, c.active ? "actif" : "arrete"));
            }
        } else {
            outln("    Ventilateurs   non lisibles");
        }
        if (g.coolers.tach_valid) {
            outln(std::format("    Tachymetre     {} tr/min", g.coolers.tach_rpm));
        }

        if (g.offsets.valid) {
            outln(std::format("    Decalages      graphique {:+} MHz (plage {} a {}), "
                              "memoire {:+} MHz (plage {} a {})",
                              g.offsets.graphics_delta_khz / 1000,
                              g.offsets.graphics_min_khz / 1000,
                              g.offsets.graphics_max_khz / 1000,
                              g.offsets.memory_delta_khz / 1000,
                              g.offsets.memory_min_khz / 1000,
                              g.offsets.memory_max_khz / 1000));
            outln(std::format("    P-states       {}, {}", g.offsets.pstate_count,
                              g.offsets.editable ? "modifiables" : "non modifiables"));
        } else {
            outln("    Decalages      non lisibles");
        }
    }

    // Sondage optionnel des dispositions de structure non publiees.
    // Uniquement sur des fonctions de LECTURE.
    bool probe_all = false;
    for (const auto& s : a.raw) {
        if (s == "--probe" || s == "--probe-fans") probe_all = true;
    }
    if (probe_all) {
        outln();
        outln(std::string(C_CYAN) + "  Sondage des structures (lecture seule)" + C_RESET);
        for (const auto& name : hw::Nvapi::probeable_functions()) {
            bool resolved = false;
            for (const auto& e : nv.entry_points()) {
                if (e.name == name) resolved = e.resolved;
            }
            if (!resolved) {
                outln(std::format("    {}{:<40}{} non resolu", C_DIM, name, C_RESET));
                continue;
            }
            const auto hits = nv.probe_layout(name);
            if (hits.empty()) {
                outln(std::format("    {}{:<40}{} aucune taille acceptee", C_RED, name, C_RESET));
            } else {
                for (const auto& h : hits) {
                    outln(std::format("    {}{:<40}{} v{} taille {} octets, champ+4 = {}",
                                      C_GREEN, name, C_RESET, h.version, h.size,
                                      h.reported_count));
                }
            }
        }
    }

    // Vidage du contenu brut : c'est en lisant les donnees reelles qu'on deduit
    // la disposition d'une structure non publiee.
    for (size_t i = 0; i + 1 < a.raw.size(); ++i) {
        if (a.raw[i] != "--dump") continue;
        const std::string name = a.raw[i + 1];
        const auto buf = nv.dump_layout(name);
        outln();
        outln(std::string(C_CYAN) + "  Vidage de " + name + C_RESET);
        if (buf.empty()) {
            outln("    aucune donnee");
            continue;
        }
        outln(std::format("    {} octets, {} mots de 32 bits", buf.size(), buf.size() / 4));

        // Seuls les mots non nuls sont interessants : le reste est du
        // remplissage reserve.
        const uint32_t* d = reinterpret_cast<const uint32_t*>(buf.data());
        const size_t    n = buf.size() / 4;
        size_t          shown = 0;
        for (size_t k = 0; k < n && shown < 60; ++k) {
            if (d[k] == 0) continue;
            outln(std::format("      [{:4}] offset {:5}  = {:<12} 0x{:08X}", k, k * 4, d[k],
                              d[k]));
            ++shown;
        }
        size_t nonzero = 0;
        for (size_t k = 0; k < n; ++k) nonzero += (d[k] != 0) ? 1 : 0;
        outln(std::format("    {} mots non nuls au total", nonzero));
    }

    outln();
    outln(std::string(C_DIM) +
          "  Lecture seule : aucune ecriture n'est possible depuis cette commande." + C_RESET);
    outln();
    return 0;
}

// Ouvre le controleur de la carte principale, ou explique pourquoi il n'y en
// a pas. La raison vient du controleur lui-meme : elle nomme le fabricant
// detecte, ce qui evite le « indisponible » sec que personne ne sait
// interpreter a distance.
std::unique_ptr<hw::IGpuController> open_gpu() {
    std::string why;
    auto        ctl = hw::make_gpu_controller(&why);
    if (!ctl || ctl->gpu_count() == 0) {
        outln(std::string(C_RED) + "  " + (why.empty() ? "Aucun controleur GPU." : why) +
              C_RESET);
        return nullptr;
    }
    return ctl;
}

// ---------------------------------------------------------------------------
// Ecriture de la limite de puissance GPU. Reglage volatile : il disparait au
// redemarrage, ce qui en fait le point d'entree le plus sur.
int cmd_gpu_power(const Args& a) {
    outln();
    auto ctl = open_gpu();
    if (!ctl) return 1;

    if (!ctl->capabilities().set_power_limit) {
        outln(std::string(C_RED) +
              std::format("  Limite de puissance non pilotable par {} sur cette carte.",
                          ctl->backend_name()) +
              C_RESET);
        return 1;
    }

    const auto& g = ctl->gpu(0);
    if (!g.power.valid) {
        outln(std::string(C_RED) + "  Limite de puissance non lisible : ecriture refusee." +
              C_RESET);
        return 1;
    }

    const float before = g.power.current_pct;
    outln(std::format("  {}  —  limite actuelle {:.0f} %  (plage {:.0f} a {:.0f} %)", g.name,
                      before, g.power.min_pct, g.power.max_pct));

    bool  reset = false;
    float target = -1.0f;
    for (const auto& s : a.raw) {
        if (s == "--reset") reset = true;
    }
    if (reset) {
        target = g.power.default_pct;
    } else if (!a.positional.empty()) {
        target = static_cast<float>(std::atof(a.positional.front().c_str()));
    } else {
        outln();
        outln("  Usage : tuneforge gpu-power <pourcentage> [--watchdog N] [--yes]");
        outln("          tuneforge gpu-power --reset");
        outln();
        return 2;
    }

    outln();
    outln(std::format("  Cible : {:.0f} %", target));

    if (a.dry_run) {
        outln(std::string(C_YELLOW) + "  MODE SIMULATION — rien n'a ete ecrit" + C_RESET);
        outln();
        return 0;
    }

    Result r = ctl->set_power_limit_pct(0, target);
    if (!r) {
        outln(std::string(C_RED) + "  Echec : " + r.message + C_RESET);
        outln();
        return 1;
    }
    outln(std::string(C_GREEN) +
          std::format("  Applique : {:.0f} %", ctl->gpu(0).power.current_pct) + C_RESET);

    // Chien de garde : sans confirmation, on revient a la valeur d'avant.
    const int wd = a.watchdog > 0 ? a.watchdog : 15;
    if (!a.yes) {
        outln();
        outln(std::format("{}  Chien de garde : {} secondes pour confirmer.{}", C_YELLOW, wd,
                          C_RESET));
        outln("  Tapez O puis Entree pour conserver, ou ne faites rien pour revenir en arriere.");

        Watchdog guard(wd, [&ctl, before]() {
            Result rb = ctl->set_power_limit_pct(0, before);
            outln();
            outln(std::string(C_RED) +
                  std::format("  Delai ecoule : retour a {:.0f} %{}", before,
                              rb ? "" : " (ECHEC du retour)") +
                  C_RESET);
        });

        std::string line;
        std::getline(std::cin, line);
        if (!guard.fired()) {
            guard.confirm();
            if (line == "O" || line == "o" || line == "y" || line == "Y") {
                outln(std::string(C_GREEN) + "  Conserve." + C_RESET);
            } else {
                Result rb = ctl->set_power_limit_pct(0, before);
                outln(std::format("  Non confirme : retour a {:.0f} %{}", before,
                                  rb ? "" : " (ECHEC)"));
            }
        }
    }

    outln();
    outln(std::string(C_DIM) +
          "  Ce reglage est volatile : il disparait au redemarrage et au rechargement du "
          "pilote." + C_RESET);
    outln();
    return 0;
}

// ---------------------------------------------------------------------------
// Decalages d'horloge GPU. Volatile, comme la limite de puissance.
int cmd_gpu_clock(const Args& a) {
    outln();
    auto ctl = open_gpu();
    if (!ctl) return 1;

    if (!ctl->capabilities().set_clock_offset) {
        outln(std::string(C_RED) +
              std::format("  Decalages d'horloge non pilotables par {} sur cette carte.",
                          ctl->backend_name()) +
              C_RESET);
        return 1;
    }

    const auto& g = ctl->gpu(0);
    if (!g.offsets.valid) {
        outln(std::string(C_RED) + "  Decalages non lisibles : ecriture refusee." + C_RESET);
        return 1;
    }

    const int32_t core_before = g.offsets.graphics_delta_khz / 1000;
    const int32_t mem_before  = g.offsets.memory_delta_khz / 1000;
    outln(std::format("  {}", g.name));
    outln(std::format("    graphique {:+} MHz (plage {} a {})", core_before,
                      g.offsets.graphics_min_khz / 1000, g.offsets.graphics_max_khz / 1000));
    outln(std::format("    memoire   {:+} MHz (plage {} a {})", mem_before,
                      g.offsets.memory_min_khz / 1000, g.offsets.memory_max_khz / 1000));

    // --- Analyse des arguments ---------------------------------------------
    bool has_core = false, has_mem = false, reset = false;
    int32_t core = 0, mem = 0;
    for (size_t i = 0; i < a.raw.size(); ++i) {
        if (a.raw[i] == "--reset") { reset = true; }
        else if (a.raw[i] == "--core" && i + 1 < a.raw.size()) {
            core = std::atoi(a.raw[i + 1].c_str());
            has_core = true;
        } else if (a.raw[i] == "--mem" && i + 1 < a.raw.size()) {
            mem = std::atoi(a.raw[i + 1].c_str());
            has_mem = true;
        }
    }
    if (reset) { core = 0; mem = 0; has_core = true; has_mem = true; }
    if (!has_core && !has_mem) {
        outln();
        outln("  Usage : tuneforge gpu-clock [--core <MHz>] [--mem <MHz>] [--watchdog N] [--yes]");
        outln("          tuneforge gpu-clock --reset");
        outln();
        return 2;
    }

    outln();
    if (has_core) outln(std::format("  Cible graphique : {:+} MHz", core));
    if (has_mem)  outln(std::format("  Cible memoire   : {:+} MHz", mem));

    if (a.dry_run) {
        outln(std::string(C_YELLOW) + "  MODE SIMULATION — rien n'a ete ecrit" + C_RESET);
        outln();
        return 0;
    }

    auto apply = [&](bool with_core, int32_t c, bool with_mem, int32_t m) -> bool {
        bool ok = true;
        if (with_core) {
            Result r = ctl->set_clock_offset_mhz(0, hw::GpuClockDomain::Graphics, c);
            if (!r) { outln(std::string(C_RED) + "  Graphique : " + r.message + C_RESET); ok = false; }
            else outln(std::string(C_GREEN) + std::format("  Graphique : {:+} MHz", c) + C_RESET);
        }
        if (with_mem) {
            Result r = ctl->set_clock_offset_mhz(0, hw::GpuClockDomain::Memory, m);
            if (!r) { outln(std::string(C_RED) + "  Memoire : " + r.message + C_RESET); ok = false; }
            else outln(std::string(C_GREEN) + std::format("  Memoire   : {:+} MHz", m) + C_RESET);
        }
        return ok;
    };

    if (!apply(has_core, core, has_mem, mem)) {
        outln();
        return 1;
    }

    const int wd = a.watchdog > 0 ? a.watchdog : 15;
    if (!a.yes) {
        outln();
        outln(std::format("{}  Chien de garde : {} secondes pour confirmer.{}", C_YELLOW, wd,
                          C_RESET));
        outln("  Tapez O puis Entree pour conserver, ou ne faites rien pour revenir en arriere.");

        Watchdog guard(wd, [&]() {
            apply(has_core, core_before, has_mem, mem_before);
            outln();
            outln(std::string(C_RED) + "  Delai ecoule : decalages restaures." + C_RESET);
        });

        std::string line;
        std::getline(std::cin, line);
        if (!guard.fired()) {
            guard.confirm();
            if (line == "O" || line == "o" || line == "y" || line == "Y") {
                outln(std::string(C_GREEN) + "  Conserve." + C_RESET);
            } else {
                outln("  Non confirme : restauration.");
                apply(has_core, core_before, has_mem, mem_before);
            }
        }
    }

    outln();
    outln(std::string(C_DIM) +
          "  Reglage volatile : il disparait au redemarrage et au rechargement du pilote." +
          C_RESET);
    outln();
    return 0;
}

// ---------------------------------------------------------------------------
// Controle des ventilateurs. Volatile, avec retour automatique au mode
// automatique si l'utilisateur ne confirme pas.
int cmd_gpu_fan(const Args& a) {
    outln();
    auto ctl = open_gpu();
    if (!ctl) return 1;

    if (!ctl->capabilities().set_fan) {
        outln(std::string(C_RED) +
              std::format("  Ventilateurs non pilotables par {} sur cette carte.",
                          ctl->backend_name()) +
              C_RESET);
        return 1;
    }

    const auto& g = ctl->gpu(0);
    outln(std::format("  {}", g.name));
    for (const auto& c : g.coolers.items) {
        outln(std::format("    Ventilateur {}  niveau {} %, {}", c.index, c.current_level,
                          c.active ? "actif" : "arrete"));
    }
    if (g.coolers.tach_valid) outln(std::format("    Tachymetre     {} tr/min", g.coolers.tach_rpm));

    bool     automatic = false;
    uint32_t level = 0;
    for (const auto& s : a.raw) {
        if (s == "--auto") automatic = true;
    }
    if (!automatic && !a.positional.empty()) {
        level = static_cast<uint32_t>(std::atoi(a.positional.front().c_str()));
    } else if (!automatic) {
        outln();
        outln("  Usage : tuneforge gpu-fan <pourcentage>  (30 a 100)");
        outln("          tuneforge gpu-fan --auto");
        outln();
        return 2;
    }

    outln();
    outln(automatic ? "  Cible : mode automatique"
                    : std::format("  Cible : {} % en manuel", level));

    if (a.dry_run) {
        outln(std::string(C_YELLOW) + "  MODE SIMULATION — rien n'a ete ecrit" + C_RESET);
        outln();
        return 0;
    }

    Result r = automatic ? ctl->set_fan_auto(0) : ctl->set_fan_level_pct(0, level);
    if (!r) {
        outln(std::string(C_RED) + "  Echec : " + r.message + C_RESET);
        // En cas de doute sur la disposition, on rend la main au pilote.
        if (!automatic) ctl->set_fan_auto(0);
        outln();
        return 1;
    }
    outln(std::string(C_GREEN) + "  Applique." + C_RESET);
    for (const auto& c : ctl->gpu(0).coolers.items) {
        outln(std::format("    Ventilateur {}  niveau {} %, {}", c.index, c.current_level,
                          c.active ? "actif" : "arrete"));
    }
    if (ctl->gpu(0).coolers.tach_valid) {
        outln(std::format("    Tachymetre     {} tr/min", ctl->gpu(0).coolers.tach_rpm));
    }

    if (!automatic && !a.yes) {
        const int wd = a.watchdog > 0 ? a.watchdog : 15;
        outln();
        outln(std::format("{}  Chien de garde : {} secondes pour confirmer.{}", C_YELLOW, wd,
                          C_RESET));
        outln("  Tapez O puis Entree pour conserver, sinon retour au mode automatique.");

        Watchdog guard(wd, [&ctl]() {
            ctl->set_fan_auto(0);
            outln();
            outln(std::string(C_RED) + "  Delai ecoule : retour au mode automatique." + C_RESET);
        });
        std::string line;
        std::getline(std::cin, line);
        if (!guard.fired()) {
            guard.confirm();
            if (line == "O" || line == "o" || line == "y" || line == "Y") {
                outln(std::string(C_GREEN) + "  Conserve." + C_RESET);
            } else {
                ctl->set_fan_auto(0);
                outln("  Non confirme : retour au mode automatique.");
            }
        }
    }
    outln();
    outln(std::string(C_DIM) +
          "  Reglage volatile : il disparait au redemarrage et au rechargement du pilote." +
          C_RESET);
    outln();
    return 0;
}

// ---------------------------------------------------------------------------
double max_reading(const hw::HwInfoSensors& s, std::string_view label, hw::ReadingType type) {
    double best = 0.0;
    bool   found = false;
    for (const auto& r : s.readings()) {
        if (r.type != type) continue;
        if (lower(r.label).find(lower(label)) == std::string::npos) continue;
        if (!found || r.value > best) { best = r.value; found = true; }
    }
    return found ? best : -1.0;
}

int cmd_monitor(Engine& e, const Args& a) {
    ::SetConsoleCtrlHandler(ctrl_handler, TRUE);

    hw::SystemCounters counters;
    hw::HwInfoSensors  sensors;
    hw::Nvml           nvml;

    const bool has_hwinfo = sensors.open();
    const bool has_nvml   = nvml.init();
    const hw::Profile& p  = e.hardware();

    counters.sample();  // amorce le calcul de charge CPU

    const int interval = (a.interval_ms < 200) ? 200 : a.interval_ms;

    while (!g_stop) {
        ::Sleep(static_cast<DWORD>(interval));
        if (g_stop) break;

        hw::SystemSnapshot s = counters.sample();
        if (has_hwinfo) sensors.refresh();

        std::string screen;
        screen += "\x1b[H\x1b[2J";
        screen += std::format("{}Tuneforge monitor{}   {}   (Ctrl+C pour quitter)\n\n",
                              C_BOLD, C_RESET, timestamp_iso());

        screen += std::format("{}PROCESSEUR{}  {}\n", C_CYAN, C_RESET, p.cpu.brand);
        screen += std::format("  Charge          {:6.1f} %\n", s.cpu_load_pct);
        if (has_hwinfo) {
            double t = max_reading(sensors, "Tctl", hw::ReadingType::Temperature);
            if (t < 0) t = max_reading(sensors, "CPU", hw::ReadingType::Temperature);
            double w = max_reading(sensors, "CPU Package Power", hw::ReadingType::Power);
            if (w < 0) w = max_reading(sensors, "CPU PPT", hw::ReadingType::Power);
            double c = max_reading(sensors, "Core Clock", hw::ReadingType::Clock);
            if (t >= 0) screen += std::format("  Temperature     {:6.1f} degC\n", t);
            if (w >= 0) screen += std::format("  Puissance       {:6.1f} W\n", w);
            if (c >= 0) screen += std::format("  Frequence max   {:6.0f} MHz\n", c);
        }

        screen += std::format("\n{}MEMOIRE{}\n", C_CYAN, C_RESET);
        screen += std::format("  Utilisee        {:6.1f} Go / {:.1f} Go  ({:.0f} %)\n",
                              s.ram_used_bytes / 1073741824.0,
                              s.ram_total_bytes / 1073741824.0, s.ram_pct);

        if (has_nvml) {
            for (uint32_t i = 0; i < nvml.device_count(); ++i) {
                auto g = nvml.sample(i);
                if (!g) continue;
                screen += std::format("\n{}GPU{}  {}\n", C_CYAN, C_RESET, g->name);
                screen += std::format("  Charge          {:6} %\n", g->utilization_pct);
                screen += std::format("  Temperature     {:6} degC\n", g->temperature_c);
                screen += std::format("  Puissance       {:6.1f} W\n", g->power_mw / 1000.0);
                screen += std::format("  Frequence GPU   {:6} MHz\n", g->clock_graphics_mhz);
                screen += std::format("  Frequence VRAM  {:6} MHz\n", g->clock_memory_mhz);
                if (g->fan_pct) screen += std::format("  Ventilateur     {:6} %\n", g->fan_pct);
                if (g->vram_total_bytes) {
                    screen += std::format("  VRAM            {:6.2f} Go / {:.2f} Go\n",
                                          g->vram_used_bytes / 1073741824.0,
                                          g->vram_total_bytes / 1073741824.0);
                }
            }
        }

        screen += std::format("\n{}SOURCES{}  compteurs Windows{}{}\n", C_DIM, C_RESET,
                              has_nvml ? ", NVML" : "",
                              has_hwinfo ? ", memoire partagee HWiNFO" : "");
        if (!has_hwinfo) {
            screen += std::string(C_DIM) +
                      "  Lancez HWiNFO64 (Settings > Shared Memory Support) pour les "
                      "temperatures et puissances CPU.\n" + C_RESET;
        }
        out(screen);
        std::fflush(stdout);
    }
    outln();
    return 0;
}

bool command_writes(const std::string& c) {
    // gpu-power inclus : le pilote refuse l'ecriture avec
    // NVAPI_INVALID_USER_PRIVILEGE tant qu'on n'est pas administrateur.
    return c == "apply" || c == "revert" || c == "recover" || c == "gpu-power" ||
           c == "gpu-clock" || c == "gpu-fan";
}

} // namespace

int main(int argc, char** argv) {
    init_colors();
    log_init(data_dir() + L"\\tuneforge.log");
    log_set_console_level(LogLevel::Warn);

    Args a = parse_args(argc, argv);
    if (a.command.empty() || a.command == "help" || a.command == "--help") {
        print_help();
        return 0;
    }
    if (a.command == "version" || a.command == "--version") {
        outln(std::string("Tuneforge ") + TF_VERSION);
        return 0;
    }

    // Une commande mal formee doit echouer AVANT l'elevation : sinon
    // l'utilisateur valide une invite UAC pour se voir repondre qu'il a oublie
    // un argument.
    if ((a.command == "apply" || a.command == "revert") && a.positional.empty() &&
        !a.all) {
        outln();
        outln(std::string(C_RED) + "  Precisez un ou plusieurs identifiants de reglage." +
              C_RESET);
        if (a.command == "apply") {
            outln("  tuneforge list           pour le catalogue complet");
        } else {
            outln("  tuneforge status         pour ce qui est restaurable");
            outln("  tuneforge revert --all   pour tout restaurer");
        }
        outln();
        return 2;
    }

    // Les commandes qui ecrivent ont besoin de l'elevation. On relance plutot
    // que d'echouer a mi-parcours sur un ERROR_ACCESS_DENIED.
    if (command_writes(a.command) && !is_elevated() && !a.no_elevate) {
        outln();
        outln(std::string(C_YELLOW) +
              "  Cette commande modifie le systeme : relance en administrateur..." + C_RESET);
        std::vector<std::string> args = a.raw;
        args.push_back("--no-elevate");
        args.push_back("--pause");
        if (relaunch_elevated(args, true)) return 0;
        outln(std::string(C_RED) +
              "  Elevation refusee. Relancez ce terminal en administrateur." + C_RESET);
        return 3;
    }

    SingleInstance guard(L"Global\\TuneforgeApplyLock");
    if (command_writes(a.command) && !guard.acquired()) {
        outln(std::string(C_RED) +
              "  Une autre instance de Tuneforge applique deja des reglages." + C_RESET);
        return 4;
    }

    Engine engine;

    // Un lot interrompu est signale a chaque demarrage, quelle que soit la commande.
    if (DirtyFlag::exists() && a.command != "recover") {
        outln();
        outln(std::string(C_RED) + "  Attention : un lot d'application a ete interrompu." +
              C_RESET);
        outln("  Lancez « tuneforge recover » pour revenir a l'etat d'avant.");
    }

    // Un outil qui touche au systeme ne doit jamais mourir sur une exception
    // silencieuse : l'utilisateur doit repartir avec un message exploitable.
    int rc = 0;
    try {
        if      (a.command == "detect")   rc = cmd_detect(engine);
        else if (a.command == "list")     rc = cmd_list(engine, a);
        else if (a.command == "status")   rc = cmd_status(engine);
        else if (a.command == "apply")    rc = cmd_apply(engine, a);
        else if (a.command == "revert")   rc = cmd_revert(engine, a);
        else if (a.command == "recover")  rc = cmd_recover(engine);
        else if (a.command == "report")   rc = cmd_report(engine, a);
        else if (a.command == "monitor")  rc = cmd_monitor(engine, a);
        else if (a.command == "gpu")       rc = cmd_gpu(a);
        else if (a.command == "gpu-power") rc = cmd_gpu_power(a);
        else if (a.command == "gpu-clock") rc = cmd_gpu_clock(a);
        else if (a.command == "gpu-fan")   rc = cmd_gpu_fan(a);
        else {
            outln(std::string(C_RED) + "  Commande inconnue : " + a.command + C_RESET);
            print_help();
            rc = 2;
        }
    } catch (const std::exception& ex) {
        log_error("exception non rattrapee dans « {} » : {}", a.command, ex.what());
        outln();
        outln(std::string(C_RED) + "  Erreur interne : " + ex.what() + C_RESET);
        outln("  Journal : " + narrow(data_dir() + L"\\tuneforge.log"));
        outln("  Aucun reglage n'a ete laisse dans un etat intermediaire "
              "(voir « tuneforge status »).");
        rc = 70;
    } catch (...) {
        log_error("exception inconnue dans « {} »", a.command);
        outln();
        outln(std::string(C_RED) + "  Erreur interne inconnue." + C_RESET);
        rc = 70;
    }

    if (a.pause_at_exit) {
        outln();
        out("  Appuyez sur Entree pour fermer...");
        std::string dummy;
        std::getline(std::cin, dummy);
    }
    log_close();
    return rc;
}
