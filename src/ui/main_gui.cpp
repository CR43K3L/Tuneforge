//
// Point d'entree de l'interface graphique.
//
// Le coeur (tfcore) est strictement identique a celui de la ligne de commande :
// cette couche n'ajoute aucune logique, elle ne fait que presenter.
//
#include <windows.h>
#include <shellapi.h>

#include "common/util.hpp"
#include "imgui.h"
#include "ui/app.hpp"
#include "ui/theme.hpp"
#include "ui/window.hpp"

using namespace tf;

namespace {

// --page <nom> : ouvre directement une section. Pratique pour un raccourci
// pointant sur les reglages, et pour verifier une page sans cliquer.
std::optional<ui::Page> page_from_args() {
    int     argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv) return std::nullopt;

    std::optional<ui::Page> out;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) != L"--page" || i + 1 >= argc) continue;
        const std::string name = lower(narrow(argv[++i]));
        if      (name == "dashboard" || name == "tableau")  out = ui::Page::Dashboard;
        else if (name == "reglages"  || name == "tweaks")   out = ui::Page::Tweaks;
        else if (name == "restauration" || name == "restore") out = ui::Page::Restore;
        else if (name == "gpu")                             out = ui::Page::Gpu;
        else if (name == "materiel"  || name == "hardware") out = ui::Page::Hardware;
        else if (name == "parametres" || name == "settings" || name == "journal")
            out = ui::Page::Settings;
    }
    ::LocalFree(argv);
    return out;
}

// --page quiz : demarre le questionnaire des l'ouverture.
bool wants_quiz() {
    int     argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv) return false;
    bool found = false;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::wstring(argv[i]) == L"--page" && lower(narrow(argv[i + 1])) == "quiz") {
            found = true;
        }
    }
    ::LocalFree(argv);
    return found;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    log_init(data_dir() + L"\\tuneforge.log");
    log_info("interface graphique demarree (version {})", TF_VERSION);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Preferences d'interface avant toute mise en style : le theme et
    // l'echelle doivent etre connus au premier chargement des polices.
    ui::App::LoadUiPrefsStatic();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // pas de imgui.ini a cote du binaire
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    ui::Window window;
    // Taille par defaut generreuse : l'echelle d'interface part a 120 %, une
    // fenetre plus etroite obligerait a faire defiler des la premiere page.
    if (!window.Create(L"Tuneforge", 1320, 880)) {
        ::MessageBoxW(nullptr,
                      L"Impossible de creer la fenetre Direct3D 11.\n"
                      L"Consultez %LOCALAPPDATA%\\Tuneforge\\tuneforge.log.",
                      L"Tuneforge", MB_ICONERROR | MB_OK);
        ImGui::DestroyContext();
        return 1;
    }

    float dpi = window.dpi_scale();
    ui::LoadFonts(dpi);
    ui::ApplyStyle(dpi);

    // Portee explicite : le destructeur de App rend les ventilateurs au
    // pilote et le journalise. Laisser l objet vivre jusqu a la fin de
    // wWinMain le ferait travailler apres log_close().
    {
        ui::App app;
        app.Init(&window);
        if (auto p = page_from_args()) app.SetPage(*p);
        if (wants_quiz()) app.StartQuiz();

        while (window.PumpEvents()) {
            if (window.ConsumeDpiChanged()) {
                dpi = window.dpi_scale();
                ui::LoadFonts(dpi);
                ui::ApplyStyle(dpi);
                log_debug("echelle DPI mise a jour : {:.2f}", dpi);
            }
            window.BeginFrame();
            app.Frame();
            window.EndFrame(ui::P().bg);

            // Le rechargement des polices doit se faire hors image : c'est pour
            // cela que la demande est mise en attente plutot qu'appliquee au clic.
            if (app.ConsumeStyleReload()) {
                ui::LoadFonts(dpi);
                ui::ApplyStyle(dpi);
                log_debug("echelle d'interface : {:.0f} %", ui::UiScale() * 100.0f);
            }
        }
    }

    window.Destroy();
    ImGui::DestroyContext();
    log_info("interface graphique fermee");
    log_close();
    return 0;
}
