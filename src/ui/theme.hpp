#pragma once
//
// Systeme de design « Ardoise + cyan glacial ».
//
// Tout passe par des jetons nommes : aucune couleur ni aucun espacement n'est
// ecrit en dur ailleurs dans l'interface. Changer le theme se fait ici et
// nulle part ailleurs.
//
// Regle chromatique : l'accent de marque est cyan, ce qui laisse vert, ambre
// et rouge entierement disponibles pour les etats. Une couleur = un sens.
//
#include "imgui.h"

namespace tf::ui {

// 0xRRGGBB -> ImVec4, evaluable a la compilation.
constexpr ImVec4 Hex(unsigned rgb, float a = 1.0f) {
    return ImVec4(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                  static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                  static_cast<float>(rgb & 0xFF) / 255.0f, a);
}

ImU32  U32(const ImVec4& c);
ImVec4 WithAlpha(const ImVec4& c, float a);
ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t);

// ---------------------------------------------------------------------------
// Jetons de couleur
// ---------------------------------------------------------------------------
struct Palette {
    // Surfaces, du plus enfonce au plus eleve.
    ImVec4 sunken        = Hex(0x070A0E);  // champs de saisie, zones creuses
    ImVec4 bg            = Hex(0x0B0F14);  // fond de fenetre
    ImVec4 surface1      = Hex(0x121820);  // cartes, panneaux
    ImVec4 surface2      = Hex(0x17202B);  // survol
    ImVec4 surface3      = Hex(0x1D2833);  // actif, enfonce
    ImVec4 sidebar       = Hex(0x0D1219);  // barre laterale, legerement distincte

    ImVec4 border_subtle = Hex(0x1F2933);
    ImVec4 border_strong = Hex(0x2C3A47);

    // Texte : quatre niveaux, pas un de plus.
    ImVec4 text          = Hex(0xE6EDF3);
    ImVec4 text_secondary= Hex(0x9BA9B8);
    ImVec4 text_muted    = Hex(0x7D8998);
    ImVec4 text_disabled = Hex(0x4B5765);

    // Accent de marque.
    ImVec4 accent        = Hex(0x38BDF8);
    ImVec4 accent_hover  = Hex(0x7DD3FC);
    ImVec4 accent_active = Hex(0x0EA5E9);
    ImVec4 accent_subtle = Hex(0x38BDF8, 0.14f);
    ImVec4 accent_border = Hex(0x38BDF8, 0.38f);

    // Etats. Chacun a sa version « fond discret » pour les pastilles.
    ImVec4 ok            = Hex(0x34D399);
    ImVec4 ok_subtle     = Hex(0x34D399, 0.14f);
    ImVec4 warn          = Hex(0xFBBF24);
    ImVec4 warn_subtle   = Hex(0xFBBF24, 0.14f);
    ImVec4 danger        = Hex(0xF87171);
    ImVec4 danger_subtle = Hex(0xF87171, 0.14f);

    ImVec4 shadow        = Hex(0x000000, 0.55f);
    ImVec4 scrim         = Hex(0x05080B, 0.72f);  // voile des boites modales
};

// ---------------------------------------------------------------------------
// Jetons de dimension — deja multiplies par l'echelle DPI
// ---------------------------------------------------------------------------
struct Metrics {
    // Espacement sur une base de 4.
    float sp_xs = 4, sp_sm = 8, sp_md = 12, sp_lg = 16, sp_xl = 24, sp_2xl = 32;
    // Rayons.
    float r_sm = 4, r_md = 7, r_lg = 11, r_pill = 999;
    // Typographie.
    float font_display = 26, font_h1 = 21, font_title = 17.5f, font_body = 16.5f,
          font_small = 14.5f, font_micro = 13;
    // Gabarits.
    float sidebar_w = 228, titlebar_h = 42, nav_h = 38, row_h = 34, border = 1;
    float scale = 1.0f;
};

struct Fonts {
    ImFont* regular  = nullptr;
    ImFont* semibold = nullptr;
    ImFont* mono     = nullptr;
};

enum class Status { Neutral, Ok, Warn, Danger, Accent };

const Palette& P();
const Metrics& M();
const Fonts&   F();

// Couleur de premier plan et de fond associees a un etat.
ImVec4 StatusColor(Status s);
ImVec4 StatusSubtle(Status s);

// --- Themes ----------------------------------------------------------------
// Seules les surfaces et l'accent changent d'un theme a l'autre. Les couleurs
// d'etat (vert, ambre, rouge) restent identiques partout : une couleur porte un
// sens, elle ne suit pas la decoration. C'est aussi pour cela qu'aucun theme
// n'a un accent vert ou ambre — il entrerait en collision avec un statut.
enum class ThemeId { SlateCyan, InkViolet, OledSteel, SlateMagenta, Count };

void        SetTheme(ThemeId id);
ThemeId     CurrentTheme();
const char* ThemeName(ThemeId id);
const char* ThemeDescription(ThemeId id);
ImVec4      ThemeAccent(ThemeId id);
ImVec4      ThemeSurface(ThemeId id);

// --- Echelle d'interface ---------------------------------------------------
// Multiplicateur applique PAR-DESSUS l'echelle DPI, ajustable par l'utilisateur.
// Il agit sur tout — polices et gabarits — pour que les proportions du systeme
// de design restent intactes a n'importe quelle taille.
void  SetUiScale(float scale);
float UiScale();
inline constexpr float kUiScaleMin = 0.85f;
inline constexpr float kUiScaleMax = 1.60f;
inline constexpr float kUiScaleStep = 0.05f;

// Charge Segoe UI / Cascadia Mono depuis le systeme. Retombe sur la police
// integree d'ImGui si elles sont absentes (Windows N, images allegees).
void LoadFonts(float dpi_scale);
void ApplyStyle(float dpi_scale);

} // namespace tf::ui
