#include "ui/theme.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "common/util.hpp"

namespace tf::ui {
namespace {

Palette g_palette;
Metrics g_metrics;
Fonts   g_fonts;

// 105 % : les jetons typographiques ont ete remontes a la source, inutile de
// gonfler tout le reste par-dessus.
float   g_ui_scale = 1.05f;
ThemeId g_theme = ThemeId::SlateCyan;

// Fabrique la palette d'un theme. Seules les surfaces et l'accent varient.
Palette make_palette(ThemeId id) {
    Palette p;  // valeurs par defaut = Ardoise & cyan
    switch (id) {
        case ThemeId::InkViolet:
            p.sunken = Hex(0x09080F);  p.bg = Hex(0x0C0B14);
            p.surface1 = Hex(0x15131F); p.surface2 = Hex(0x1B1929);
            p.surface3 = Hex(0x232134); p.sidebar = Hex(0x0E0D18);
            p.border_subtle = Hex(0x262338); p.border_strong = Hex(0x342F4A);
            p.text = Hex(0xEDEBF5); p.text_secondary = Hex(0xA29DB8);
            p.text_muted = Hex(0x8A85A0); p.text_disabled = Hex(0x545070);
            p.accent = Hex(0x8B5CF6); p.accent_hover = Hex(0xA78BFA);
            p.accent_active = Hex(0x7C3AED);
            break;

        case ThemeId::OledSteel:
            p.sunken = Hex(0x000000);  p.bg = Hex(0x000000);
            p.surface1 = Hex(0x0C0C0E); p.surface2 = Hex(0x141417);
            p.surface3 = Hex(0x1C1C20); p.sidebar = Hex(0x060607);
            p.border_subtle = Hex(0x1A1A1E); p.border_strong = Hex(0x2A2A30);
            p.text = Hex(0xFAFAFA); p.text_secondary = Hex(0xA1A1AA);
            p.text_muted = Hex(0x71717A); p.text_disabled = Hex(0x3F3F46);
            p.accent = Hex(0x60A5FA); p.accent_hover = Hex(0x93C5FD);
            p.accent_active = Hex(0x3B82F6);
            break;

        case ThemeId::SlateMagenta:
            p.accent = Hex(0xE879F9); p.accent_hover = Hex(0xF0ABFC);
            p.accent_active = Hex(0xD946EF);
            break;

        case ThemeId::SlateCyan:
        default:
            break;
    }
    // Derivees : recalculees pour qu'aucun theme n'ait a les redefinir.
    p.accent_subtle = WithAlpha(p.accent, 0.14f);
    p.accent_border = WithAlpha(p.accent, 0.38f);
    p.glow          = WithAlpha(p.accent, 0.30f);
    // La surface surelevee est la surface de carte eclaircie d'un cran, pas
    // une couleur inventee : elle reste juste quel que soit le theme.
    p.surface_raised = Mix(p.surface1, p.surface2, 0.55f);
    return p;
}

// Plage de glyphes : Latin-1 pour le francais, plus la ponctuation typographique
// (tirets cadratins, guillemets), les fleches et les formes geometriques dont
// l'interface se sert comme pastilles.
const ImWchar* glyph_ranges() {
    static const ImWchar ranges[] = {
        0x0020, 0x00FF,  // latin de base + supplement latin-1
        0x2010, 0x2027,  // tirets, guillemets, points de suspension
        0x2032, 0x2033,  // primes
        0x2190, 0x21FF,  // fleches
        0x2500, 0x257F,  // filets
        0x25A0, 0x25FF,  // formes geometriques (carres, cercles, triangles)
        0x2713, 0x2716,  // coches et croix
        0,
    };
    return ranges;
}

// Charge la premiere police trouvee parmi les candidates.
ImFont* load_first(const char* const* candidates, int count, float size,
                   const ImFontConfig* cfg) {
    ImGuiIO& io = ImGui::GetIO();
    char     path[MAX_PATH];
    for (int i = 0; i < count; ++i) {
        std::snprintf(path, sizeof(path), "C:\\Windows\\Fonts\\%s", candidates[i]);
        if (!file_exists(widen(path))) continue;
        if (ImFont* f = io.Fonts->AddFontFromFileTTF(path, size, cfg, glyph_ranges())) {
            log_debug("police chargee : {}", candidates[i]);
            return f;
        }
    }
    return nullptr;
}

} // namespace

namespace {
const Motion g_motion;
}  // namespace

const Motion& MO() { return g_motion; }

// Courbes d'interpolation. Toutes prennent et rendent une progression [0,1].
float EaseOutCubic(float t) {
    const float u = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - u * u * u;
}

float EaseInOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}

// Depassement leger puis retour. Reserve a ce qui doit sembler avoir du
// ressort — une coche qui apparait, une pastille qui se pose. Jamais sur une
// mesure : un chiffre qui depasse sa valeur avant d'y revenir est un mensonge.
float EaseOutBack(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

float EaseOutExpo(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
}

ImU32  U32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

ImVec4 WithAlpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

const Palette& P() { return g_palette; }
const Metrics& M() { return g_metrics; }
const Fonts&   F() { return g_fonts; }

void SetTheme(ThemeId id) {
    if (id >= ThemeId::Count) id = ThemeId::SlateCyan;
    g_theme = id;
    g_palette = make_palette(id);
}

ThemeId CurrentTheme() { return g_theme; }

const char* ThemeName(ThemeId id) {
    switch (id) {
        case ThemeId::SlateCyan:    return "Ardoise & cyan";
        case ThemeId::InkViolet:    return "Encre & violet";
        case ThemeId::OledSteel:    return "OLED & acier";
        case ThemeId::SlateMagenta: return "Ardoise & magenta";
        default:                    return "";
    }
}

const char* ThemeDescription(ThemeId id) {
    switch (id) {
        case ThemeId::SlateCyan:
            return "Bleus desatures, accent froid. Le plus lisible sur de longues sessions.";
        case ThemeId::InkViolet:
            return "Presque noir neutre, accent violet. Plus de caractere.";
        case ThemeId::OledSteel:
            return "Fond noir pur. Superbe sur dalle OLED, plus dur sur IPS.";
        case ThemeId::SlateMagenta:
            return "Memes surfaces que le theme par defaut, accent magenta.";
        default:
            return "";
    }
}

ImVec4 ThemeAccent(ThemeId id) { return make_palette(id).accent; }
ImVec4 ThemeSurface(ThemeId id) { return make_palette(id).surface1; }

void SetUiScale(float scale) {
    g_ui_scale = (std::max)(kUiScaleMin, (std::min)(kUiScaleMax, scale));
}
float UiScale() { return g_ui_scale; }

ImVec4 StatusColor(Status s) {
    switch (s) {
        case Status::Ok:     return g_palette.ok;
        case Status::Warn:   return g_palette.warn;
        case Status::Danger: return g_palette.danger;
        case Status::Accent: return g_palette.accent;
        default:             return g_palette.text_muted;
    }
}

ImVec4 StatusSubtle(Status s) {
    switch (s) {
        case Status::Ok:     return g_palette.ok_subtle;
        case Status::Warn:   return g_palette.warn_subtle;
        case Status::Danger: return g_palette.danger_subtle;
        case Status::Accent: return g_palette.accent_subtle;
        default:             return WithAlpha(g_palette.text_muted, 0.12f);
    }
}

// ---------------------------------------------------------------------------
void LoadFonts(float dpi_scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // Meme facteur que les gabarits : polices et espacements grandissent ensemble.
    const float base = g_metrics.font_body * dpi_scale * g_ui_scale;

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = false;

    static const char* regular[]  = {"segoeui.ttf", "SegUIVar.ttf", "arial.ttf"};
    static const char* semibold[] = {"seguisb.ttf", "segoeuib.ttf", "arialbd.ttf"};
    static const char* mono[]     = {"CascadiaMono.ttf", "consola.ttf", "cour.ttf"};

    g_fonts.regular  = load_first(regular, IM_ARRAYSIZE(regular), base, &cfg);
    g_fonts.semibold = load_first(semibold, IM_ARRAYSIZE(semibold), base, &cfg);
    g_fonts.mono     = load_first(mono, IM_ARRAYSIZE(mono), base * 0.92f, &cfg);

    if (!g_fonts.regular) {
        log_warn("aucune police systeme trouvee : repli sur la police integree");
        g_fonts.regular = io.Fonts->AddFontDefault();
    }
    if (!g_fonts.semibold) g_fonts.semibold = g_fonts.regular;
    if (!g_fonts.mono)     g_fonts.mono     = g_fonts.regular;

    io.FontDefault = g_fonts.regular;
}

// ---------------------------------------------------------------------------
void ApplyStyle(float dpi_scale) {
    // Les jetons de dimension sont mis a l'echelle une seule fois, ici.
    Metrics m;
    const float s = dpi_scale * g_ui_scale;
    m.sp_xs = 4 * s;   m.sp_sm = 8 * s;   m.sp_md = 12 * s;
    m.sp_lg = 16 * s;  m.sp_xl = 24 * s;  m.sp_2xl = 32 * s;
    m.r_sm = 4 * s;    m.r_md = 7 * s;    m.r_lg = 11 * s;   m.r_pill = 999.0f;
    m.font_display = 22 * s;  m.font_h1 = 17.5f * s;  m.font_title = 15 * s;
    m.font_body = 15 * s;     m.font_small = 13 * s;  m.font_micro = 11.5f * s;
    m.sidebar_w = 208 * s;    m.titlebar_h = 38 * s;  m.nav_h = 34 * s;
    m.row_h = 32 * s;         m.border = 1.0f;
    m.scale = s;
    g_metrics = m;

    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();  // repart d'une base propre

    const Palette& p = g_palette;

    // --- Geometrie ---------------------------------------------------------
    // Marge interieure des cartes : sp_lg suffit, sp_xl gonflait chaque bloc
    // de seize pixels pour rien.
    st.WindowPadding     = ImVec2(m.sp_lg, m.sp_lg);
    st.FramePadding      = ImVec2(m.sp_md, m.sp_sm);
    st.CellPadding       = ImVec2(m.sp_md, m.sp_sm);
    st.ItemSpacing       = ImVec2(m.sp_md, m.sp_sm);
    st.ItemInnerSpacing  = ImVec2(m.sp_sm, m.sp_xs);
    st.IndentSpacing     = m.sp_xl;
    st.ScrollbarSize     = 10 * s;
    st.GrabMinSize       = 12 * s;

    st.WindowBorderSize  = 0;
    st.ChildBorderSize   = m.border;
    st.PopupBorderSize   = m.border;
    st.FrameBorderSize   = m.border;
    st.TabBorderSize     = 0;
    st.SeparatorTextBorderSize = m.border;

    st.WindowRounding    = 0;
    st.ChildRounding     = m.r_lg;
    st.FrameRounding     = m.r_md;
    st.PopupRounding     = m.r_md;
    st.ScrollbarRounding = m.r_pill;
    st.GrabRounding      = m.r_pill;
    st.TabRounding       = m.r_sm;

    st.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    st.ButtonTextAlign   = ImVec2(0.5f, 0.5f);
    st.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    st.SeparatorTextAlign  = ImVec2(0.0f, 0.5f);
    st.SeparatorTextPadding = ImVec2(0, m.sp_sm);

    st.AntiAliasedLines       = true;
    st.AntiAliasedLinesUseTex = true;
    st.AntiAliasedFill        = true;
    st.CurveTessellationTol   = 1.0f;

    // --- Couleurs ----------------------------------------------------------
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                 = p.text;
    c[ImGuiCol_TextDisabled]         = p.text_disabled;
    c[ImGuiCol_WindowBg]             = p.bg;
    c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]              = p.surface2;
    c[ImGuiCol_Border]               = p.border_subtle;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]              = p.sunken;
    c[ImGuiCol_FrameBgHovered]       = p.surface2;
    c[ImGuiCol_FrameBgActive]        = p.surface3;

    c[ImGuiCol_TitleBg]              = p.bg;
    c[ImGuiCol_TitleBgActive]        = p.bg;
    c[ImGuiCol_TitleBgCollapsed]     = p.bg;
    c[ImGuiCol_MenuBarBg]            = p.sidebar;

    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = WithAlpha(p.text_muted, 0.22f);
    c[ImGuiCol_ScrollbarGrabHovered] = WithAlpha(p.text_muted, 0.36f);
    c[ImGuiCol_ScrollbarGrabActive]  = WithAlpha(p.accent, 0.55f);

    c[ImGuiCol_CheckMark]            = p.accent;
    c[ImGuiCol_SliderGrab]           = p.accent;
    c[ImGuiCol_SliderGrabActive]     = p.accent_hover;

    c[ImGuiCol_Button]               = p.surface2;
    c[ImGuiCol_ButtonHovered]        = p.surface3;
    c[ImGuiCol_ButtonActive]         = Mix(p.surface3, p.accent, 0.18f);

    c[ImGuiCol_Header]               = p.accent_subtle;
    c[ImGuiCol_HeaderHovered]        = WithAlpha(p.accent, 0.20f);
    c[ImGuiCol_HeaderActive]         = WithAlpha(p.accent, 0.28f);

    c[ImGuiCol_Separator]            = p.border_subtle;
    c[ImGuiCol_SeparatorHovered]     = p.border_strong;
    c[ImGuiCol_SeparatorActive]      = p.accent_border;

    c[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered]    = WithAlpha(p.accent, 0.25f);
    c[ImGuiCol_ResizeGripActive]     = WithAlpha(p.accent, 0.45f);

    c[ImGuiCol_Tab]                  = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered]           = p.surface2;
    c[ImGuiCol_TabSelected]          = p.surface1;
    c[ImGuiCol_TabSelectedOverline]  = p.accent;
    c[ImGuiCol_TabDimmed]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabDimmedSelected]    = p.surface1;

    c[ImGuiCol_PlotLines]            = p.accent;
    c[ImGuiCol_PlotLinesHovered]     = p.accent_hover;
    c[ImGuiCol_PlotHistogram]        = p.accent;
    c[ImGuiCol_PlotHistogramHovered] = p.accent_hover;

    c[ImGuiCol_TableHeaderBg]        = p.surface1;
    c[ImGuiCol_TableBorderStrong]    = p.border_strong;
    c[ImGuiCol_TableBorderLight]     = p.border_subtle;
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = WithAlpha(p.surface1, 0.45f);

    c[ImGuiCol_TextSelectedBg]       = WithAlpha(p.accent, 0.30f);
    c[ImGuiCol_TextLink]             = p.accent;
    c[ImGuiCol_DragDropTarget]       = p.accent;
    c[ImGuiCol_NavCursor]            = p.accent_border;
    c[ImGuiCol_NavWindowingHighlight]= p.accent;
    c[ImGuiCol_NavWindowingDimBg]    = p.scrim;
    c[ImGuiCol_ModalWindowDimBg]     = p.scrim;
}

} // namespace tf::ui
