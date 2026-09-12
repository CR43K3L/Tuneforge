#include "ui/app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <windows.h>
#include <shellapi.h>

#include "imgui_internal.h"
#include "platform/elevation.hpp"
#include "ui/widgets.hpp"

namespace tf::ui {
namespace {

struct NavEntry { Page page; const char* label; };

const NavEntry kNav[] = {
    {Page::Dashboard, "Tableau de bord"},
    {Page::Tweaks,    "Optimisations"},
    {Page::Gpu,       "Overclock GPU"},
    {Page::Restore,   "Restauration"},
    {Page::Hardware,  "Materiel"},
    {Page::Settings,  "Parametres"},
};

const char* page_title(Page p) {
    switch (p) {
        case Page::Dashboard: return "Tableau de bord";
        case Page::Tweaks:    return "Optimisations";
        case Page::Gpu:       return "Overclock GPU";
        case Page::Restore:   return "Restauration";
        case Page::Hardware:  return "Materiel";
        case Page::Settings:  return "Parametres";
    }
    return "";
}

const char* page_subtitle(Page p) {
    switch (p) {
        case Page::Dashboard: return "Telemetrie en direct et etat du systeme";
        case Page::Tweaks:    return "Un questionnaire vous guide reglage par reglage";
        case Page::Gpu:       return "Reglages GPU volatiles, perdus au redemarrage";
        case Page::Restore:   return "Tout ce que Tuneforge peut remettre en etat";
        case Page::Hardware:  return "Ce que Tuneforge a detecte sur cette machine";
        case Page::Settings:  return "Theme, taille de l'interface, diagnostic et journal";
    }
    return "";
}

Status status_of(TweakState s) {
    switch (s) {
        case TweakState::Applied:     return Status::Ok;
        case TweakState::Partial:     return Status::Warn;
        case TweakState::Unavailable: return Status::Danger;
        default:                      return Status::Neutral;
    }
}

Status tier_status(Tier t) {
    switch (t) {
        case Tier::Basic:    return Status::Ok;
        case Tier::Advanced: return Status::Warn;
        case Tier::Expert:   return Status::Danger;
    }
    return Status::Neutral;
}

// Bouton de barre de titre : un glyphe trace a la main, pas de police d'icones.
enum class SysGlyph { Minimize, Maximize, Restore, Close };

bool TitlebarButton(const char* id, SysGlyph g, bool danger) {
    const float h = M().titlebar_h;
    const float w = h * 1.22f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();
    const bool clicked = ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsItemClicked();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered) {
        const ImVec4 bg = danger ? Hex(0xE81123, held ? 0.95f : 0.85f)
                                 : WithAlpha(P().text, held ? 0.14f : 0.08f);
        dl->AddRectFilled(pos, pos + ImVec2(w, h), U32(bg));
    }

    const ImVec2 c(pos.x + w * 0.5f, pos.y + h * 0.5f);
    const float  s = 5.0f * M().scale;
    const ImU32  col = U32((hovered && danger) ? Hex(0xFFFFFF) : P().text_secondary);
    const float  th = 1.25f * M().scale;

    switch (g) {
        case SysGlyph::Minimize:
            dl->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y), col, th);
            break;
        case SysGlyph::Maximize:
            dl->AddRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, 1.5f, 0, th);
            break;
        case SysGlyph::Restore:
            dl->AddRect(ImVec2(c.x - s, c.y - s * 0.55f), ImVec2(c.x + s * 0.55f, c.y + s),
                        col, 1.5f, 0, th);
            dl->AddLine(ImVec2(c.x - s * 0.4f, c.y - s * 0.55f),
                        ImVec2(c.x - s * 0.4f, c.y - s), col, th);
            dl->AddLine(ImVec2(c.x - s * 0.4f, c.y - s), ImVec2(c.x + s, c.y - s), col, th);
            dl->AddLine(ImVec2(c.x + s, c.y - s), ImVec2(c.x + s, c.y + s * 0.4f), col, th);
            break;
        case SysGlyph::Close:
            dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, th);
            dl->AddLine(ImVec2(c.x + s, c.y - s), ImVec2(c.x - s, c.y + s), col, th);
            break;
    }
    return clicked;
}

std::string human_bytes(uint64_t b) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(b) / 1073741824.0);
    return buf;
}

} // namespace

// ===========================================================================
void History::push(float v) {
    // Sans amorcage, la courbe reste collee au bas du cadre pendant les
    // quarante premieres secondes : on prefille avec la premiere mesure.
    if (!primed) {
        for (float& x : values) x = v;
        primed = true;
    }
    values[offset] = v;
    offset = (offset + 1) % kCapacity;
    last = v;
    float m = 0.0f;
    for (float x : values) m = (std::max)(m, x);
    vmax = (std::max)(m * 1.15f, 1.0f);
}

// ===========================================================================
namespace {

// Courbe par defaut : silencieuse jusqu'a 55 °C, puis montee reguliere. Le
// premier point est a 30 % et non a zero — en manuel la carte n'accepte pas
// moins, et pretendre le contraire sur le graphe serait mentir.
std::vector<FanPoint> default_fan_curve() {
    return {{40, 30}, {55, 40}, {65, 55}, {75, 75}, {85, 100}};
}

// Selecteur segmente. Trois boutons plutot qu'une liste deroulante : les trois
// options tiennent a l'ecran, les cacher derriere un clic n'apporterait rien.
bool ModeButton(const char* label, bool active, float w) {
    return active ? PrimaryButton(label, ImVec2(w, 0)) : GhostButton(label, ImVec2(w, 0));
}

}  // namespace

bool App::Init(Window* window) {
    window_  = window;
    engine_  = std::make_unique<Engine>();
    elevated_ = is_elevated();

    has_hwinfo_ = sensors_.open();
    has_nvml_   = nvml_.init();
    counters_.sample();
    cpu_freq_.init(engine_->hardware().cpu.base_mhz);

    gpu_ctl_ = hw::make_gpu_controller(&gpu_absent_);
    if (gpu_ctl_ && gpu_ctl_->gpu_count() > 0) {
        // Les curseurs partent des valeurs reellement en place, pas de zeros.
        const auto& g = gpu_ctl_->gpu(0);
        if (g.power.valid) power_target_ = static_cast<int>(g.power.current_pct);
        if (g.offsets.valid) {
            core_target_ = g.offsets.graphics_delta_khz / 1000;
            mem_target_  = g.offsets.memory_delta_khz / 1000;
        }
    }

    // Courbe et mode ventilateur : ils vivent dans ui.json, a cote du theme.
    // LoadUiPrefsStatic tourne avant l'existence de l'instance et ne peut donc
    // pas les lire — c'est fait ici.
    if (auto text = read_text_file(data_dir() + L"\\ui.json")) {
        if (auto j = Json::parse(*text); j && j->is_object()) {
            std::vector<FanPoint> pts;
            for (const auto& it : (*j)["fan_curve"].items()) {
                if (!it.is_object()) continue;
                FanPoint fp;
                fp.temp_c    = static_cast<int>(it["t"].as_int(0));
                fp.level_pct = static_cast<int>(it["l"].as_int(0));
                if (fp.temp_c > 0) pts.push_back(fp);
            }
            if (pts.size() >= 2) fan_curve_ = std::move(pts);
            const int64_t mode = (*j)["fan_mode"].as_int(0);
            if (mode >= 0 && mode <= 2) fan_mode_ = static_cast<FanMode>(mode);
        }
    }
    if (fan_curve_.size() < 2) fan_curve_ = default_fan_curve();

    RefreshLog();
    return true;
}

void App::SampleTelemetry() {
    const double now = ImGui::GetTime();
    if (now - last_sample_ < 0.25) return;
    last_sample_ = now;

    snapshot_ = counters_.sample();
    cpu_hist_.push(static_cast<float>(snapshot_.cpu_load_pct));

    // Frequence reelle via PDH : disponible sans HWiNFO et sans driver.
    const float mhz = cpu_freq_.sample_mhz();
    if (mhz > 0.0f) cpu_mhz_ = mhz;

    if (has_hwinfo_ && sensors_.refresh()) {
        auto pick = [&](const char* label, hw::ReadingType t) -> float {
            const hw::Reading* r = sensors_.find_any(label, t);
            return r ? static_cast<float>(r->value) : -1.0f;
        };
        cpu_temp_ = pick("Tctl", hw::ReadingType::Temperature);
        if (cpu_temp_ < 0) cpu_temp_ = pick("CPU", hw::ReadingType::Temperature);
        cpu_power_ = pick("CPU Package Power", hw::ReadingType::Power);
        if (cpu_power_ < 0) cpu_power_ = pick("CPU PPT", hw::ReadingType::Power);
        cpu_clock_ = pick("Core Clock", hw::ReadingType::Clock);
        if (cpu_temp_ > 0) cpu_temp_hist_.push(cpu_temp_);
    }

    // NVAPI traverse le pilote a chaque appel : une seconde suffit largement.
    if (gpu_ctl_ && page_ == Page::Gpu && now - last_gpu_ > 1.0) {
        last_gpu_ = now;
        gpu_ctl_->refresh();
    }

    if (has_nvml_) {
        if (auto g = nvml_.sample(0)) {
            gpu_ = *g;
            gpu_hist_.push(static_cast<float>(gpu_.utilization_pct));
            gpu_temp_hist_.push(static_cast<float>(gpu_.temperature_c));
        }
    }
}

void App::RefreshLog() {
    last_log_read_ = ImGui::GetTime();
    log_lines_.clear();
    auto text = read_text_file(data_dir() + L"\\tuneforge.log");
    if (!text) return;
    for (auto& l : split(*text, '\n')) {
        std::string t = trim(l);
        if (!t.empty()) log_lines_.push_back(std::move(t));
    }
    if (log_lines_.size() > 400) {
        log_lines_.erase(log_lines_.begin(),
                         log_lines_.begin() + static_cast<long long>(log_lines_.size() - 400));
    }
    std::reverse(log_lines_.begin(), log_lines_.end());
}

void App::StartQuiz() {
    page_ = Page::Tweaks;
    BuildQuiz(level_);
    flow_ = quiz_ids_.empty() ? TweakFlow::Intro : TweakFlow::Question;
}

bool App::ConsumeStyleReload() {
    const bool v = style_reload_;
    style_reload_ = false;
    return v;
}

void App::Notify(const std::string& text, Status s) {
    notice_ = text;
    notice_status_ = s;
    notice_until_ = ImGui::GetTime() + 6.0;
}

// ===========================================================================
void App::Frame() {
    SampleTelemetry();
    ApplyFanCurve();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar(2);

    DrawTitleBar(vp->WorkSize.x);

    ImGui::SetCursorPos(ImVec2(0, M().titlebar_h));
    DrawSidebar();
    ImGui::SameLine(0, 0);
    DrawContent();

    ImGui::End();
}

// ---------------------------------------------------------------------------
void App::DrawTitleBar(float width) {
    const float h = M().titlebar_h;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList*  dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(origin, origin + ImVec2(width, h), U32(P().sidebar));
    dl->AddLine(ImVec2(origin.x, origin.y + h), ImVec2(origin.x + width, origin.y + h),
                U32(P().border_subtle), M().border);

    // Marque : un losange cyan plutot qu'une icone importee.
    const ImVec2 c(origin.x + M().sp_lg + 6 * M().scale, origin.y + h * 0.5f);
    const float  r = 6.0f * M().scale;
    dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r),
                      ImVec2(c.x - r, c.y), U32(P().accent));
    dl->AddQuadFilled(ImVec2(c.x, c.y - r * 0.45f), ImVec2(c.x + r * 0.45f, c.y),
                      ImVec2(c.x, c.y + r * 0.45f), ImVec2(c.x - r * 0.45f, c.y),
                      U32(P().sidebar));

    ImGui::PushFont(F().semibold, M().font_body);
    const float name_y = origin.y + (h - ImGui::GetFontSize()) * 0.5f;
    dl->AddText(ImVec2(c.x + r + M().sp_md, name_y), U32(P().text), "TUNEFORGE");
    const float name_w = ImGui::CalcTextSize("TUNEFORGE").x;
    ImGui::PopFont();

    ImGui::PushFont(F().regular, M().font_micro);
    dl->AddText(ImVec2(c.x + r + M().sp_md + name_w + M().sp_sm,
                       origin.y + (h - ImGui::GetFontSize()) * 0.5f + 1.0f),
                U32(P().text_disabled), "v" TF_VERSION);
    ImGui::PopFont();

    // Boutons systeme, ancres a droite.
    const float bw = h * 1.22f;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + width - bw * 3, origin.y));
    if (TitlebarButton("##min", SysGlyph::Minimize, false)) window_->Minimize();
    ImGui::SameLine(0, 0);
    if (TitlebarButton("##max",
                       window_->maximized() ? SysGlyph::Restore : SysGlyph::Maximize, false)) {
        window_->ToggleMaximize();
    }
    ImGui::SameLine(0, 0);
    if (TitlebarButton("##close", SysGlyph::Close, true)) window_->Close();

    // Le deplacement de la fenetre n'est autorise que hors des boutons.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool over_bar = mouse.y >= origin.y && mouse.y < origin.y + h;
    const bool over_buttons = mouse.x >= origin.x + width - bw * 3;
    window_->SetTitlebarHeight(h);
    window_->SetDragAllowed(over_bar && !over_buttons);

    // Double-clic sur la barre : agrandir / restaurer, comme partout ailleurs.
    if (over_bar && !over_buttons && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        window_->ToggleMaximize();
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + h));
}

// ---------------------------------------------------------------------------
void App::DrawSidebar() {
    const float h = ImGui::GetContentRegionAvail().y;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, P().sidebar);
    ImGui::BeginChild("##sidebar", ImVec2(M().sidebar_w, h), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetWindowPos();
    dl->AddLine(ImVec2(p0.x + M().sidebar_w - 1, p0.y), ImVec2(p0.x + M().sidebar_w - 1, p0.y + h),
                U32(P().border_subtle), M().border);

    ImGui::SetCursorPos(ImVec2(M().sp_md, M().sp_lg));
    ImGui::BeginGroup();
    SectionLabel("Navigation");
    VSpace(M().sp_xs);

    ImGui::PushItemWidth(M().sidebar_w - M().sp_md * 2);
    for (const NavEntry& e : kNav) {
        ImGui::SetCursorPosX(M().sp_md);
        ImGui::PushID(static_cast<int>(e.page));
        Status ind = Status::Neutral;
        if (e.page == Page::Tweaks && !engine_->state().applied_ids().empty()) ind = Status::Ok;
        if (e.page == Page::Restore && DirtyFlag::exists()) ind = Status::Danger;
        if (NavItem(e.label, page_ == e.page, ind)) page_ = e.page;
        ImGui::PopID();
    }
    ImGui::PopItemWidth();
    ImGui::EndGroup();

    // Pied de barre laterale : l'echelle a demenage dans les parametres, il ne
    // reste que ce que l'outil s'autorise a faire.
    const float footer_h = 74 * M().scale;
    ImGui::SetCursorPos(ImVec2(M().sp_md, h - footer_h));
    ImGui::BeginGroup();
    dl->AddLine(ImVec2(p0.x + M().sp_md, ImGui::GetCursorScreenPos().y - M().sp_sm),
                ImVec2(p0.x + M().sidebar_w - M().sp_md, ImGui::GetCursorScreenPos().y - M().sp_sm),
                U32(P().border_subtle), M().border);

    StatusPill(elevated_ ? "Administrateur" : "Droits utilisateur",
               elevated_ ? Status::Accent : Status::Neutral);
    VSpace(M().sp_sm);
    Small("Aucun driver noyau", P().text_disabled);
    ImGui::EndGroup();

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
void App::DrawContent() {
    ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_None);

    ImGui::SetCursorPos(ImVec2(M().sp_xl, M().sp_lg));
    ImGui::BeginGroup();
    Display(page_title(page_));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - M().sp_xs);
    // Le sous-titre des Reglages depend de l'etape du parcours.
    const char* subtitle = page_subtitle(page_);
    if (page_ == Page::Tweaks) {
        switch (flow_) {
            case TweakFlow::Intro:
                subtitle = "Un questionnaire vous guide reglage par reglage"; break;
            case TweakFlow::Question:
                subtitle = "Ce que le reglage apporte, et ce qu'il coute"; break;
            case TweakFlow::Recap:
                subtitle = "Relisez vos choix avant d'appliquer quoi que ce soit"; break;
            case TweakFlow::Done:
                subtitle = "Compte rendu de l'application"; break;
        }
    }
    Muted(subtitle);
    ImGui::EndGroup();

    VSpace(M().sp_lg);
    DrawNotice();

    // Colonne de contenu, marges laterales constantes.
    ImGui::SetCursorPosX(M().sp_xl);
    const float avail = ImGui::GetContentRegionAvail().x - M().sp_xl;
    ImGui::BeginChild("##page", ImVec2(avail, 0), ImGuiChildFlags_None);
    switch (page_) {
        case Page::Dashboard: PageDashboard(); break;
        case Page::Tweaks:    PageTweaks();    break;
        case Page::Gpu:       PageGpu();       break;
        case Page::Restore:   PageRestore();   break;
        case Page::Hardware:  PageHardware();  break;
        case Page::Settings:  PageSettings();  break;
    }
    VSpace(M().sp_2xl);
    ImGui::EndChild();

    ImGui::EndChild();
}

void App::DrawNotice() {
    if (notice_.empty() || ImGui::GetTime() > notice_until_) return;

    ImGui::SetCursorPosX(M().sp_2xl);
    const float w = ImGui::GetContentRegionAvail().x - M().sp_2xl;
    const ImVec2 p = ImGui::GetCursorScreenPos();

    ImGui::PushFont(F().regular, M().font_small);
    const float pad = M().sp_md;
    const ImVec2 ts = ImGui::CalcTextSize(notice_.c_str(), nullptr, false, w - pad * 2 - 4);
    const ImVec2 size(w, ts.y + pad * 2);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + size, U32(StatusSubtle(notice_status_)), M().r_md);
    dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x, p.y + size.y), U32(StatusColor(notice_status_)),
                2.5f * M().scale);
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(ImVec2(p.x + pad, p.y + pad));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - pad * 2);
    TextAt(F().regular, M().font_small, StatusColor(notice_status_), notice_.c_str());
    ImGui::PopTextWrapPos();

    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + size.y + M().sp_md));
}

// ===========================================================================
// Tableau de bord
// ===========================================================================
void App::PageDashboard() {
    const hw::Profile& hwp = engine_->hardware();
    const float total = ImGui::GetContentRegionAvail().x;
    const float gap = M().sp_md;
    const float tile_w = (total - gap * 3) / 4.0f;
    const float tile_h = 84 * M().scale;

    char buf[96];

    // --- Rangee de tuiles --------------------------------------------------
    std::snprintf(buf, sizeof(buf), "%.0f", snapshot_.cpu_load_pct);
    MetricTile("CHARGE CPU", buf, "%", static_cast<float>(snapshot_.cpu_load_pct) / 100.0f,
               snapshot_.cpu_load_pct > 85 ? Status::Warn : Status::Accent,
               ImVec2(tile_w, tile_h));
    ImGui::SameLine(0, gap);

    // La frequence remplace la temperature : elle est mesurable sans HWiNFO.
    if (cpu_mhz_ > 0) {
        std::snprintf(buf, sizeof(buf), "%.2f", cpu_mhz_ / 1000.0f);
        const float base = static_cast<float>(hwp.cpu.base_mhz);
        MetricTile("FREQUENCE CPU", buf, "GHz",
                   base > 0 ? cpu_mhz_ / (base * 1.35f) : -1.0f, Status::Accent,
                   ImVec2(tile_w, tile_h));
    } else {
        MetricTile("FREQUENCE CPU", "--", "", -1.0f, Status::Neutral, ImVec2(tile_w, tile_h));
    }
    ImGui::SameLine(0, gap);

    std::snprintf(buf, sizeof(buf), "%.1f", snapshot_.ram_used_bytes / 1073741824.0);
    MetricTile("MEMOIRE", buf, ("Go / " + human_bytes(snapshot_.ram_total_bytes) + " Go").c_str(),
               static_cast<float>(snapshot_.ram_pct) / 100.0f,
               snapshot_.ram_pct > 88 ? Status::Warn : Status::Accent, ImVec2(tile_w, tile_h));
    ImGui::SameLine(0, gap);

    if (has_nvml_ && gpu_.valid) {
        std::snprintf(buf, sizeof(buf), "%u", gpu_.utilization_pct);
        MetricTile("CHARGE GPU", buf, "%", gpu_.utilization_pct / 100.0f, Status::Accent,
                   ImVec2(tile_w, tile_h));
    } else {
        MetricTile("CHARGE GPU", "--", "", -1.0f, Status::Neutral, ImVec2(tile_w, tile_h));
    }

    VSpace(gap);

    // --- Graphiques --------------------------------------------------------
    // La fenetre affichee vaut kCapacity echantillons a 250 ms.
    const int window_s = History::kCapacity / 4;
    char x_label[48];
    std::snprintf(x_label, sizeof(x_label), "maintenant  (%d s)", window_s);

    const float col_w = (total - gap) / 2.0f;
    const float chart_h = 150 * M().scale;

    auto stat = [&](const char* label, const char* value, Status s) {
        ImGui::BeginGroup();
        Small(label, P().text_muted);
        TextAt(F().mono, M().font_body, StatusColor(s) , value);
        ImGui::EndGroup();
    };

    if (BeginCard("##cpu_card", ImVec2(col_w, 0))) {
        CardHeader("Processeur", hwp.cpu.brand.c_str());
        const float w = ImGui::GetContentRegionAvail().x;

        ChartOptions o;
        o.vmin = 0.0f;
        o.vmax = 100.0f;
        o.unit = " %";
        o.x_label = x_label;
        o.status = Status::Accent;
        Chart("cpu", cpu_hist_.values, History::kCapacity, cpu_hist_.offset,
              ImVec2(w, chart_h), o);

        VSpace(M().sp_md);
        if (cpu_mhz_ > 0) std::snprintf(buf, sizeof(buf), "%.0f MHz", cpu_mhz_);
        else              std::snprintf(buf, sizeof(buf), "--");
        stat("Frequence actuelle", buf, Status::Neutral);
        ImGui::SameLine(0, M().sp_xl);
        std::snprintf(buf, sizeof(buf), "%u MHz", hwp.cpu.base_mhz);
        stat("Base", buf, Status::Neutral);
        ImGui::SameLine(0, M().sp_xl);
        std::snprintf(buf, sizeof(buf), "%u / %u", hwp.cpu.physical_cores, hwp.cpu.logical_cores);
        stat("Coeurs", buf, Status::Neutral);
        ImGui::SameLine(0, M().sp_xl);
        std::snprintf(buf, sizeof(buf), "%u", snapshot_.process_count);
        stat("Processus", buf, Status::Neutral);

        VSpace(M().sp_sm);
        if (cpu_temp_ > 0) {
            std::snprintf(buf, sizeof(buf), "Temperature %.0f °C", cpu_temp_);
            StatusPill(buf, cpu_temp_ > 85 ? Status::Danger
                                           : (cpu_temp_ > 75 ? Status::Warn : Status::Ok));
        } else {
            // Sur AMD, temperature et puissance passent par le SMU : sans HWiNFO
            // il n'existe aucune source usermode. On le dit plutot que d'afficher
            // des tirets sans explication.
            StatusPill("Temperature et puissance : lancez HWiNFO64", Status::Warn);
        }
    }
    EndCard();
    ImGui::SameLine(0, gap);

    if (BeginCard("##gpu_card", ImVec2(col_w, 0))) {
        const hw::GpuInfo* g = hwp.primary_gpu();
        CardHeader("Carte graphique", g ? g->description.c_str() : "aucune");
        const float w = ImGui::GetContentRegionAvail().x;

        ChartOptions o;
        o.vmin = 0.0f;
        o.vmax = 100.0f;
        o.unit = " %";
        o.x_label = x_label;
        o.status = Status::Accent;
        Chart("gpu", gpu_hist_.values, History::kCapacity, gpu_hist_.offset,
              ImVec2(w, chart_h), o);

        VSpace(M().sp_md);
        const bool ok = has_nvml_ && gpu_.valid;
        if (ok) std::snprintf(buf, sizeof(buf), "%u MHz", gpu_.clock_graphics_mhz);
        else    std::snprintf(buf, sizeof(buf), "--");
        stat("Frequence GPU", buf, Status::Neutral);
        ImGui::SameLine(0, M().sp_xl);
        if (ok) std::snprintf(buf, sizeof(buf), "%.1f W", gpu_.power_mw / 1000.0);
        else    std::snprintf(buf, sizeof(buf), "--");
        stat("Puissance", buf, Status::Neutral);
        ImGui::SameLine(0, M().sp_xl);
        if (ok && gpu_.vram_total_bytes) {
            std::snprintf(buf, sizeof(buf), "%.1f / %.1f Go", gpu_.vram_used_bytes / 1073741824.0,
                          gpu_.vram_total_bytes / 1073741824.0);
        } else {
            std::snprintf(buf, sizeof(buf), "--");
        }
        stat("VRAM", buf, Status::Neutral);
        ImGui::SameLine(0, M().sp_xl);
        // Un ventilateur a 0 % n'est pas une absence de mesure : c'est le mode
        // zero-RPM au repos. On affiche la valeur telle quelle.
        if (ok) std::snprintf(buf, sizeof(buf), "%u %%", gpu_.fan_pct);
        else    std::snprintf(buf, sizeof(buf), "--");
        stat("Ventilateur", buf, Status::Neutral);

        VSpace(M().sp_sm);
        if (ok) {
            std::snprintf(buf, sizeof(buf), "Temperature %u °C", gpu_.temperature_c);
            StatusPill(buf, gpu_.temperature_c > 83 ? Status::Danger
                                                    : (gpu_.temperature_c > 75 ? Status::Warn
                                                                               : Status::Ok));
        } else {
            StatusPill("NVML indisponible", Status::Neutral);
        }
    }
    EndCard();
}

// ===========================================================================
// Reglages
// ===========================================================================
// ===========================================================================
// Reglages — questionnaire
//
// Plutot que de demander un « niveau » abstrait, on pose une question par
// reglage en montrant cote a cote ce qu'il apporte et ce qu'il coute. Le
// consentement est donne reglage par reglage, en connaissance de cause.
// ===========================================================================
namespace {

// Hauteur qu'occupera un panneau d'information pour ce texte et cette largeur.
float PanelHeight(const char* text, float width) {
    ImGui::PushFont(F().regular, M().font_small);
    const float body = ImGui::CalcTextSize(text, nullptr, false, width - M().sp_md * 2).y;
    ImGui::PopFont();
    return M().sp_md * 2 + M().font_micro + M().sp_sm + body;
}

// Panneau teinte : un intitule en capitales, puis le texte.
void InfoPanel(const char* label, const char* text, Status s, const ImVec2& size) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList*  dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(pos, pos + size, U32(StatusSubtle(s)), M().r_md);
    dl->AddLine(ImVec2(pos.x, pos.y + M().r_md), ImVec2(pos.x, pos.y + size.y - M().r_md),
                U32(StatusColor(s)), 2.5f * M().scale);

    ImGui::PushFont(F().semibold, M().font_micro);
    dl->AddText(ImVec2(pos.x + M().sp_md, pos.y + M().sp_md), U32(StatusColor(s)), label);
    ImGui::PopFont();

    ImGui::PushFont(F().regular, M().font_small);
    dl->AddText(F().regular, M().font_small,
                ImVec2(pos.x + M().sp_md, pos.y + M().sp_md + M().font_micro + M().sp_sm),
                U32(P().text), text, nullptr, size.x - M().sp_md * 2);
    ImGui::PopFont();

    ImGui::Dummy(size);
}

}  // namespace

namespace {

// Carte de niveau selectionnable. Dessinee a la main pour rester coherente
// avec le reste : ImGui::Selectable ne permet pas cette mise en forme.
bool LevelCard(const char* key, const char* title, const char* summary, const char* detail,
               size_t questions, size_t already, size_t total, Status s, bool selected,
               const ImVec2& size) {
    ImGuiWindow*  win = ImGui::GetCurrentWindow();
    const ImVec2  pos = ImGui::GetCursorScreenPos();
    const ImGuiID id = win->GetID(key);

    ImGui::ItemSize(size);
    if (!ImGui::ItemAdd(ImRect(pos, pos + size), id)) return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(ImRect(pos, pos + size), id, &hovered, &held);
    const float t = Animate(ImHashStr("##sel", 0, id), selected ? 1.0f : (hovered ? 0.4f : 0.0f));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, pos + size, U32(Mix(P().surface1, StatusSubtle(s), t)), M().r_lg);
    dl->AddRect(pos, pos + size, U32(Mix(P().border_subtle, StatusColor(s), t * 0.8f)),
                M().r_lg, 0, M().border + t);

    const float pad = M().sp_lg;
    float       y = pos.y + pad;

    dl->AddCircleFilled(ImVec2(pos.x + pad + 4 * M().scale, y + 8 * M().scale),
                        4.5f * M().scale, U32(StatusColor(s)), 16);

    ImGui::PushFont(F().semibold, M().font_h1);
    dl->AddText(ImVec2(pos.x + pad + 18 * M().scale, y - 2 * M().scale), U32(P().text), title);
    y += ImGui::GetFontSize() + M().sp_sm;
    ImGui::PopFont();

    const float wrap = size.x - pad * 2;

    ImGui::PushFont(F().semibold, M().font_small);
    dl->AddText(F().semibold, M().font_small, ImVec2(pos.x + pad, y), U32(StatusColor(s)),
                summary, nullptr, wrap);
    y += ImGui::CalcTextSize(summary, nullptr, false, wrap).y + M().sp_sm;
    ImGui::PopFont();

    ImGui::PushFont(F().regular, M().font_small);
    dl->AddText(F().regular, M().font_small, ImVec2(pos.x + pad, y), U32(P().text_muted),
                detail, nullptr, wrap);
    ImGui::PopFont();

    // Bas de carte : le nombre de questions ET d'ou il vient. Afficher « 1
    // question » sur un niveau qui compte 9 reglages, sans expliquer que 8 sont
    // deja en place, donne l'impression que l'outil ne fait rien.
    char l1[64], l2[96];
    std::snprintf(l1, sizeof(l1), questions > 1 ? "%zu questions" : "%zu question", questions);
    std::snprintf(l2, sizeof(l2), "%zu des %zu reglages sont deja dans l'etat cible", already,
                  total);

    ImGui::PushFont(F().regular, M().font_micro);
    const float h2 = ImGui::GetFontSize();
    dl->AddText(ImVec2(pos.x + pad, pos.y + size.y - pad - h2), U32(P().text_muted),
                already ? l2 : "aucun n'est deja en place");
    ImGui::PopFont();

    ImGui::PushFont(F().mono, M().font_small);
    const ImVec2 fs = ImGui::CalcTextSize(l1);
    dl->AddText(ImVec2(pos.x + pad, pos.y + size.y - pad - h2 - M().sp_xs - fs.y),
                U32(selected ? StatusColor(s) : P().text_secondary), l1);
    ImGui::PopFont();

    return pressed;
}

}  // namespace

void App::BuildQuiz(OptLevel level) {
    const hw::Profile& p = engine_->hardware();
    quiz_ids_.clear();
    quiz_answers_.clear();
    quiz_already_ = 0;
    quiz_index_ = 0;

    // Ordre : basique d'abord, avance ensuite. Les reglages deja dans l'etat
    // cible ne sont pas proposes — une question dont la reponse ne change rien
    // n'est qu'une corvee de plus.
    const Tier kTiers[] = {Tier::Basic, Tier::Advanced, Tier::Expert};
    for (Tier tier : kTiers) {
        if (level == OptLevel::Basic && tier != Tier::Basic) continue;
        for (const auto& t : engine_->tweaks()) {
            if (t->meta().tier != tier) continue;
            if (!t->available(p)) continue;
            if (t->state() == TweakState::Applied) {
                ++quiz_already_;
                continue;
            }
            quiz_ids_.push_back(t->meta().id);
            quiz_answers_.push_back(Answer::None);
        }
    }
}

// ---------------------------------------------------------------------------
void App::PageTweaksIntro() {
    const hw::Profile& hwp = engine_->hardware();
    const float total = ImGui::GetContentRegionAvail().x;

    // Comptage par niveau : on annonce la longueur reelle du questionnaire,
    // pas un total theorique.
    size_t q_basic = 0, q_all = 0, already = 0, unavailable = 0;
    size_t already_basic = 0, total_basic = 0, total_all = 0;
    for (const auto& t : engine_->tweaks()) {
        if (!t->available(hwp)) { ++unavailable; continue; }
        const bool is_basic = t->meta().tier == Tier::Basic;
        ++total_all;
        if (is_basic) ++total_basic;

        if (t->state() == TweakState::Applied) {
            ++already;
            if (is_basic) ++already_basic;
            continue;
        }
        ++q_all;
        if (is_basic) ++q_basic;
    }

    // --- Avertissement ------------------------------------------------------
    if (BeginCard("##intro_warn", ImVec2(total, 0))) {
        const float inner = ImGui::GetContentRegionAvail().x;
        ImGui::BeginGroup();
        Dot(Status::Warn, 4.0f * M().scale);
        ImGui::SameLine(0, M().sp_sm);
        CardTitle("Avant de continuer");
        ImGui::EndGroup();
        VSpace(M().sp_sm);
        WrappedMuted("Tuneforge modifie des reglages du systeme. L'etat d'origine de chaque "
                     "reglage est capture avant d'etre modifie et reste restaurable a tout "
                     "moment, mais la responsabilite de la modification vous revient.",
                     inner);
        VSpace(M().sp_sm);
        ImGui::BeginGroup();
        StatusPill("Aucune tension ni frequence materielle", Status::Ok);
        ImGui::SameLine(0, M().sp_xs);
        StatusPill("Aucun driver noyau", Status::Ok);
        ImGui::SameLine(0, M().sp_xs);
        StatusPill("Rien n'est applique avant votre validation finale", Status::Ok);
        ImGui::EndGroup();
    }
    EndCard();

    VSpace(M().sp_lg);
    SectionLabel("Choisissez votre niveau d'optimisation");
    VSpace(M().sp_xs);
    WrappedMuted("Le questionnaire ne pose que les questions utiles : un reglage deja dans "
                 "l'etat voulu sur cette machine n'est pas propose, puisque repondre ne "
                 "changerait rien.",
                 total);
    VSpace(M().sp_sm);

    // --- Les deux niveaux ---------------------------------------------------
    const float gap = M().sp_md;
    const float cw = (total - gap) / 2.0f;
    const float ch = 226 * M().scale;

    if (LevelCard("##lvl_basic", "Basique",
                  "Aucun risque, aucun effet de bord.",
                  "Uniquement les reglages Windows qui n'ont aucune contrepartie notable : "
                  "ni consommation, ni bruit, ni compatibilite. Questionnaire court. "
                  "C'est le niveau a prendre si vous ne connaissez pas la machine.",
                  q_basic, already_basic, total_basic, Status::Ok,
                  level_ == OptLevel::Basic, ImVec2(cw, ch))) {
        level_ = OptLevel::Basic;
    }
    ImGui::SameLine(0, gap);

    if (LevelCard("##lvl_expert", "Expert",
                  "Tous les reglages, contreparties comprises.",
                  "Ajoute ceux qui echangent quelque chose contre de la reactivite : "
                  "consommation, chaleur, bruit, compatibilite avec certains outils de "
                  "capture. Questionnaire plus long, mais vous tranchez chaque cas en "
                  "voyant ce qu'il coute.",
                  q_all, already, total_all, Status::Warn,
                  level_ == OptLevel::Expert, ImVec2(cw, ch))) {
        level_ = OptLevel::Expert;
    }

    VSpace(M().sp_lg);

    // --- Depart --------------------------------------------------------------
    if (BeginCard("##intro_start", ImVec2(total, 0))) {
        const float inner = ImGui::GetContentRegionAvail().x;
        const size_t count = (level_ == OptLevel::Basic) ? q_basic : q_all;

        WrappedMuted("Une question par reglage : ce qu'il apporte a gauche, ce qu'il coute a "
                     "droite, et vous repondez oui ou non. Vous relirez tous vos choix avant "
                     "que quoi que ce soit ne soit applique.",
                     inner);

        VSpace(M().sp_md);
        ImGui::BeginGroup();
        char line[128];
        std::snprintf(line, sizeof(line), count > 1 ? "%zu questions" : "%zu question", count);
        StatusPill(line, Status::Accent);
        if (already) {
            ImGui::SameLine(0, M().sp_xs);
            std::snprintf(line, sizeof(line), "%zu deja dans l'etat cible", already);
            StatusPill(line, Status::Ok);
        }
        if (unavailable) {
            ImGui::SameLine(0, M().sp_xs);
            std::snprintf(line, sizeof(line), "%zu indisponibles ici", unavailable);
            StatusPill(line, Status::Neutral);
        }
        ImGui::EndGroup();

        VSpace(M().sp_lg);
        std::snprintf(line, sizeof(line),
                      count > 1 ? "Commencer  —  %zu questions" : "Commencer  —  %zu question",
                      count);
        if (PrimaryButton(line, ImVec2(0, 0), count > 0)) {
            BuildQuiz(level_);
            flow_ = TweakFlow::Question;
        }
        if (count == 0) {
            VSpace(M().sp_sm);
            Small("Tous les reglages de ce niveau sont deja dans l'etat cible.", P().ok);
        }
    }
    EndCard();
}

// ---------------------------------------------------------------------------
void App::PageTweaksQuiz() {
    if (quiz_index_ >= quiz_ids_.size()) {
        flow_ = TweakFlow::Recap;
        return;
    }

    const hw::Profile& hwp = engine_->hardware();
    const float total = ImGui::GetContentRegionAvail().x;

    ITweak* t = engine_->find(quiz_ids_[quiz_index_]);
    if (!t) {  // catalogue modifie entre-temps : on saute
        ++quiz_index_;
        return;
    }
    const TweakMeta& m = t->meta();

    // --- Progression --------------------------------------------------------
    char prog[64];
    std::snprintf(prog, sizeof(prog), "Question %zu sur %zu", quiz_index_ + 1,
                  quiz_ids_.size());
    ImGui::BeginGroup();
    SectionLabel(prog);
    ImGui::EndGroup();
    VSpace(M().sp_xs);
    // Progression sur la question EN COURS : a la premiere question, une barre
    // vide donnerait l'impression que l'element est casse.
    ProgressBar(static_cast<float>(quiz_index_ + 1) / static_cast<float>(quiz_ids_.size()),
                ImVec2(total, 6 * M().scale), Status::Accent);
    VSpace(M().sp_lg);

    // --- La question --------------------------------------------------------
    if (BeginCard("##question", ImVec2(total, 0))) {
        const float inner = ImGui::GetContentRegionAvail().x;

        ImGui::BeginGroup();
        StatusPill(m.tier == Tier::Basic ? "basique"
                                         : (m.tier == Tier::Advanced ? "avance" : "expert"),
                   tier_status(m.tier));
        if (m.needs_reboot) {
            ImGui::SameLine(0, M().sp_xs);
            Badge("redemarrage requis", P().warn, P().warn_subtle);
        }
        ImGui::EndGroup();

        VSpace(M().sp_sm);
        H1(m.title.c_str());
        VSpace(M().sp_xs);
        WrappedMuted(m.what.c_str(), inner);

        VSpace(M().sp_lg);

        // Gain et contrepartie cote a cote, a hauteur egale : ni l'un ni
        // l'autre ne doit paraitre secondaire.
        const char* gain = m.why.c_str();
        const char* cost = m.caveat.empty()
                               ? "Aucune contrepartie connue sur une machine de bureau."
                               : m.caveat.c_str();
        const float gap = M().sp_md;
        const float half = (inner - gap) / 2.0f;
        const float h = (std::max)(PanelHeight(gain, half), PanelHeight(cost, half));

        InfoPanel("CE QUE CA APPORTE", gain, Status::Ok, ImVec2(half, h));
        ImGui::SameLine(0, gap);
        InfoPanel("CE QUE CA COUTE", cost, Status::Warn, ImVec2(half, h));

        VSpace(M().sp_lg);
        ImGui::BeginGroup();
        Small("Actuel", P().text_disabled);
        Mono(t->current_value().c_str(), P().text_secondary);
        ImGui::EndGroup();
        ImGui::SameLine(0, M().sp_xl);
        ImGui::BeginGroup();
        Small("Apres application", P().text_disabled);
        Mono(t->target_value().c_str(), P().accent);
        ImGui::EndGroup();

        VSpace(M().sp_lg);
        if (PrimaryButton("Oui, je veux ce reglage")) {
            quiz_answers_[quiz_index_] = Answer::Yes;
            ++quiz_index_;
        }
        ImGui::SameLine(0, M().sp_sm);
        if (GhostButton("Non, laisser tel quel")) {
            quiz_answers_[quiz_index_] = Answer::No;
            ++quiz_index_;
        }
        if (quiz_index_ > 0) {
            ImGui::SameLine(0, M().sp_xl);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + M().sp_sm);
            if (ImGui::InvisibleButton("##back", ImGui::CalcTextSize("Question precedente"))) {
                --quiz_index_;
            }
            const bool hov = ImGui::IsItemHovered();
            ImGui::SetCursorScreenPos(ImGui::GetItemRectMin());
            Small("Question precedente", hov ? P().accent : P().text_disabled);
        }
    }
    EndCard();

    (void)hwp;
}

// ---------------------------------------------------------------------------
void App::PageTweaksRecap() {
    const float total = ImGui::GetContentRegionAvail().x;

    std::vector<std::string> chosen, refused;
    for (size_t i = 0; i < quiz_ids_.size(); ++i) {
        if (quiz_answers_[i] == Answer::Yes) chosen.push_back(quiz_ids_[i]);
        else if (quiz_answers_[i] == Answer::No) refused.push_back(quiz_ids_[i]);
    }

    bool needs_reboot = false;
    for (const auto& id : chosen) {
        if (const ITweak* t = engine_->find(id)) needs_reboot |= t->meta().needs_reboot;
    }

    if (BeginCard("##recap", ImVec2(total, 0))) {
        const float inner = ImGui::GetContentRegionAvail().x;
        CardHeader("Recapitulatif", "Rien n'a encore ete applique.");

        char line[96];
        ImGui::BeginGroup();
        std::snprintf(line, sizeof(line), "%zu retenus", chosen.size());
        StatusPill(line, chosen.empty() ? Status::Neutral : Status::Ok);
        ImGui::SameLine(0, M().sp_xs);
        std::snprintf(line, sizeof(line), "%zu ecartes", refused.size());
        StatusPill(line, Status::Neutral);
        if (needs_reboot) {
            ImGui::SameLine(0, M().sp_xs);
            StatusPill("redemarrage necessaire", Status::Warn);
        }
        ImGui::EndGroup();

        if (!chosen.empty()) {
            VSpace(M().sp_lg);
            SectionLabel("A appliquer");
            VSpace(M().sp_xs);
            for (const auto& id : chosen) {
                const ITweak* t = engine_->find(id);
                ImGui::BeginGroup();
                Dot(Status::Ok, 3.0f * M().scale);
                ImGui::SameLine(0, M().sp_sm);
                Body(t ? t->meta().title.c_str() : id.c_str());
                ImGui::EndGroup();
            }
        }

        if (!refused.empty()) {
            VSpace(M().sp_lg);
            SectionLabel("Ecartes");
            VSpace(M().sp_xs);
            for (const auto& id : refused) {
                const ITweak* t = engine_->find(id);
                ImGui::BeginGroup();
                Dot(Status::Neutral, 3.0f * M().scale, false);
                ImGui::SameLine(0, M().sp_sm);
                Small(t ? t->meta().title.c_str() : id.c_str(), P().text_muted);
                ImGui::EndGroup();
            }
        }

        VSpace(M().sp_lg);
        std::snprintf(line, sizeof(line), "Appliquer les %zu reglages retenus", chosen.size());
        if (PrimaryButton(line, ImVec2(0, 0), !chosen.empty())) {
            // Le questionnaire a montre la contrepartie de chaque reglage :
            // le consentement a deja ete donne, reglage par reglage.
            ApplySelection(chosen, true, true);
            flow_ = TweakFlow::Done;
        }
        ImGui::SameLine(0, M().sp_sm);
        if (GhostButton("Revoir le questionnaire")) {
            quiz_index_ = 0;
            flow_ = TweakFlow::Question;
        }

        if (!elevated_) {
            VSpace(M().sp_md);
            WrappedMuted("Tuneforge n'est pas lance en administrateur : l'application "
                         "echouera. Relancez-le en tant qu'administrateur avant de valider.",
                         inner);
        }
    }
    EndCard();
}

void App::PageTweaks() {
    switch (flow_) {
        case TweakFlow::Intro:    PageTweaksIntro(); break;
        case TweakFlow::Question: PageTweaksQuiz();  break;
        case TweakFlow::Recap:    PageTweaksRecap(); break;
        case TweakFlow::Done:     PageTweaksDone();  break;
    }
}

// ---------------------------------------------------------------------------
void App::PageTweaksDone() {
    const float total = ImGui::GetContentRegionAvail().x;

    if (BeginCard("##done", ImVec2(total, 0))) {
        const float inner = ImGui::GetContentRegionAvail().x;
        const bool  ok = last_outcome_.failed.empty();

        ImGui::BeginGroup();
        Dot(ok ? Status::Ok : Status::Danger, 4.0f * M().scale);
        ImGui::SameLine(0, M().sp_sm);
        CardTitle(ok ? "Termine" : "Termine avec des erreurs");
        ImGui::EndGroup();

        VSpace(M().sp_sm);
        char line[128];
        std::snprintf(line, sizeof(line),
                      last_outcome_.applied.size() > 1 ? "%zu reglages appliques"
                                                       : "%zu reglage applique",
                      last_outcome_.applied.size());
        StatusPill(line, last_outcome_.applied.empty() ? Status::Neutral : Status::Ok);

        if (!last_outcome_.applied.empty()) {
            VSpace(M().sp_lg);
            SectionLabel("Appliques");
            VSpace(M().sp_xs);
            for (const auto& id : last_outcome_.applied) {
                const ITweak* t = engine_->find(id);
                ImGui::BeginGroup();
                Dot(Status::Ok, 3.0f * M().scale);
                ImGui::SameLine(0, M().sp_sm);
                Body(t ? t->meta().title.c_str() : id.c_str());
                ImGui::EndGroup();
            }
        }

        if (!last_outcome_.failed.empty()) {
            VSpace(M().sp_lg);
            SectionLabel("En echec");
            VSpace(M().sp_xs);
            for (const auto& f : last_outcome_.failed) {
                ImGui::BeginGroup();
                Dot(Status::Danger, 3.0f * M().scale);
                ImGui::SameLine(0, M().sp_sm);
                WrappedMuted(f.c_str(), inner - M().sp_xl);
                ImGui::EndGroup();
            }
        }

        if (last_outcome_.needs_reboot) {
            VSpace(M().sp_md);
            StatusPill("Redemarrez pour que tout prenne effet", Status::Warn);
        }

        VSpace(M().sp_lg);
        WrappedMuted("Tout ce qui vient d'etre applique est restaurable depuis l'onglet "
                     "Restauration.",
                     inner);

        VSpace(M().sp_md);
        if (PrimaryButton("Voir ce qui est restaurable")) {
            page_ = Page::Restore;
        }
        ImGui::SameLine(0, M().sp_sm);
        if (GhostButton("Refaire le questionnaire")) {
            flow_ = TweakFlow::Intro;
        }
    }
    EndCard();
}

// ===========================================================================
// GPU (v0.3)
//
// Tous les reglages de cette page sont VOLATILES : ils disparaissent au
// redemarrage et au rechargement du pilote. C'est pour cela qu'ils ne passent
// pas par le catalogue de reglages, dont les instantanes sont persistes : un
// etat sur disque qui pretendrait avoir modifie quelque chose deja revenu tout
// seul serait mensonger.
// ===========================================================================
namespace {

// Curseur aux couleurs du theme, avec son intitule au-dessus.
bool ThemedSlider(const char* id, const char* label, int* value, int vmin, int vmax,
                  const char* fmt, float width) {
    ImGui::BeginGroup();
    Small(label, P().text_muted);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, P().sunken);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, P().surface2);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, P().surface3);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, P().accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, P().accent_hover);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(M().sp_md, M().sp_sm));
    ImGui::PushFont(F().mono, M().font_body);
    ImGui::SetNextItemWidth(width);
    const bool changed = ImGui::SliderInt(id, value, vmin, vmax, fmt);
    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);
    ImGui::EndGroup();
    return changed;
}

}  // namespace

// ---------------------------------------------------------------------------
// Ventilateurs : mode et courbe
// ---------------------------------------------------------------------------
void App::PageGpuFans(float width) {
    const hw::GpuReading& g = gpu_ctl_->gpu(0);
    const bool can_write = elevated_ && gpu_ctl_->capabilities().set_fan;
    char buf[96];

    if (!BeginCard("##gpu_fan", ImVec2(width, 0))) { EndCard(); return; }
    const float inner = ImGui::GetContentRegionAvail().x;

    char sub[128];
    std::snprintf(sub, sizeof(sub),
                  "%s ne confie pas de courbe au pilote : Tuneforge la tient lui-meme.",
                  gpu_ctl_->backend_name());
    CardHeader("Ventilateurs", sub);

    // --- Mode ---------------------------------------------------------------
    const float mw = (std::min)(160.0f * M().scale, (inner - M().sp_sm * 2) / 3.0f);
    if (ModeButton("Pilote", fan_mode_ == FanMode::Driver, mw)) {
        fan_mode_ = FanMode::Driver;
        ReleaseFans();
        SaveUiPrefs();
    }
    ImGui::SameLine(0, M().sp_sm);
    if (ModeButton("Niveau fixe", fan_mode_ == FanMode::Fixed, mw)) {
        fan_mode_ = FanMode::Fixed;
        SaveUiPrefs();
    }
    ImGui::SameLine(0, M().sp_sm);
    if (ModeButton("Courbe", fan_mode_ == FanMode::Curve, mw)) {
        fan_mode_ = FanMode::Curve;
        fan_curve_written_ = -1;   // force une premiere ecriture
        SaveUiPrefs();
    }

    VSpace(M().sp_md);

    // --- Etat courant, sur une ligne ----------------------------------------
    std::string state;
    for (const auto& c : g.coolers.items) {
        if (!state.empty()) state += "   ";
        std::snprintf(buf, sizeof(buf), "ventilateur %u : %u %%", c.index, c.current_level);
        state += buf;
    }
    if (g.coolers.tach_valid && g.coolers.tach_rpm > 0) {
        std::snprintf(buf, sizeof(buf), "   %u tr/min", g.coolers.tach_rpm);
        state += buf;
    }
    if (!state.empty()) Mono(state.c_str(), P().text_secondary);

    // Le defaut d elevation est deja annonce par le bandeau en haut de page :
    // le repeter ici ne ferait que doubler le meme message.
    if (elevated_ && !gpu_ctl_->capabilities().set_fan) {
        VSpace(M().sp_sm);
        Small("Ecriture des ventilateurs indisponible sur cette carte.", P().warn);
    }

    VSpace(M().sp_md);

    switch (fan_mode_) {
        case FanMode::Driver:
            WrappedMuted("Le pilote NVIDIA gere seul la vitesse, comme sans Tuneforge. "
                         "C'est le mode a garder si vous n'avez pas de raison precise "
                         "d'en changer.",
                         inner);
            break;

        case FanMode::Fixed: {
            ThemedSlider("##fan", "Niveau", &fan_target_, 30, 100, "%d %%",
                         (std::min)(inner, 320 * M().scale));
            VSpace(M().sp_sm);
            Small("Le plancher est a 30 % : en dessous, la carte refuse la consigne.",
                  P().text_muted);
            VSpace(M().sp_md);
            ImGui::BeginDisabled(!can_write);
            if (PrimaryButton("Appliquer##fan")) {
                Result r = gpu_ctl_->set_fan_level_pct(0, static_cast<uint32_t>(fan_target_));
                fans_taken_ = fans_taken_ || static_cast<bool>(r);
                Notify(r ? std::format("Ventilateurs a {} %.", fan_target_)
                         : "Ventilateurs : " + r.message,
                       r ? Status::Ok : Status::Danger);
            }
            ImGui::EndDisabled();
            break;
        }

        case FanMode::Curve: {
            const float side = (std::min)(240.0f * M().scale, inner * 0.34f);
            const float cw = inner - side - M().sp_lg;

            FanCurveView view;
            view.live_temp_c = g.thermal.valid ? static_cast<float>(g.thermal.gpu_c) : -1.0f;
            view.editable = true;

            ImGui::BeginGroup();
            if (FanCurveEditor("##fancurve", fan_curve_.data(),
                               static_cast<int>(fan_curve_.size()),
                               ImVec2(cw, 170 * M().scale), view)) {
                fan_curve_written_ = -1;   // la consigne a change : reecrire
                SaveUiPrefs();
            }
            VSpace(M().sp_xs);
            Small("Temperature du GPU en abscisse, niveau en ordonnee. "
                  "Faites glisser les points.",
                  P().text_muted);
            ImGui::EndGroup();

            ImGui::SameLine(0, M().sp_lg);

            ImGui::BeginGroup();
            const int target = g.thermal.valid
                                   ? FanCurveLevelAt(fan_curve_.data(),
                                                     static_cast<int>(fan_curve_.size()),
                                                     static_cast<float>(g.thermal.gpu_c))
                                   : -1;
            Small("VISE PAR LA COURBE", P().text_muted);
            if (target >= 0) {
                std::snprintf(buf, sizeof(buf), "%d %%", target);
                TextAt(F().semibold, M().font_h1, P().text, buf);
            } else {
                TextAt(F().semibold, M().font_h1, P().text_disabled, "--");
            }

            VSpace(M().sp_sm);
            Small("DERNIER NIVEAU ECRIT", P().text_muted);
            const std::string blocker = FanCurveBlocker();
            if (fan_curve_written_ >= 0) {
                std::snprintf(buf, sizeof(buf), "%d %%", fan_curve_written_);
                Mono(buf, P().text_secondary);
            } else if (blocker.empty()) {
                Mono("en attente", P().text_disabled);
            } else {
                Mono("aucun", P().text_disabled);
            }
            if (!blocker.empty()) {
                VSpace(M().sp_xs);
                WrappedMuted(blocker.c_str(), side);
            }

            VSpace(M().sp_md);
            if (GhostButton("Courbe par defaut", ImVec2(side, 0))) {
                fan_curve_ = default_fan_curve();
                fan_curve_written_ = -1;
                SaveUiPrefs();
                Notify("Courbe remise a sa forme d'origine.", Status::Ok);
            }
            ImGui::EndGroup();
            break;
        }
    }

    EndCard();
}

// ---------------------------------------------------------------------------
// Application de la courbe
//
// Le pilote ne connait que des niveaux fixes : la courbe n'existe que tant que
// Tuneforge tourne. On relit donc la temperature et on reecrit le niveau — mais
// seulement quand l'ecart le justifie, sinon chaque image traverserait le
// pilote pour rien.
// ---------------------------------------------------------------------------
std::string App::FanCurveBlocker() const {
    if (!gpu_ctl_ || gpu_ctl_->gpu_count() == 0) return "Aucun controleur GPU.";
    if (!elevated_) {
        return "Droits administrateur requis : le pilote refuse la consigne sans eux.";
    }
    if (!gpu_ctl_->capabilities().set_fan) {
        return "Cette carte n'expose pas de pilotage des ventilateurs.";
    }
    if (fan_curve_.size() < 2) return "Courbe incomplete.";
    if (!gpu_ctl_->gpu(0).thermal.valid) {
        return "Temperature du GPU illisible : la courbe n'a rien sur quoi s'appuyer.";
    }
    return {};
}

void App::ApplyFanCurve() {
    if (fan_mode_ != FanMode::Curve) return;
    if (!FanCurveBlocker().empty()) return;

    const hw::GpuReading& g = gpu_ctl_->gpu(0);

    const double now = ImGui::GetTime();
    if (now - last_curve_ < 2.0) return;   // deux secondes suffisent a suivre
    last_curve_ = now;

    const int target = FanCurveLevelAt(fan_curve_.data(), static_cast<int>(fan_curve_.size()),
                                       static_cast<float>(g.thermal.gpu_c));

    // Hysteresis : sous trois points d'ecart, on laisse le ventilateur
    // tranquille. Suivre la courbe au point pres le ferait chanter.
    if (fan_curve_written_ >= 0 && std::abs(target - fan_curve_written_) < 3) return;

    Result r = gpu_ctl_->set_fan_level_pct(0, static_cast<uint32_t>(target));
    if (r) {
        fan_curve_written_ = target;
        fans_taken_ = true;
    } else {
        // Une consigne refusee ne doit pas etre retentee toutes les deux
        // secondes en silence : on sort du mode courbe et on le dit.
        log_warn("courbe ventilateur abandonnee : {}", r.message);
        fan_mode_ = FanMode::Driver;
        Notify("Courbe abandonnee : " + r.message, Status::Danger);
        ReleaseFans();
    }
}

void App::ReleaseFans() {
    if (!fans_taken_) return;
    if (!gpu_ctl_ || gpu_ctl_->gpu_count() == 0) return;
    Result r = gpu_ctl_->set_fan_auto(0);
    if (r) {
        log_info("ventilateurs rendus au pilote");
        fans_taken_ = false;
        fan_curve_written_ = -1;
    } else {
        log_error("ventilateurs non rendus au pilote : {}", r.message);
    }
}

App::~App() {
    // Sans cela, fermer la fenetre laisserait les ventilateurs bloques au
    // dernier niveau ecrit — y compris apres la fin du processus.
    ReleaseFans();
}

// ---------------------------------------------------------------------------
void App::PageGpu() {
    const float total = ImGui::GetContentRegionAvail().x;

    if (!gpu_ctl_ || gpu_ctl_->gpu_count() == 0) {
        if (BeginCard("##nogpu", ImVec2(total, 0))) {
            const float inner = ImGui::GetContentRegionAvail().x;
            CardHeader("Aucun reglage GPU disponible");
            // La raison exacte, pas un message generique : sur une machine
            // qu'on n'a pas sous la main, c'est la seule chose qui permettra
            // de comprendre ce qui manque.
            WrappedMuted(gpu_absent_.empty()
                             ? "Aucun controleur GPU n'a pu etre initialise."
                             : gpu_absent_.c_str(),
                         inner);
        }
        EndCard();
        return;
    }

    const hw::GpuReading& g = gpu_ctl_->gpu(0);
    char buf[96];

    // --- Elevation ----------------------------------------------------------
    // Avec le manifeste « requireAdministrator », ce bandeau ne devrait jamais
    // apparaitre. Il reste la pour la seule configuration ou l'app peut
    // demarrer sans droits : un binaire dont le manifeste a ete retire.
    if (!elevated_) {
        static const char* kRelaunch = "Relancer en administrateur";
        ImGui::BeginGroup();
        StatusPill("Lecture seule — elevation requise pour ecrire", Status::Warn);
        ImGui::SameLine(0, M().sp_md);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - M().sp_xs);
        // Largeur explicite : laissee au calcul automatique, l'etiquette
        // debordait de son cadre a certaines echelles.
        ImGui::PushFont(F().semibold, M().font_body);
        const float bw = ImGui::CalcTextSize(kRelaunch).x + M().sp_lg * 2 + M().sp_sm;
        ImGui::PopFont();
        if (GhostButton(kRelaunch, ImVec2(bw, 0))) {
            if (relaunch_elevated({"--page", "gpu"}, false)) window_->Close();
        }
        ImGui::EndGroup();
        VSpace(M().sp_md);
    }

    // --- Mesures ------------------------------------------------------------
    const float gap = M().sp_md;
    const float tile_w = (total - gap * 3) / 4.0f;
    const float tile_h = 84 * M().scale;

    if (g.thermal.valid) {
        std::snprintf(buf, sizeof(buf), "%d", g.thermal.gpu_c);
        MetricTile("TEMPERATURE", buf, "°C", g.thermal.gpu_c / 90.0f,
                   g.thermal.gpu_c > 83 ? Status::Danger
                                        : (g.thermal.gpu_c > 75 ? Status::Warn : Status::Ok),
                   ImVec2(tile_w, tile_h));
    } else {
        MetricTile("TEMPERATURE", "--", "", -1.0f, Status::Neutral, ImVec2(tile_w, tile_h));
    }
    ImGui::SameLine(0, gap);

    if (g.voltage.valid) {
        std::snprintf(buf, sizeof(buf), "%.3f", g.voltage.volts());
        MetricTile("TENSION", buf, "V", g.voltage.volts() / 1.1f, Status::Accent,
                   ImVec2(tile_w, tile_h));
    } else {
        MetricTile("TENSION", "--", "", -1.0f, Status::Neutral, ImVec2(tile_w, tile_h));
    }
    ImGui::SameLine(0, gap);

    if (has_nvml_ && gpu_.valid) {
        std::snprintf(buf, sizeof(buf), "%.0f", gpu_.power_mw / 1000.0);
        MetricTile("PUISSANCE", buf, "W", (gpu_.power_mw / 1000.0f) / 275.0f, Status::Accent,
                   ImVec2(tile_w, tile_h));
    } else {
        MetricTile("PUISSANCE", "--", "", -1.0f, Status::Neutral, ImVec2(tile_w, tile_h));
    }
    ImGui::SameLine(0, gap);

    if (g.clocks.valid) {
        std::snprintf(buf, sizeof(buf), "%u", g.clocks.graphics_khz / 1000);
        MetricTile("FREQUENCE GPU", buf, "MHz", (g.clocks.graphics_khz / 1000.0f) / 3000.0f,
                   Status::Accent, ImVec2(tile_w, tile_h));
    } else {
        MetricTile("FREQUENCE GPU", "--", "", -1.0f, Status::Neutral, ImVec2(tile_w, tile_h));
    }

    VSpace(M().sp_sm);

    // Une ligne de texte discrete plutot que trois pastilles : ces trois faits
    // sont du contexte permanent, ils n'ont pas a peser autant qu'un etat.
    const std::string drv = gpu_ctl_->driver_version();
    std::snprintf(buf, sizeof(buf), "Volatile, perdu au redemarrage   ·   Sans driver noyau"
                                    "   ·   %s %s",
                  gpu_ctl_->backend_name(), drv.empty() ? "(version inconnue)" : drv.c_str());
    Small(buf, P().text_muted);

    VSpace(gap);

    // --- Ventilateurs --------------------------------------------------------
    PageGpuFans(total);

    VSpace(gap);
    const float col_w = (total - gap) / 2.0f;
    const bool  can_write = elevated_;

    // --- Limite de puissance -------------------------------------------------
    if (BeginCard("##gpu_power", ImVec2(col_w, 0))) {
        const float inner = ImGui::GetContentRegionAvail().x;
        CardHeader("Limite de puissance",
                   "Le plafond que la carte s'autorise, en pourcentage du defaut.");

        if (!g.power.valid) {
            WrappedMuted("Non lisible sur cette carte.", inner);
        } else {
            const int vmin = static_cast<int>(g.power.min_pct);
            const int vmax = static_cast<int>(g.power.max_pct);
            ThemedSlider("##pl", "Limite", &power_target_, vmin, vmax, "%d %%", inner);

            VSpace(M().sp_sm);
            std::snprintf(buf, sizeof(buf), "actuelle %.0f %%  ·  defaut %.0f %%",
                          g.power.current_pct, g.power.default_pct);
            Small(buf, P().text_muted);

            VSpace(M().sp_md);
            ImGui::BeginDisabled(!can_write);
            if (PrimaryButton("Appliquer##pl")) {
                Result r = gpu_ctl_->set_power_limit_pct(0,
                                                         static_cast<float>(power_target_));
                Notify(r ? std::format("Limite de puissance a {} %.", power_target_)
                         : "Limite de puissance : " + r.message,
                       r ? Status::Ok : Status::Danger);
            }
            ImGui::SameLine(0, M().sp_sm);
            if (GhostButton("Defaut##pl")) {
                Result r = gpu_ctl_->set_power_limit_pct(0, g.power.default_pct);
                power_target_ = static_cast<int>(g.power.default_pct);
                Notify(r ? "Limite de puissance revenue au defaut."
                         : "Limite de puissance : " + r.message,
                       r ? Status::Ok : Status::Danger);
            }
            ImGui::EndDisabled();
        }
    }
    EndCard();
    ImGui::SameLine(0, gap);

    // --- Decalages d'horloge --------------------------------------------------
    if (BeginCard("##gpu_clocks", ImVec2(col_w, 0))) {
        const float inner = ImGui::GetContentRegionAvail().x;
        CardHeader("Decalages d'horloge", "Un delta ajoute a la courbe d'origine.");

        if (!g.offsets.valid) {
            WrappedMuted("Non lisibles sur cette carte.", inner);
        } else {
            ThemedSlider("##core", "Coeur graphique", &core_target_,
                         g.offsets.graphics_min_khz / 1000, g.offsets.graphics_max_khz / 1000,
                         "%+d MHz", inner);
            VSpace(M().sp_sm);
            ThemedSlider("##mem", "Memoire", &mem_target_, g.offsets.memory_min_khz / 1000,
                         g.offsets.memory_max_khz / 1000, "%+d MHz", inner);

            VSpace(M().sp_sm);
            std::snprintf(buf, sizeof(buf), "appliques : coeur %+d MHz  ·  memoire %+d MHz",
                          g.offsets.graphics_delta_khz / 1000, g.offsets.memory_delta_khz / 1000);
            Small(buf, P().text_muted);

            VSpace(M().sp_md);
            ImGui::BeginDisabled(!can_write);
            if (PrimaryButton("Appliquer##clk")) {
                Result rc = gpu_ctl_->set_clock_offset_mhz(
                    0, hw::GpuClockDomain::Graphics, core_target_);
                Result rm = gpu_ctl_->set_clock_offset_mhz(
                    0, hw::GpuClockDomain::Memory, mem_target_);
                if (rc && rm) {
                    Notify(std::format("Decalages appliques : coeur {:+} MHz, memoire {:+} MHz.",
                                       core_target_, mem_target_),
                           Status::Ok);
                } else {
                    Notify("Decalages : " + (rc ? rm.message : rc.message), Status::Danger);
                }
            }
            ImGui::SameLine(0, M().sp_sm);
            if (GhostButton("Remettre a zero##clk")) {
                gpu_ctl_->set_clock_offset_mhz(0, hw::GpuClockDomain::Graphics, 0);
                gpu_ctl_->set_clock_offset_mhz(0, hw::GpuClockDomain::Memory, 0);
                core_target_ = 0;
                mem_target_ = 0;
                Notify("Decalages remis a zero.", Status::Ok);
            }
            ImGui::EndDisabled();
        }
    }
    EndCard();
}

// ===========================================================================
// Restauration
//
// Une page dediee plutot qu'un bouton perdu dans un catalogue : la promesse
// centrale de l'outil est d'etre reversible, elle doit etre visible en
// permanence, y compris — et surtout — quand il n'y a rien a restaurer.
// ===========================================================================
void App::PageRestore() {
    const float total = ImGui::GetContentRegionAvail().x;
    const auto  ids = engine_->state().applied_ids();

    if (BeginCard("##restore_head", ImVec2(total, 0))) {
        const float inner = ImGui::GetContentRegionAvail().x;
        char line[128];
        std::snprintf(line, sizeof(line),
                      ids.size() > 1 ? "%zu reglages modifies par Tuneforge"
                                     : "%zu reglage modifie par Tuneforge",
                      ids.size());
        CardHeader(line, "Chacun peut etre remis dans son etat d'origine.");

        if (ids.empty()) {
            WrappedMuted("Tuneforge n'a rien modifie sur cette machine. Les reglages deja "
                         "actifs le sont du fait de votre configuration Windows, pas de cet "
                         "outil : il ne les revendique pas et ne propose donc pas de les "
                         "annuler.",
                         inner);
        } else {
            if (DangerButton("Tout restaurer", ImVec2(0, 0), elevated_)) {
                Engine::Outcome o = engine_->revert_all();
                char msg[160];
                std::snprintf(msg, sizeof(msg), "%zu reglage(s) restaure(s).",
                              o.applied.size());
                Notify(msg, o.failed.empty() ? Status::Ok : Status::Danger);
                RefreshLog();
            }
            if (!elevated_) {
                ImGui::SameLine(0, M().sp_md);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + M().sp_sm);
                Small("elevation requise", P().warn);
            }
        }
    }
    EndCard();

    // --- Detail, un reglage par ligne ---------------------------------------
    for (const auto& id : ids) {
        ITweak* t = engine_->find(id);
        if (!t) continue;
        VSpace(M().sp_sm);
        ImGui::PushID(id.c_str());
        if (BeginCard("##item", ImVec2(total, 0))) {
            const float inner = ImGui::GetContentRegionAvail().x;
            ImGui::BeginGroup();
            CardTitle(t->meta().title.c_str());
            Mono(id.c_str(), P().text_disabled);
            ImGui::EndGroup();

            VSpace(M().sp_sm);
            ImGui::BeginGroup();
            Small("Actuel", P().text_disabled);
            Mono(t->current_value().c_str(), P().text_secondary);
            ImGui::EndGroup();
            ImGui::SameLine(0, M().sp_xl);
            ImGui::BeginGroup();
            Small("Etat", P().text_disabled);
            StatusPill(to_string(t->state()), status_of(t->state()));
            ImGui::EndGroup();

            VSpace(M().sp_md);
            if (DangerButton("Restaurer ce reglage", ImVec2(0, 0), elevated_)) {
                Engine::Outcome o = engine_->revert_ids({id});
                Notify(o.failed.empty() ? t->meta().title + " : restaure"
                                        : t->meta().title + " : " + o.failed.front(),
                       o.failed.empty() ? Status::Ok : Status::Danger);
                RefreshLog();
            }
            (void)inner;
        }
        EndCard();
        ImGui::PopID();
    }

    // --- Filet de secours ----------------------------------------------------
    VSpace(M().sp_lg);
    if (BeginCard("##restore_tool", ImVec2(total, 0))) {
        const float inner = ImGui::GetContentRegionAvail().x;
        CardHeader("Si Tuneforge ne demarre plus");
        WrappedMuted("tuneforge-reset.exe, livre a cote de l'application, restaure tout sans "
                     "lire la moindre configuration et sans dependre de cette interface. "
                     "Il ne pose qu'une question.",
                     inner);
        VSpace(M().sp_sm);
        Mono(narrow(exe_dir() + L"\\tuneforge-reset.exe").c_str(), P().text_secondary);
    }
    EndCard();
}

bool App::ApplySelection(const std::vector<std::string>& ids, bool advanced, bool expert) {
    last_outcome_ = Engine::Outcome{};
    if (ids.empty()) return false;
    if (!elevated_) {
        Notify("Modification impossible sans elevation. Fermez Tuneforge et relancez-le "
               "en tant qu'administrateur, ou utilisez la ligne de commande.",
               Status::Warn);
        return false;
    }
    Engine::Options opt;
    opt.allow_advanced = advanced;
    opt.allow_expert   = expert;
    Engine::Outcome o = engine_->apply_ids(ids, opt);
    last_outcome_ = o;

    if (!o.failed.empty()) {
        Notify("Echec : " + o.failed.front(), Status::Danger);
    } else if (!o.skipped.empty()) {
        Notify("Ignore : " + o.skipped.front(), Status::Warn);
    } else if (!o.applied.empty()) {
        Notify(o.needs_reboot ? "Applique. Un redemarrage est necessaire pour que tout "
                                "prenne effet."
                              : "Applique.",
               Status::Ok);
    } else {
        Notify("Deja dans l'etat cible.", Status::Neutral);
    }
    RefreshLog();
    return !o.applied.empty();
}

// ===========================================================================
// Materiel
// ===========================================================================
namespace {

// Ligne de fiche technique : intitule a gauche sur une largeur fixe, valeur a
// droite. La valeur est en monospace parce que c'est une donnee relevee sur la
// machine, pas une phrase.
void SpecRow(const char* label, const std::string& value, float label_w) {
    if (value.empty()) return;
    ImDrawList*  dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float  avail = ImGui::GetContentRegionAvail().x;

    ImGui::PushFont(F().regular, M().font_small);
    const float line = ImGui::GetFontSize();
    dl->AddText(ImVec2(pos.x, pos.y), U32(P().text_muted), label);
    ImGui::PopFont();

    ImGui::PushFont(F().mono, M().font_small);
    const float wrap = avail - label_w;
    const float h = ImGui::CalcTextSize(value.c_str(), nullptr, false, wrap).y;
    dl->AddText(F().mono, M().font_small, ImVec2(pos.x + label_w, pos.y), U32(P().text),
                value.c_str(), nullptr, wrap);
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(avail, (std::max)(line, h) + M().sp_xs));
}

}  // namespace

void App::PageHardware() {
    const hw::Profile& p = engine_->hardware();
    const float total = ImGui::GetContentRegionAvail().x;
    const float gap = M().sp_md;
    const float col = (total - gap) / 2.0f;
    const float lw = 118 * M().scale;

    // --- Processeur ---------------------------------------------------------
    if (BeginCard("##hw_cpu", ImVec2(col, 0))) {
        CardHeader("Processeur", p.cpu.brand.c_str());
        SpecRow("Architecture", p.cpu.codename(), lw);
        SpecRow("Coeurs", std::format("{} physiques / {} logiques", p.cpu.physical_cores,
                                      p.cpu.logical_cores), lw);
        SpecRow("Frequence base", std::format("{} MHz", p.cpu.base_mhz), lw);
        SpecRow("Instructions", std::string("AVX2 ") + (p.cpu.avx2 ? "oui" : "non") +
                                    (p.cpu.avx512f ? " · AVX-512 oui" : ""), lw);
        SpecRow("Signature", std::format("famille {:#x} · modele {:#x} · stepping {}",
                                         p.cpu.family, p.cpu.model, p.cpu.stepping), lw);
        if (p.cpu.has_x3d) {
            VSpace(M().sp_xs);
            StatusPill("Puce X3D — tension geree au BIOS", Status::Warn);
        }
    }
    EndCard();
    ImGui::SameLine(0, gap);

    // --- Graphismes ---------------------------------------------------------
    if (BeginCard("##hw_gpu", ImVec2(col, 0))) {
        const hw::GpuInfo* main = p.primary_gpu();
        CardHeader("Carte graphique", main ? main->description.c_str() : "aucune");
        for (const auto& g : p.gpus) {
            if (&g != main) continue;
            SpecRow("VRAM", std::format("{} Mo", g.dedicated_vram / (1024 * 1024)), lw);
            SpecRow("Pilote", g.driver_version, lw);
            SpecRow("Identifiants", std::format("{:#06x} / {:#06x}", g.vendor_id, g.device_id),
                    lw);
        }
        for (const auto& g : p.gpus) {
            if (!g.is_integrated) continue;
            SpecRow("Integre", g.description, lw);
        }
    }
    EndCard();

    VSpace(gap);

    // --- Machine ------------------------------------------------------------
    if (BeginCard("##hw_sys", ImVec2(col, 0))) {
        CardHeader("Machine", trim(p.system.manufacturer + " " + p.system.product).c_str());
        SpecRow("Carte mere", trim(p.system.baseboard_vendor + " " + p.system.baseboard_product),
                lw);
        SpecRow("BIOS", std::format("{} ({})", p.system.bios_version, p.system.bios_date), lw);
        SpecRow("Format", p.system.is_laptop ? "portable" : "bureau", lw);
        SpecRow("Windows", std::format("{} {}", p.os.product_name, p.os.display_version), lw);
        SpecRow("Build", std::format("{}.{}", p.os.build, p.os.ubr), lw);
    }
    EndCard();
    ImGui::SameLine(0, gap);

    // --- Memoire ------------------------------------------------------------
    if (BeginCard("##hw_mem", ImVec2(col, 0))) {
        CardHeader("Memoire",
                   std::format("{} Go au total", p.memory.total_bytes / 1073741824).c_str());
        for (const auto& m : p.memory.modules) {
            SpecRow(m.locator.empty() ? "Barrette" : m.locator.c_str(),
                    std::format("{} Go @ {} MT/s", m.size_bytes / 1073741824,
                                m.configured_mts ? m.configured_mts : m.speed_mts),
                    lw);
            if (!m.part_number.empty()) {
                SpecRow("  reference", trim(m.manufacturer + " " + m.part_number), lw);
            }
        }
    }
    EndCard();
}

// ===========================================================================
// Parametres
// ===========================================================================
void App::DrawJournal() {
    if (ImGui::GetTime() - last_log_read_ > 2.0) RefreshLog();

    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup();
    if (GhostButton("Actualiser")) RefreshLog();
    if (DirtyFlag::exists()) {
        ImGui::SameLine(0, M().sp_sm);
        if (DangerButton("Reparer le lot interrompu", ImVec2(0, 0), elevated_)) {
            Engine::Outcome o = engine_->recover();
            char msg[128];
            std::snprintf(msg, sizeof(msg), "Reparation : %zu reglage(s) restaure(s).",
                          o.applied.size());
            Notify(msg, Status::Ok);
            RefreshLog();
        }
    }
    ImGui::EndGroup();
    VSpace(M().sp_sm);

    if (BeginCard("##log", ImVec2(w, 300 * M().scale), true)) {
        if (log_lines_.empty()) Muted("Le journal est vide.");
        for (const auto& line : log_lines_) {
            ImVec4 col = P().text_secondary;
            if (line.find("[ERROR]") != std::string::npos)      col = P().danger;
            else if (line.find("[WARN ]") != std::string::npos) col = P().warn;
            else if (line.find("[DEBUG]") != std::string::npos) col = P().text_disabled;
            Mono(line.c_str(), col);
        }
    }
    EndCard();
}

namespace {

// Vignette de theme : deux pastilles de couleur et le nom.
bool ThemeCard(ThemeId id, bool selected, const ImVec2& size) {
    ImGuiWindow*  win = ImGui::GetCurrentWindow();
    const ImVec2  pos = ImGui::GetCursorScreenPos();
    const ImGuiID gid = win->GetID(ThemeName(id));

    ImGui::ItemSize(size);
    if (!ImGui::ItemAdd(ImRect(pos, pos + size), gid)) return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(ImRect(pos, pos + size), gid, &hovered, &held);
    const float t = Animate(ImHashStr("##th", 0, gid), selected ? 1.0f : (hovered ? 0.4f : 0.0f));

    const ImVec4 accent = ThemeAccent(id);
    ImDrawList*  dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, pos + size, U32(P().surface1), M().r_lg);
    dl->AddRect(pos, pos + size, U32(Mix(P().border_subtle, accent, t)), M().r_lg, 0,
                M().border + t * 1.2f);

    // Apercu : la surface du theme, avec une barre d'accent.
    const float pad = M().sp_md;
    const ImVec2 sw(pos.x + pad, pos.y + pad);
    const ImVec2 se(pos.x + size.x - pad, sw.y + 34 * M().scale);
    dl->AddRectFilled(sw, se, U32(ThemeSurface(id)), M().r_md);
    dl->AddRectFilled(ImVec2(sw.x + M().sp_sm, sw.y + M().sp_sm),
                      ImVec2(sw.x + M().sp_sm + 46 * M().scale, se.y - M().sp_sm),
                      U32(accent), M().r_sm);

    ImGui::PushFont(F().semibold, M().font_body);
    dl->AddText(ImVec2(pos.x + pad, se.y + M().sp_sm), U32(P().text), ThemeName(id));
    ImGui::PopFont();

    ImGui::PushFont(F().regular, M().font_micro);
    dl->AddText(F().regular, M().font_micro,
                ImVec2(pos.x + pad, se.y + M().sp_sm + M().font_body + M().sp_xs),
                U32(P().text_muted), ThemeDescription(id), nullptr, size.x - pad * 2);
    ImGui::PopFont();

    return pressed;
}

}  // namespace

void App::PageSettings() {
    const float total = ImGui::GetContentRegionAvail().x;
    const float gap = M().sp_md;

    // --- Themes -------------------------------------------------------------
    SectionLabel("Theme");
    VSpace(M().sp_sm);
    const float cw = (total - gap * 3) / 4.0f;
    const float ch = 128 * M().scale;
    for (int i = 0; i < static_cast<int>(ThemeId::Count); ++i) {
        const ThemeId id = static_cast<ThemeId>(i);
        if (i) ImGui::SameLine(0, gap);
        if (ThemeCard(id, CurrentTheme() == id, ImVec2(cw, ch))) {
            SetTheme(id);
            style_reload_ = true;
            SaveUiPrefs();
        }
    }

    VSpace(M().sp_lg);

    // --- Echelle ------------------------------------------------------------
    SectionLabel("Taille de l'interface");
    VSpace(M().sp_sm);
    if (BeginCard("##set_scale", ImVec2(total, 0))) {
        const float inner = ImGui::GetContentRegionAvail().x;
        char pct[16];
        std::snprintf(pct, sizeof(pct), "%.0f %%", UiScale() * 100.0f);
        const int step = Stepper("uiscale", pct, UiScale() > kUiScaleMin + 0.001f,
                                 UiScale() < kUiScaleMax - 0.001f,
                                 (std::min)(inner, 260 * M().scale));
        if (step != 0) {
            SetUiScale(UiScale() + step * kUiScaleStep);
            style_reload_ = true;
            SaveUiPrefs();
        }
        VSpace(M().sp_sm);
        Small("Agit sur les polices et les espacements ensemble, pour que les proportions "
              "restent justes.",
              P().text_muted);
    }
    EndCard();

    VSpace(M().sp_lg);

    // --- Diagnostic ----------------------------------------------------------
    SectionLabel("Diagnostic");
    VSpace(M().sp_sm);
    if (BeginCard("##set_report", ImVec2(total, 0))) {
        const float inner = ImGui::GetContentRegionAvail().x;
        if (PrimaryButton("Generer un rapport")) WriteDiagnosticReport();
        VSpace(M().sp_sm);
        Small("Materiel, etat des reglages, capteurs et diagnostic GPU, dans un fichier "
              "JSON.",
              P().text_muted);
        VSpace(M().sp_xs);
        WrappedMuted("Ni nom d'utilisateur, ni numero de serie. Les identifiants propres a "
                     "la machine — GUID du plan d'alimentation, des interfaces reseau — "
                     "sont remplaces par des jetons numerotes avant l'ecriture.",
                     inner);
    }
    EndCard();

    VSpace(M().sp_lg);

    // --- A propos -----------------------------------------------------------
    SectionLabel("A propos");
    VSpace(M().sp_sm);
    if (BeginCard("##set_about", ImVec2(total, 0))) {
        const float lw = 130 * M().scale;
        SpecRow("Version", TF_VERSION, lw);
        SpecRow("Donnees", narrow(data_dir()), lw);
        SpecRow("Outil de secours", narrow(exe_dir() + L"\\tuneforge-reset.exe"), lw);
        VSpace(M().sp_xs);
        StatusPill("Aucun driver noyau", Status::Ok);
    }
    EndCard();

    VSpace(M().sp_lg);

    // --- Journal --------------------------------------------------------------
    // En dernier : c'est le bloc le plus haut de la page, et le moins souvent
    // consulte. Le placer plus tot repoussait tout le reste sous le pli.
    SectionLabel("Journal");
    VSpace(M().sp_sm);
    DrawJournal();
}


// ---------------------------------------------------------------------------
// Rapport de diagnostic
//
// Ecrit a cote des donnees plutot que dans un dossier choisi : une boite de
// dialogue de sauvegarde ferait un pas de plus a franchir pour la seule chose
// qu'on demande aux testeurs de faire. L'explorateur s'ouvre sur le fichier,
// il n'y a plus qu'a le joindre.
// ---------------------------------------------------------------------------
void App::WriteDiagnosticReport() {
    const std::wstring path =
        data_dir() + L"\\rapport-" + widen(timestamp_compact()) + L".json";

    const Json report = engine_->diagnostic_report();
    if (!write_text_file(path, report.dump(2))) {
        log_error("rapport non ecrit : {}", narrow(path));
        Notify("Le rapport n'a pas pu etre ecrit. Voir le journal.", Status::Danger);
        return;
    }
    log_info("rapport de diagnostic ecrit : {}", narrow(path));

    // « /select, » ouvre le dossier avec le fichier deja mis en evidence :
    // l'utilisateur n'a pas a le chercher parmi les autres.
    ::ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select," + path).c_str(),
                    nullptr, SW_SHOWNORMAL);

    Notify("Rapport ecrit dans " + narrow(data_dir()) + ".", Status::Ok);
}

// ===========================================================================
// Preferences d'interface, persistees a cote de l'etat
// ===========================================================================
void App::LoadUiPrefsStatic() {
    auto text = read_text_file(data_dir() + L"\\ui.json");
    if (!text) return;
    auto j = Json::parse(*text);
    if (!j || !j->is_object()) return;

    const int theme = static_cast<int>((*j)["theme"].as_int(0));
    if (theme >= 0 && theme < static_cast<int>(ThemeId::Count)) {
        SetTheme(static_cast<ThemeId>(theme));
    }
    const double scale = (*j)["ui_scale"].as_number(0.0);
    if (scale > 0.0) SetUiScale(static_cast<float>(scale));
}

void App::SaveUiPrefs() {
    Json j = Json::object();
    j.set("theme", static_cast<int64_t>(CurrentTheme()));
    j.set("ui_scale", UiScale());
    j.set("fan_mode", static_cast<int64_t>(fan_mode_));
    Json curve = Json::array();
    for (const auto& fp : fan_curve_) {
        Json pt = Json::object();
        pt.set("t", static_cast<int64_t>(fp.temp_c));
        pt.set("l", static_cast<int64_t>(fp.level_pct));
        curve.push(std::move(pt));
    }
    j.set("fan_curve", std::move(curve));
    write_text_file(data_dir() + L"\\ui.json", j.dump(2));
}

} // namespace tf::ui
