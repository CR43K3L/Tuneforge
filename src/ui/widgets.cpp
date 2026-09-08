#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui_internal.h"

namespace tf::ui {

// ===========================================================================
// Texte
// ===========================================================================
void TextAt(ImFont* font, float size, const ImVec4& color, const char* text) {
    ImGui::PushFont(font, size);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void Display(const char* text)   { TextAt(F().semibold, M().font_display, P().text, text); }
void H1(const char* text)        { TextAt(F().semibold, M().font_h1, P().text, text); }
void CardTitle(const char* text) { TextAt(F().semibold, M().font_title, P().text, text); }
void Body(const char* text)      { TextAt(F().regular, M().font_body, P().text, text); }
void Muted(const char* text)     { TextAt(F().regular, M().font_small, P().text_muted, text); }

void Small(const char* text, const ImVec4& color) {
    TextAt(F().regular, M().font_small, color, text);
}

void Mono(const char* text, const ImVec4& color) {
    TextAt(F().mono, M().font_small, color, text);
}

void SectionLabel(const char* text) {
    // Etiquette de groupe : petite, en capitales, tres discrete.
    std::string upper(text);
    for (char& ch : upper) {
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    }
    TextAt(F().semibold, M().font_micro, P().text_disabled, upper.c_str());
}

void WrappedMuted(const char* text, float wrap_width) {
    ImGui::PushFont(F().regular, M().font_small);
    ImGui::PushStyleColor(ImGuiCol_Text, P().text_muted);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// ===========================================================================
// Primitives
// ===========================================================================
void VSpace(float h) { ImGui::Dummy(ImVec2(0, h)); }

void Divider(float vertical_margin) {
    const float mg = vertical_margin < 0 ? M().sp_md : vertical_margin;
    VSpace(mg);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float  w = ImGui::GetContentRegionAvail().x;
    dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y), U32(P().border_subtle), M().border);
    VSpace(mg);
}

void Dot(Status s, float radius, bool filled) {
    const float r = radius < 0 ? 3.5f * M().scale : radius;
    ImDrawList*  dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float  line_h = ImGui::GetTextLineHeight();
    const ImVec2 c(p.x + r, p.y + line_h * 0.5f);

    if (filled) {
        // Halo tres leger : donne du relief sans etre un « glow » criard.
        dl->AddCircleFilled(c, r * 2.6f, U32(StatusSubtle(s)), 16);
        dl->AddCircleFilled(c, r, U32(StatusColor(s)), 16);
    } else {
        dl->AddCircle(c, r, U32(P().text_disabled), 16, 1.4f * M().scale);
    }
    ImGui::Dummy(ImVec2(r * 2, line_h));
}

void Badge(const char* text, const ImVec4& fg, const ImVec4& bg) {
    ImGui::PushFont(F().semibold, M().font_micro);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 pad(M().sp_sm, M().sp_xs * 0.75f);
    const ImVec2 size(ts.x + pad.x * 2, ts.y + pad.y * 2);
    const ImVec2 p = ImGui::GetCursorScreenPos();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + size, U32(bg), M().r_pill);
    dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), U32(fg), text);

    ImGui::Dummy(size);
    ImGui::PopFont();
}

void StatusPill(const char* text, Status s) {
    ImGui::PushFont(F().semibold, M().font_micro);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float  r  = 3.0f * M().scale;
    const ImVec2 pad(M().sp_sm, M().sp_xs * 0.75f);
    const ImVec2 size(ts.x + pad.x * 2 + r * 2 + M().sp_xs, ts.y + pad.y * 2);
    const ImVec2 p = ImGui::GetCursorScreenPos();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + size, U32(StatusSubtle(s)), M().r_pill);
    dl->AddCircleFilled(ImVec2(p.x + pad.x + r, p.y + size.y * 0.5f), r,
                        U32(StatusColor(s)), 12);
    dl->AddText(ImVec2(p.x + pad.x + r * 2 + M().sp_xs, p.y + pad.y),
                U32(StatusColor(s)), text);

    ImGui::Dummy(size);
    ImGui::PopFont();
}

// ===========================================================================
// Cartes
// ===========================================================================
bool BeginCard(const char* id, const ImVec2& size, bool interactive) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, P().surface1);
    ImGui::PushStyleColor(ImGuiCol_Border, P().border_subtle);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, M().r_lg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(M().sp_lg, M().sp_lg));

    ImGuiChildFlags flags = ImGuiChildFlags_Borders;
    if (size.y == 0.0f) flags |= ImGuiChildFlags_AutoResizeY;

    const bool open = ImGui::BeginChild(id, size, flags,
                                        interactive ? 0 : ImGuiWindowFlags_NoScrollbar);
    return open;
}

void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void CardHeader(const char* title, const char* subtitle) {
    CardTitle(title);
    if (subtitle && *subtitle) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - M().sp_xs * 0.5f);
        Muted(subtitle);
    }
    VSpace(M().sp_sm);
}

// ===========================================================================
// Navigation
// ===========================================================================
bool NavItem(const char* label, bool selected, Status indicator) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    const float  w   = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(w, M().nav_h);

    const ImGuiID id = win->GetID(label);
    ImGui::ItemSize(size);
    if (!ImGui::ItemAdd(ImRect(pos, pos + size), id)) return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(ImRect(pos, pos + size), id, &hovered, &held);

    // L'etat visuel est anime : pas de bascule brutale au survol.
    const float t = Animate(id, (selected ? 1.0f : (hovered ? 0.45f : 0.0f)));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (t > 0.01f) {
        dl->AddRectFilled(pos, pos + size,
                          U32(WithAlpha(P().accent, 0.10f * t)), M().r_md);
    }
    if (selected) {
        // Barre d'accent a gauche, marque de la section active.
        const float bar_h = size.y * 0.52f;
        const ImVec2 a(pos.x, pos.y + (size.y - bar_h) * 0.5f);
        dl->AddRectFilled(a, ImVec2(a.x + 2.5f * M().scale, a.y + bar_h),
                          U32(P().accent), M().r_pill);
    }

    const ImVec4 col = selected ? P().text : (hovered ? P().text_secondary : P().text_muted);
    ImGui::PushFont(selected ? F().semibold : F().regular, M().font_body);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(pos.x + M().sp_md, pos.y + (size.y - ts.y) * 0.5f), U32(col), label);
    ImGui::PopFont();

    if (indicator != Status::Neutral) {
        const float r = 3.0f * M().scale;
        dl->AddCircleFilled(ImVec2(pos.x + size.x - M().sp_md, pos.y + size.y * 0.5f), r,
                            U32(StatusColor(indicator)), 12);
    }
    return pressed;
}

// ===========================================================================
// Controles
// ===========================================================================
bool Toggle(const char* id, bool* value, bool enabled) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    const float  h = ImGui::GetTextLineHeight() * 1.15f;
    const float  w = h * 1.85f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    const ImGuiID gid = win->GetID(id);
    ImGui::ItemSize(ImVec2(w, h));
    if (!ImGui::ItemAdd(ImRect(pos, pos + ImVec2(w, h)), gid)) return false;

    bool hovered = false, held = false;
    bool pressed = false;
    if (enabled) {
        pressed = ImGui::ButtonBehavior(ImRect(pos, pos + ImVec2(w, h)), gid, &hovered, &held);
        if (pressed) *value = !*value;
    }

    const float t = Animate(gid, *value ? 1.0f : 0.0f, 18.0f);
    const float r = h * 0.5f;

    ImVec4 off = enabled ? P().surface3 : WithAlpha(P().surface3, 0.5f);
    ImVec4 on  = enabled ? P().accent : WithAlpha(P().accent, 0.4f);
    if (hovered && enabled) {
        off = Mix(off, P().border_strong, 0.6f);
        on  = Mix(on, P().accent_hover, 0.5f);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, pos + ImVec2(w, h), U32(Mix(off, on, t)), r);

    const float knob_r = r - 2.5f * M().scale;
    const float kx = pos.x + r + t * (w - r * 2);
    dl->AddCircleFilled(ImVec2(kx, pos.y + r), knob_r,
                        U32(enabled ? P().text : P().text_disabled), 20);
    return pressed;
}

int Stepper(const char* id, const char* value, bool can_decrease, bool can_increase,
            float width) {
    ImGui::PushID(id);

    const float  h = 34 * M().scale;
    const float  side = h;  // zones cliquables carrees
    const float  w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList*  dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(pos, pos + ImVec2(w, h), U32(P().sunken), M().r_md);
    dl->AddRect(pos, pos + ImVec2(w, h), U32(P().border_subtle), M().r_md, 0, M().border);

    // Trace un signe moins, et la barre verticale en plus si demande.
    auto glyph = [&](const ImVec2& c, bool plus, bool enabled, bool hovered) {
        const float  s = 5.5f * M().scale;
        const float  th = 1.6f * M().scale;
        const ImVec4 col = !enabled ? P().text_disabled
                                    : (hovered ? P().accent : P().text_secondary);
        dl->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y), U32(col), th);
        if (plus) dl->AddLine(ImVec2(c.x, c.y - s), ImVec2(c.x, c.y + s), U32(col), th);
    };

    int result = 0;

    // --- Zone moins --------------------------------------------------------
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##dec", ImVec2(side, h));
    const bool dec_hover = can_decrease && ImGui::IsItemHovered();
    if (can_decrease && ImGui::IsItemClicked()) result = -1;
    if (dec_hover) {
        dl->AddRectFilled(pos, pos + ImVec2(side, h), U32(WithAlpha(P().accent, 0.10f)),
                          M().r_md, ImDrawFlags_RoundCornersLeft);
    }
    glyph(ImVec2(pos.x + side * 0.5f, pos.y + h * 0.5f), false, can_decrease, dec_hover);

    // --- Zone plus ---------------------------------------------------------
    const ImVec2 inc_pos(pos.x + w - side, pos.y);
    ImGui::SetCursorScreenPos(inc_pos);
    ImGui::InvisibleButton("##inc", ImVec2(side, h));
    const bool inc_hover = can_increase && ImGui::IsItemHovered();
    if (can_increase && ImGui::IsItemClicked()) result = 1;
    if (inc_hover) {
        dl->AddRectFilled(inc_pos, inc_pos + ImVec2(side, h), U32(WithAlpha(P().accent, 0.10f)),
                          M().r_md, ImDrawFlags_RoundCornersRight);
    }
    glyph(ImVec2(inc_pos.x + side * 0.5f, pos.y + h * 0.5f), true, can_increase, inc_hover);

    // --- Separateurs et valeur ---------------------------------------------
    const float inset = 7.0f * M().scale;
    dl->AddLine(ImVec2(pos.x + side, pos.y + inset), ImVec2(pos.x + side, pos.y + h - inset),
                U32(P().border_subtle), M().border);
    dl->AddLine(ImVec2(inc_pos.x, pos.y + inset), ImVec2(inc_pos.x, pos.y + h - inset),
                U32(P().border_subtle), M().border);

    ImGui::PushFont(F().mono, M().font_small);
    const ImVec2 ts = ImGui::CalcTextSize(value);
    dl->AddText(ImVec2(pos.x + (w - ts.x) * 0.5f, pos.y + (h - ts.y) * 0.5f), U32(P().text),
                value);
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::PopID();
    return result;
}

namespace {

bool StyledButton(const char* label, const ImVec2& size, bool enabled, const ImVec4& bg,
                  const ImVec4& bg_hover, const ImVec4& bg_active, const ImVec4& fg,
                  const ImVec4& border) {
    ImGui::PushFont(F().semibold, M().font_body);
    ImGui::PushStyleColor(ImGuiCol_Button, enabled ? bg : WithAlpha(bg, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, enabled ? bg_hover : WithAlpha(bg, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, enabled ? bg_active : WithAlpha(bg, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_Text, enabled ? fg : P().text_disabled);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(M().sp_lg, M().sp_sm + 1));

    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::Button(label, size);
    ImGui::EndDisabled();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);
    ImGui::PopFont();
    return clicked;
}

} // namespace

bool PrimaryButton(const char* label, const ImVec2& size, bool enabled) {
    return StyledButton(label, size, enabled, P().accent, P().accent_hover, P().accent_active,
                        Hex(0x04141D), ImVec4(0, 0, 0, 0));
}

bool GhostButton(const char* label, const ImVec2& size, bool enabled) {
    return StyledButton(label, size, enabled, ImVec4(0, 0, 0, 0), P().surface2, P().surface3,
                        P().text_secondary, P().border_strong);
}

bool DangerButton(const char* label, const ImVec2& size, bool enabled) {
    return StyledButton(label, size, enabled, P().danger_subtle,
                        WithAlpha(P().danger, 0.24f), WithAlpha(P().danger, 0.34f),
                        P().danger, WithAlpha(P().danger, 0.35f));
}

// ===========================================================================
// Donnees
// ===========================================================================
void ProgressBar(float fraction, const ImVec2& size, Status s) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList*  dl = ImGui::GetWindowDrawList();
    const float  r = size.y * 0.5f;

    dl->AddRectFilled(p, p + size, U32(P().sunken), r);
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    if (f > 0.001f) {
        const float w = std::max(size.y, size.x * f);
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + size.y), U32(StatusColor(s)), r);
    }
    ImGui::Dummy(size);
}

void MetricTile(const char* label, const char* value, const char* unit, float fraction,
                Status s, const ImVec2& size) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList*  dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(p, p + size, U32(P().surface1), M().r_lg);
    dl->AddRect(p, p + size, U32(P().border_subtle), M().r_lg, 0, M().border);

    const float px = M().sp_md;
    float       y  = p.y + M().sp_md;

    // Libelle
    ImGui::PushFont(F().regular, M().font_micro);
    dl->AddText(ImVec2(p.x + px, y), U32(P().text_muted), label);
    y += ImGui::GetFontSize() + M().sp_xs;
    ImGui::PopFont();

    // Valeur + unite sur la meme ligne de base
    ImGui::PushFont(F().semibold, M().font_display);
    const ImVec2 vs = ImGui::CalcTextSize(value);
    dl->AddText(ImVec2(p.x + px, y), U32(P().text), value);
    ImGui::PopFont();

    if (unit && *unit) {
        ImGui::PushFont(F().regular, M().font_small);
        const float baseline = y + vs.y - ImGui::GetFontSize() - 1.0f * M().scale;
        dl->AddText(ImVec2(p.x + px + vs.x + M().sp_xs, baseline), U32(P().text_muted), unit);
        ImGui::PopFont();
    }
    y += vs.y + M().sp_sm;

    if (fraction >= 0.0f) {
        const float bar_h = 3.0f * M().scale;
        const float bar_w = size.x - px * 2;
        const ImVec2 bp(p.x + px, std::min(y, p.y + size.y - M().sp_md - bar_h));
        dl->AddRectFilled(bp, bp + ImVec2(bar_w, bar_h), U32(P().sunken), bar_h * 0.5f);
        const float f = std::clamp(fraction, 0.0f, 1.0f);
        if (f > 0.001f) {
            dl->AddRectFilled(bp, bp + ImVec2(std::max(bar_h, bar_w * f), bar_h),
                              U32(StatusColor(s)), bar_h * 0.5f);
        }
    }
    ImGui::Dummy(size);
}

void Sparkline(const char* id, const float* values, int count, int offset, float vmin,
               float vmax, const ImVec2& size, Status s) {
    (void)id;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList*  dl = ImGui::GetWindowDrawList();
    if (count < 2 || vmax <= vmin) { ImGui::Dummy(size); return; }

    const ImVec4 col = StatusColor(s);
    const float  step = size.x / static_cast<float>(count - 1);

    static ImVector<ImVec2> pts;
    pts.resize(count);
    for (int i = 0; i < count; ++i) {
        const float v = values[(offset + i) % count];
        const float t = std::clamp((v - vmin) / (vmax - vmin), 0.0f, 1.0f);
        pts[i] = ImVec2(p.x + step * i, p.y + size.y - t * size.y);
    }

    // Remplissage degrade sous la courbe : une bande par segment, l'alpha
    // s'attenue vers le bas.
    for (int i = 0; i < count - 1; ++i) {
        const ImVec2 a = pts[i], b = pts[i + 1];
        dl->AddQuadFilled(a, b, ImVec2(b.x, p.y + size.y), ImVec2(a.x, p.y + size.y),
                          U32(WithAlpha(col, 0.13f)));
    }
    dl->AddPolyline(pts.Data, count, U32(col), 0, 1.6f * M().scale);

    // Point de tete : ou en est la mesure la plus recente.
    dl->AddCircleFilled(pts[count - 1], 2.6f * M().scale, U32(col), 12);
    ImGui::Dummy(size);
}

void Chart(const char* id, const float* values, int count, int offset, const ImVec2& size,
           const ChartOptions& opt) {
    (void)id;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList*  dl = ImGui::GetWindowDrawList();
    const ImVec4 col = StatusColor(opt.status);

    // Gouttiere de gauche : dimensionnee sur l'etiquette la plus large.
    char maxlabel[32];
    std::snprintf(maxlabel, sizeof(maxlabel), "%.0f%s", opt.vmax, opt.unit ? opt.unit : "");
    ImGui::PushFont(F().mono, M().font_micro);
    const float gutter = ImGui::CalcTextSize(maxlabel).x + M().sp_sm;
    const float label_h = ImGui::GetFontSize();
    ImGui::PopFont();

    const float x_axis_h = opt.x_label ? label_h + M().sp_xs : 0.0f;
    const ImVec2 plot_pos(p.x + gutter, p.y);
    const ImVec2 plot_size(size.x - gutter, size.y - x_axis_h);

    dl->AddRectFilled(plot_pos, ImVec2(plot_pos.x + plot_size.x, plot_pos.y + plot_size.y),
                      U32(P().sunken), M().r_md);

    // --- Graduations horizontales -----------------------------------------
    const int lines = (std::max)(1, opt.grid_lines + 1);
    ImGui::PushFont(F().mono, M().font_micro);
    for (int i = 0; i <= lines; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(lines);
        const float y = plot_pos.y + plot_size.y * t;
        const float v = opt.vmax - (opt.vmax - opt.vmin) * t;

        // La ligne du bas se confond avec le bord du cadre : on l'omet.
        if (i < lines) {
            dl->AddLine(ImVec2(plot_pos.x, y), ImVec2(plot_pos.x + plot_size.x, y),
                        U32(WithAlpha(P().border_subtle, i == 0 ? 0.9f : 0.55f)), M().border);
        }
        char lbl[32];
        std::snprintf(lbl, sizeof(lbl), "%.0f", v);
        const ImVec2 ts = ImGui::CalcTextSize(lbl);
        const float  ly = std::clamp(y - ts.y * 0.5f, p.y, plot_pos.y + plot_size.y - ts.y);
        dl->AddText(ImVec2(plot_pos.x - M().sp_sm - ts.x, ly), U32(P().text_disabled), lbl);
    }
    ImGui::PopFont();

    if (count < 2 || opt.vmax <= opt.vmin) { ImGui::Dummy(size); return; }

    // --- Courbe ------------------------------------------------------------
    const float step = plot_size.x / static_cast<float>(count - 1);
    static ImVector<ImVec2> pts;
    pts.resize(count);
    for (int i = 0; i < count; ++i) {
        const float v = values[(offset + i) % count];
        const float t = std::clamp((v - opt.vmin) / (opt.vmax - opt.vmin), 0.0f, 1.0f);
        pts[i] = ImVec2(plot_pos.x + step * i, plot_pos.y + plot_size.y - t * plot_size.y);
    }
    for (int i = 0; i < count - 1; ++i) {
        dl->AddQuadFilled(pts[i], pts[i + 1],
                          ImVec2(pts[i + 1].x, plot_pos.y + plot_size.y),
                          ImVec2(pts[i].x, plot_pos.y + plot_size.y),
                          U32(WithAlpha(col, 0.16f)));
    }
    dl->AddPolyline(pts.Data, count, U32(col), 0, 1.7f * M().scale);

    // --- Derniere mesure ---------------------------------------------------
    if (opt.show_current) {
        const ImVec2 head = pts[count - 1];
        // Repere horizontal au niveau courant : situe la valeur d'un coup d'oeil.
        for (float x = plot_pos.x; x < head.x; x += 6.0f * M().scale) {
            dl->AddLine(ImVec2(x, head.y), ImVec2(x + 3.0f * M().scale, head.y),
                        U32(WithAlpha(col, 0.30f)), M().border);
        }
        dl->AddCircleFilled(head, 3.0f * M().scale, U32(col), 12);

        char cur[32];
        std::snprintf(cur, sizeof(cur), "%.0f%s", values[(offset + count - 1) % count],
                      opt.unit ? opt.unit : "");
        ImGui::PushFont(F().semibold, M().font_micro);
        const ImVec2 ts = ImGui::CalcTextSize(cur);
        const ImVec2 pad(M().sp_sm * 0.75f, M().sp_xs * 0.6f);
        // Etiquette calee en haut a droite plutot que collee a la courbe :
        // une valeur basse la rendait illisible contre le bord du cadre.
        const ImVec2 box(plot_pos.x + plot_size.x - ts.x - pad.x * 2 - M().sp_sm,
                         plot_pos.y + M().sp_sm);
        dl->AddRectFilled(box, ImVec2(box.x + ts.x + pad.x * 2, box.y + ts.y + pad.y * 2),
                          U32(StatusSubtle(opt.status)), M().r_sm);
        dl->AddText(ImVec2(box.x + pad.x, box.y + pad.y), U32(col), cur);
        ImGui::PopFont();
    }

    // --- Axe temporel ------------------------------------------------------
    if (opt.x_label) {
        ImGui::PushFont(F().regular, M().font_micro);
        const ImVec2 ts = ImGui::CalcTextSize(opt.x_label);
        dl->AddText(ImVec2(plot_pos.x + plot_size.x - ts.x, plot_pos.y + plot_size.y + M().sp_xs),
                    U32(P().text_disabled), opt.x_label);
        dl->AddText(ImVec2(plot_pos.x, plot_pos.y + plot_size.y + M().sp_xs),
                    U32(P().text_disabled), "passe");
        ImGui::PopFont();
    }
    ImGui::Dummy(size);
}

// ===========================================================================
// Divers
// ===========================================================================
void HelpMarker(const char* text) {
    Small("(?)", P().text_disabled);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(M().sp_md, M().sp_md));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, P().surface2);
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(320.0f * M().scale);
        ImGui::PushFont(F().regular, M().font_small);
        ImGui::TextUnformatted(text);
        ImGui::PopFont();
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
}

float Animate(ImGuiID id, float target, float speed) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    float*        v = storage->GetFloatRef(id, target);
    const float   dt = ImGui::GetIO().DeltaTime;
    // Convergence exponentielle : le rendu reste identique quel que soit le
    // nombre d'images par seconde.
    *v += (target - *v) * (1.0f - std::exp(-speed * dt));
    if (std::fabs(target - *v) < 0.001f) *v = target;
    return *v;
}

// ===========================================================================
// Courbe de ventilateur
// ===========================================================================
int FanCurveLevelAt(const FanPoint* pts, int count, float temp_c) {
    if (count <= 0) return 0;
    if (temp_c <= static_cast<float>(pts[0].temp_c)) return pts[0].level_pct;
    if (temp_c >= static_cast<float>(pts[count - 1].temp_c)) return pts[count - 1].level_pct;
    for (int i = 0; i < count - 1; ++i) {
        const float a = static_cast<float>(pts[i].temp_c);
        const float b = static_cast<float>(pts[i + 1].temp_c);
        if (temp_c < a || temp_c > b) continue;
        const float span = b - a;
        if (span <= 0.0f) return pts[i + 1].level_pct;
        const float t = (temp_c - a) / span;
        const float l = pts[i].level_pct + (pts[i + 1].level_pct - pts[i].level_pct) * t;
        return static_cast<int>(std::lround(l));
    }
    return pts[count - 1].level_pct;
}

bool FanCurveEditor(const char* id, FanPoint* pts, int count, const ImVec2& size,
                    const FanCurveView& v) {
    ImGuiWindow*  win = ImGui::GetCurrentWindow();
    const ImGuiID gid = win->GetID(id);
    const ImVec2  p = ImGui::GetCursorScreenPos();

    // Gouttiere de gauche et bandeau du bas, dimensionnes sur les etiquettes.
    ImGui::PushFont(F().mono, M().font_micro);
    const float gutter = ImGui::CalcTextSize("100").x + M().sp_sm;
    const float axis_h = ImGui::GetFontSize() + M().sp_xs;
    ImGui::PopFont();

    const ImVec2 plot(p.x + gutter, p.y);
    const ImVec2 psize(size.x - gutter, size.y - axis_h);

    ImGui::ItemSize(size);
    if (!ImGui::ItemAdd(ImRect(p, p + size), gid)) return false;
    if (count < 2 || psize.x <= 1.0f || psize.y <= 1.0f) return false;

    ImDrawList*  dl = ImGui::GetWindowDrawList();
    const ImVec4 col = P().accent;
    const float  t_span = static_cast<float>(v.temp_max - v.temp_min);
    const float  l_span = 100.0f;

    auto xof = [&](float t) {
        return plot.x + psize.x * (t - static_cast<float>(v.temp_min)) / t_span;
    };
    auto yof = [&](float l) { return plot.y + psize.y * (1.0f - l / l_span); };

    dl->AddRectFilled(plot, plot + psize, U32(P().sunken), M().r_md);

    // --- Plancher d ecriture -------------------------------------------------
    const float floor_y = yof(static_cast<float>(v.level_floor));
    dl->AddRectFilled(ImVec2(plot.x, floor_y), ImVec2(plot.x + psize.x, plot.y + psize.y),
                      U32(WithAlpha(P().border_subtle, 0.45f)), M().r_md,
                      ImDrawFlags_RoundCornersBottom);
    {
        char fl[32];
        std::snprintf(fl, sizeof(fl), "plancher %d %%", v.level_floor);
        ImGui::PushFont(F().regular, M().font_micro);
        dl->AddText(ImVec2(plot.x + M().sp_sm, floor_y + M().sp_xs * 0.5f),
                    U32(P().text_disabled), fl);
        ImGui::PopFont();
    }

    // --- Grille -------------------------------------------------------------
    ImGui::PushFont(F().mono, M().font_micro);
    for (int lv = 0; lv <= 100; lv += 25) {
        const float y = yof(static_cast<float>(lv));
        if (lv > 0 && lv < 100) {
            dl->AddLine(ImVec2(plot.x, y), ImVec2(plot.x + psize.x, y),
                        U32(WithAlpha(P().border_subtle, 0.55f)), M().border);
        }
        char lbl[8];
        std::snprintf(lbl, sizeof(lbl), "%d", lv);
        const ImVec2 ts = ImGui::CalcTextSize(lbl);
        dl->AddText(ImVec2(plot.x - M().sp_sm - ts.x,
                           std::clamp(y - ts.y * 0.5f, plot.y, plot.y + psize.y - ts.y)),
                    U32(P().text_disabled), lbl);
    }
    for (int tc = v.temp_min; tc <= v.temp_max; tc += 15) {
        const float x = xof(static_cast<float>(tc));
        if (tc > v.temp_min) {
            dl->AddLine(ImVec2(x, plot.y), ImVec2(x, plot.y + psize.y),
                        U32(WithAlpha(P().border_subtle, 0.40f)), M().border);
        }
        char lbl[8];
        std::snprintf(lbl, sizeof(lbl), "%d", tc);
        const ImVec2 ts = ImGui::CalcTextSize(lbl);
        dl->AddText(ImVec2(std::clamp(x - ts.x * 0.5f, plot.x, plot.x + psize.x - ts.x),
                           plot.y + psize.y + M().sp_xs),
                    U32(P().text_disabled), lbl);
    }
    ImGui::PopFont();

    // --- Deplacement d'un point ---------------------------------------------
    // L'index en cours de glissement vit dans le stockage d'ImGui plutot que
    // dans une variable statique : deux courbes a l'ecran ne se marcheraient
    // pas dessus. -1 = repos, -2 = clic dans le vide (aucune prise).
    bool changed = false;
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID drag_key = ImHashStr("##fandrag", 0, gid);
    int           drag = st->GetInt(drag_key, -1);

    if (v.editable) {
        bool hovered = false, held = false;
        ImGui::ButtonBehavior(ImRect(plot, plot + psize), gid, &hovered, &held);
        const ImVec2 m = ImGui::GetIO().MousePos;

        if (held && drag == -1) {
            float best = 20.0f * M().scale;
            int   pick = -1;
            for (int i = 0; i < count; ++i) {
                const ImVec2 h(xof(static_cast<float>(pts[i].temp_c)),
                               yof(static_cast<float>(pts[i].level_pct)));
                const float  d = std::sqrt(ImLengthSqr(m - h));
                if (d < best) { best = d; pick = i; }
            }
            drag = (pick >= 0) ? pick : -2;
            st->SetInt(drag_key, drag);
        } else if (!held && drag != -1) {
            drag = -1;
            st->SetInt(drag_key, -1);
        }

        if (drag >= 0 && drag < count) {
            int nt = static_cast<int>(std::lround(
                static_cast<float>(v.temp_min) + (m.x - plot.x) / psize.x * t_span));
            int nl = static_cast<int>(
                std::lround((1.0f - (m.y - plot.y) / psize.y) * l_span));

            nt = std::clamp(nt, v.temp_min, v.temp_max);
            nl = std::clamp(nl, v.level_floor, 100);
            // Deux points ne doivent jamais se superposer : passe le meme
            // abscisse, l'un des deux devient impossible a rattraper.
            if (drag > 0)         nt = (std::max)(nt, pts[drag - 1].temp_c + 2);
            if (drag < count - 1) nt = (std::min)(nt, pts[drag + 1].temp_c - 2);
            // Monotonie : une courbe qui redescend ferait osciller le
            // ventilateur autour du point d'inversion.
            if (drag > 0)         nl = (std::max)(nl, pts[drag - 1].level_pct);
            if (drag < count - 1) nl = (std::min)(nl, pts[drag + 1].level_pct);

            if (nt != pts[drag].temp_c || nl != pts[drag].level_pct) {
                pts[drag].temp_c = nt;
                pts[drag].level_pct = nl;
                changed = true;
            }
        }
    }

    // --- Courbe --------------------------------------------------------------
    // La courbe est plate avant le premier point et apres le dernier : c'est
    // exactement ce que fait l'evaluation, autant le montrer.
    ImVector<ImVec2> line;
    line.reserve(count + 2);
    line.push_back(ImVec2(plot.x, yof(static_cast<float>(pts[0].level_pct))));
    for (int i = 0; i < count; ++i) {
        line.push_back(ImVec2(xof(static_cast<float>(pts[i].temp_c)),
                              yof(static_cast<float>(pts[i].level_pct))));
    }
    line.push_back(ImVec2(plot.x + psize.x,
                          yof(static_cast<float>(pts[count - 1].level_pct))));

    for (int i = 0; i < line.Size - 1; ++i) {
        dl->AddQuadFilled(line[i], line[i + 1],
                          ImVec2(line[i + 1].x, plot.y + psize.y),
                          ImVec2(line[i].x, plot.y + psize.y),
                          U32(WithAlpha(col, v.editable ? 0.16f : 0.07f)));
    }
    dl->AddPolyline(line.Data, line.Size, U32(v.editable ? col : WithAlpha(col, 0.45f)), 0,
                    2.0f * M().scale);

    // --- Repere de mesure -----------------------------------------------------
    if (v.live_temp_c >= 0.0f) {
        const float lx = std::clamp(xof(v.live_temp_c), plot.x, plot.x + psize.x);
        for (float y = plot.y; y < plot.y + psize.y; y += 6.0f * M().scale) {
            dl->AddLine(ImVec2(lx, y), ImVec2(lx, y + 3.0f * M().scale),
                        U32(WithAlpha(P().text_muted, 0.55f)), M().border);
        }
        const int   lvl = FanCurveLevelAt(pts, count, v.live_temp_c);
        const ImVec2 dot(lx, yof(static_cast<float>(lvl)));
        dl->AddCircleFilled(dot, 4.5f * M().scale, U32(P().bg), 16);
        dl->AddCircleFilled(dot, 3.0f * M().scale, U32(P().warn), 16);

        char lbl[48];
        std::snprintf(lbl, sizeof(lbl), "%.0f \xc2\xb0""C  \xe2\x86\x92  %d %%", v.live_temp_c, lvl);
        ImGui::PushFont(F().semibold, M().font_micro);
        const ImVec2 ts = ImGui::CalcTextSize(lbl);
        const ImVec2 bp(std::clamp(dot.x + M().sp_sm, plot.x,
                                   plot.x + psize.x - ts.x - M().sp_sm * 2),
                        plot.y + M().sp_sm);
        dl->AddRectFilled(bp, ImVec2(bp.x + ts.x + M().sp_sm, bp.y + ts.y + M().sp_xs),
                          U32(WithAlpha(P().warn, 0.16f)), M().r_sm);
        dl->AddText(ImVec2(bp.x + M().sp_sm * 0.5f, bp.y + M().sp_xs * 0.5f), U32(P().warn), lbl);
        ImGui::PopFont();
    }

    // --- Poignees -------------------------------------------------------------
    if (v.editable) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        for (int i = 0; i < count; ++i) {
            const ImVec2 h(xof(static_cast<float>(pts[i].temp_c)),
                           yof(static_cast<float>(pts[i].level_pct)));
            const bool  on = (drag == i) ||
                             (drag == -1 && std::sqrt(ImLengthSqr(m - h)) < 20.0f * M().scale);
            const float r = (on ? 6.5f : 5.0f) * M().scale;
            dl->AddCircleFilled(h, r + 1.5f * M().scale, U32(P().sunken), 20);
            dl->AddCircleFilled(h, r, U32(on ? P().accent_hover : col), 20);
        }
    }

    return changed;
}

} // namespace tf::ui
