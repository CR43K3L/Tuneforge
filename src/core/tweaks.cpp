#include "core/tweak.hpp"

#include <windows.h>

#include <functional>
#include <utility>

#include "platform/power.hpp"
#include "platform/winreg.hpp"

namespace tf {

const char* to_string(Tier t) {
    switch (t) {
        case Tier::Basic:    return "basique";
        case Tier::Advanced: return "avance";
        case Tier::Expert:   return "expert";
    }
    return "?";
}

const char* to_string(TweakState s) {
    switch (s) {
        case TweakState::Applied:     return "applique";
        case TweakState::NotApplied:  return "non applique";
        case TweakState::Partial:     return "partiel";
        case TweakState::Unavailable: return "indisponible";
        default:                      return "inconnu";
    }
}

namespace {

using AvailFn = std::function<bool(const hw::Profile&)>;

// ===========================================================================
// Reglages a base de valeurs de registre
// ===========================================================================
struct RegEntry {
    reg::Root    root;
    std::wstring key;
    std::wstring name;
    bool         is_string = false;
    uint32_t     dword_value = 0;
    std::wstring string_value;
};

class RegistryTweak : public ITweak {
public:
    RegistryTweak(TweakMeta m, std::vector<RegEntry> entries, AvailFn avail = {},
                  std::string avail_reason = {})
        : meta_(std::move(m)),
          entries_(std::move(entries)),
          avail_(std::move(avail)),
          avail_reason_(std::move(avail_reason)) {}

    const TweakMeta& meta() const override { return meta_; }

    bool available(const hw::Profile& p) const override {
        return avail_ ? avail_(p) : true;
    }
    std::string unavailable_reason(const hw::Profile& p) const override {
        return available(p) ? std::string() : avail_reason_;
    }

    TweakState state() const override {
        size_t matching = 0;
        for (const auto& e : entries_) {
            if (e.is_string) {
                auto v = reg::read_string(e.root, e.key, e.name);
                if (v && *v == e.string_value) ++matching;
            } else {
                auto v = reg::read_dword(e.root, e.key, e.name);
                if (v && *v == e.dword_value) ++matching;
            }
        }
        if (matching == entries_.size()) return TweakState::Applied;
        if (matching == 0) return TweakState::NotApplied;
        return TweakState::Partial;
    }

    std::string current_value() const override {
        std::string out;
        for (const auto& e : entries_) {
            if (!out.empty()) out += ", ";
            out += narrow(e.name) + "=";
            if (e.is_string) {
                auto v = reg::read_string(e.root, e.key, e.name);
                out += v ? narrow(*v) : "(absent)";
            } else {
                auto v = reg::read_dword(e.root, e.key, e.name);
                out += v ? std::to_string(*v) : "(absent)";
            }
        }
        return out;
    }

    std::string target_value() const override {
        std::string out;
        for (const auto& e : entries_) {
            if (!out.empty()) out += ", ";
            out += narrow(e.name) + "=";
            out += e.is_string ? narrow(e.string_value) : std::to_string(e.dword_value);
        }
        return out;
    }

    Json capture() const override {
        Json arr = Json::array();
        for (const auto& e : entries_) {
            Json j = Json::object();
            j.set("root", narrow(reg::root_name(e.root)));
            j.set("key", narrow(e.key));
            j.set("name", narrow(e.name));
            if (e.is_string) {
                auto v = reg::read_string(e.root, e.key, e.name);
                j.set("type", "string");
                j.set("present", v.has_value());
                if (v) j.set("value", narrow(*v));
            } else {
                auto v = reg::read_dword(e.root, e.key, e.name);
                j.set("type", "dword");
                j.set("present", v.has_value());
                if (v) j.set("value", static_cast<int64_t>(*v));
            }
            arr.push(std::move(j));
        }
        return arr;
    }

    Result apply() override {
        for (const auto& e : entries_) {
            Result r = e.is_string
                           ? reg::write_string(e.root, e.key, e.name, e.string_value)
                           : reg::write_dword(e.root, e.key, e.name, e.dword_value);
            if (!r) return r;
        }
        return Result::success();
    }

    Result restore(const Json& snapshot) override {
        if (!snapshot.is_array()) return Result::fail("instantane invalide (tableau attendu)");
        for (const auto& j : snapshot.items()) {
            auto root = reg::root_from_name(widen(j["root"].as_string()));
            if (!root) return Result::fail("racine de registre inconnue dans l'instantane");
            const std::wstring key  = widen(j["key"].as_string());
            const std::wstring name = widen(j["name"].as_string());

            Result r;
            if (!j["present"].as_bool()) {
                // La valeur n'existait pas : on la supprime pour revenir a l'origine.
                r = reg::delete_value(*root, key, name);
            } else if (j["type"].as_string() == "string") {
                r = reg::write_string(*root, key, name, widen(j["value"].as_string()));
            } else {
                r = reg::write_dword(*root, key, name, j["value"].as_u32());
            }
            if (!r) return r;
        }
        return Result::success();
    }

private:
    TweakMeta             meta_;
    std::vector<RegEntry> entries_;
    AvailFn               avail_;
    std::string           avail_reason_;
};

// ===========================================================================
// Reglages du plan d'alimentation actif
// ===========================================================================
class PowerSettingTweak : public ITweak {
public:
    PowerSettingTweak(TweakMeta m, GUID sub, GUID setting, uint32_t ac, bool apply_dc = false,
                      uint32_t dc = 0, AvailFn avail = {}, std::string avail_reason = {})
        : meta_(std::move(m)),
          sub_(sub),
          setting_(setting),
          ac_(ac),
          dc_(dc),
          apply_dc_(apply_dc),
          avail_(std::move(avail)),
          avail_reason_(std::move(avail_reason)) {}

    const TweakMeta& meta() const override { return meta_; }

    bool available(const hw::Profile& p) const override {
        if (avail_ && !avail_(p)) return false;
        return power::read_ac(sub_, setting_).has_value();
    }
    std::string unavailable_reason(const hw::Profile& p) const override {
        if (available(p)) return {};
        if (avail_ && !avail_(p)) return avail_reason_;
        return "reglage absent du plan d'alimentation de cette machine";
    }

    TweakState state() const override {
        auto ac = power::read_ac(sub_, setting_);
        if (!ac) return TweakState::Unavailable;
        if (*ac != ac_) return TweakState::NotApplied;
        if (apply_dc_) {
            auto dc = power::read_dc(sub_, setting_);
            if (!dc || *dc != dc_) return TweakState::Partial;
        }
        return TweakState::Applied;
    }

    std::string current_value() const override {
        auto ac = power::read_ac(sub_, setting_);
        auto dc = power::read_dc(sub_, setting_);
        return std::format("secteur={} batterie={}",
                           ac ? std::to_string(*ac) : "(n/d)",
                           dc ? std::to_string(*dc) : "(n/d)");
    }

    std::string target_value() const override {
        return apply_dc_ ? std::format("secteur={} batterie={}", ac_, dc_)
                         : std::format("secteur={}", ac_);
    }

    Json capture() const override {
        Json j = Json::object();
        j.set("kind", "power_setting");
        if (auto s = power::active_scheme()) j.set("scheme", power::guid_to_string(*s));
        if (auto ac = power::read_ac(sub_, setting_)) j.set("ac", static_cast<int64_t>(*ac));
        if (auto dc = power::read_dc(sub_, setting_)) j.set("dc", static_cast<int64_t>(*dc));
        return j;
    }

    Result apply() override {
        power::unhide_setting(sub_, setting_);
        Result r = power::write_ac(sub_, setting_, ac_);
        if (!r) return r;
        if (apply_dc_) {
            r = power::write_dc(sub_, setting_, dc_);
            if (!r) return r;
        }
        return power::commit();
    }

    Result restore(const Json& snapshot) override {
        // On restaure sur le plan qui etait actif au moment de la capture.
        std::optional<GUID> scheme;
        if (snapshot.has("scheme")) scheme = power::guid_from_string(snapshot["scheme"].as_string());
        const GUID* target = (scheme && power::scheme_exists(*scheme)) ? &*scheme : nullptr;

        if (snapshot.has("ac")) {
            Result r = power::write_ac(sub_, setting_, snapshot["ac"].as_u32(), target);
            if (!r) return r;
        }
        if (snapshot.has("dc")) {
            Result r = power::write_dc(sub_, setting_, snapshot["dc"].as_u32(), target);
            if (!r) return r;
        }
        return power::commit();
    }

private:
    TweakMeta   meta_;
    GUID        sub_{}, setting_{};
    uint32_t    ac_ = 0, dc_ = 0;
    bool        apply_dc_ = false;
    AvailFn     avail_;
    std::string avail_reason_;
};

// ===========================================================================
// Bascule de plan d'alimentation
// ===========================================================================
class PowerPlanTweak : public ITweak {
public:
    explicit PowerPlanTweak(TweakMeta m) : meta_(std::move(m)) {}

    const TweakMeta& meta() const override { return meta_; }

    bool available(const hw::Profile& p) const override {
        return p.caps.power_plans && !p.system.is_laptop;
    }
    std::string unavailable_reason(const hw::Profile& p) const override {
        if (!p.caps.power_plans) return "API des plans d'alimentation inaccessible";
        if (p.system.is_laptop) {
            return "machine portable : le plan Performances ultimes degrade l'autonomie "
                   "et la thermique, non applique par defaut";
        }
        return {};
    }

    TweakState state() const override {
        auto active = power::active_scheme();
        if (!active) return TweakState::Unavailable;
        return power::guid_equal(*active, power::kSchemeUltimate) ? TweakState::Applied
                                                                  : TweakState::NotApplied;
    }

    std::string current_value() const override {
        auto active = power::active_scheme();
        return active ? power::scheme_name(*active) : "(inconnu)";
    }

    std::string target_value() const override { return "Performances ultimes"; }

    Json capture() const override {
        Json j = Json::object();
        j.set("kind", "power_plan");
        if (auto s = power::active_scheme()) {
            j.set("scheme", power::guid_to_string(*s));
            j.set("scheme_name", power::scheme_name(*s));
        }
        return j;
    }

    Result apply() override {
        auto g = power::ensure_ultimate_scheme();
        if (!g) {
            return Result::fail(
                "plan Performances ultimes indisponible sur cette edition de Windows");
        }
        return power::set_active_scheme(*g);
    }

    Result restore(const Json& snapshot) override {
        if (!snapshot.has("scheme")) return Result::fail("aucun plan enregistre");
        auto g = power::guid_from_string(snapshot["scheme"].as_string());
        if (!g) return Result::fail("GUID de plan invalide dans l'instantane");
        if (!power::scheme_exists(*g)) {
            log_warn("plan {} disparu, retour sur Utilisation normale",
                     snapshot["scheme"].as_string());
            return power::set_active_scheme(power::kSchemeBalanced);
        }
        return power::set_active_scheme(*g);
    }

private:
    TweakMeta meta_;
};

// ===========================================================================
// Reglages TCP par interface reseau
// ===========================================================================
constexpr wchar_t kTcpInterfaces[] =
    L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces";

class NagleTweak : public ITweak {
public:
    explicit NagleTweak(TweakMeta m) : meta_(std::move(m)) {}

    const TweakMeta& meta() const override { return meta_; }

    bool available(const hw::Profile& p) const override { return p.caps.network_tweaks; }
    std::string unavailable_reason(const hw::Profile& p) const override {
        return p.caps.network_tweaks ? std::string() : "aucune interface TCP/IP trouvee";
    }

    TweakState state() const override {
        auto ifaces = interfaces();
        if (ifaces.empty()) return TweakState::Unavailable;
        size_t done = 0;
        for (const auto& i : ifaces) {
            auto a = reg::read_dword(reg::Root::HKLM, i, L"TcpAckFrequency");
            auto n = reg::read_dword(reg::Root::HKLM, i, L"TCPNoDelay");
            if (a && *a == 1 && n && *n == 1) ++done;
        }
        if (done == ifaces.size()) return TweakState::Applied;
        if (done == 0) return TweakState::NotApplied;
        return TweakState::Partial;
    }

    std::string current_value() const override {
        auto ifaces = interfaces();
        size_t done = 0;
        for (const auto& i : ifaces) {
            auto a = reg::read_dword(reg::Root::HKLM, i, L"TcpAckFrequency");
            if (a && *a == 1) ++done;
        }
        return std::format("{}/{} interfaces", done, ifaces.size());
    }

    std::string target_value() const override {
        return "TcpAckFrequency=1, TCPNoDelay=1 sur toutes les interfaces";
    }

    Json capture() const override {
        Json arr = Json::array();
        for (const auto& key : interfaces()) {
            Json j = Json::object();
            j.set("key", narrow(key));
            auto a = reg::read_dword(reg::Root::HKLM, key, L"TcpAckFrequency");
            auto n = reg::read_dword(reg::Root::HKLM, key, L"TCPNoDelay");
            j.set("ack_present", a.has_value());
            if (a) j.set("ack", static_cast<int64_t>(*a));
            j.set("nodelay_present", n.has_value());
            if (n) j.set("nodelay", static_cast<int64_t>(*n));
            arr.push(std::move(j));
        }
        return arr;
    }

    Result apply() override {
        auto ifaces = interfaces();
        if (ifaces.empty()) return Result::fail("aucune interface TCP/IP trouvee");
        for (const auto& key : ifaces) {
            Result r = reg::write_dword(reg::Root::HKLM, key, L"TcpAckFrequency", 1);
            if (!r) return r;
            r = reg::write_dword(reg::Root::HKLM, key, L"TCPNoDelay", 1);
            if (!r) return r;
        }
        return Result::success();
    }

    Result restore(const Json& snapshot) override {
        if (!snapshot.is_array()) return Result::fail("instantane invalide");
        for (const auto& j : snapshot.items()) {
            const std::wstring key = widen(j["key"].as_string());
            Result r = j["ack_present"].as_bool()
                           ? reg::write_dword(reg::Root::HKLM, key, L"TcpAckFrequency",
                                              j["ack"].as_u32())
                           : reg::delete_value(reg::Root::HKLM, key, L"TcpAckFrequency");
            if (!r) return r;
            r = j["nodelay_present"].as_bool()
                    ? reg::write_dword(reg::Root::HKLM, key, L"TCPNoDelay", j["nodelay"].as_u32())
                    : reg::delete_value(reg::Root::HKLM, key, L"TCPNoDelay");
            if (!r) return r;
        }
        return Result::success();
    }

private:
    static std::vector<std::wstring> interfaces() {
        std::vector<std::wstring> out;
        for (const auto& sub : reg::enum_subkeys(reg::Root::HKLM, kTcpInterfaces)) {
            if (sub.size() < 2 || sub.front() != L'{') continue;
            out.push_back(std::wstring(kTcpInterfaces) + L"\\" + sub);
        }
        return out;
    }

    TweakMeta meta_;
};

// ===========================================================================
// Catalogue
// ===========================================================================
TweakMeta mk(std::string id, std::string title, std::string what, std::string why, Tier tier,
             bool reboot = false, std::string caveat = {}) {
    TweakMeta m;
    m.id = std::move(id);
    m.title = std::move(title);
    m.what = std::move(what);
    m.why = std::move(why);
    m.tier = tier;
    m.needs_admin = true;
    m.needs_reboot = reboot;
    m.caveat = std::move(caveat);
    return m;
}

} // namespace

std::vector<TweakPtr> build_all_tweaks() {
    std::vector<TweakPtr> v;

    // --- Alimentation ------------------------------------------------------
    v.push_back(std::make_unique<PowerPlanTweak>(mk(
        "power.plan.ultimate", "Plan « Performances ultimes »",
        "Active le plan d'alimentation Performances ultimes, cree s'il n'existe pas.",
        "Supprime les temporisations d'etat de veille des composants ; profite surtout "
        "a la latence, pas au debit brut.",
        Tier::Basic, false,
        "Consommation au repos plus elevee. Sans effet notable si le plan actif est deja "
        "Performances elevees.")));

    v.push_back(std::make_unique<PowerSettingTweak>(
        mk("power.core_parking.off", "Desactiver le core parking",
           "Force 100 % des coeurs a rester actifs (processeur : coeurs minimum).",
           "Evite les micro-latences de reveil de coeur quand la charge est irreguliere — "
           "typiquement les 1 % low en jeu.",
           Tier::Basic, false,
           "Consommation au repos legerement superieure."),
        power::kSubProcessor, power::kSetCoreParkingMin, 100));

    v.push_back(std::make_unique<PowerSettingTweak>(
        mk("power.boost.aggressive", "Mode de boost agressif",
           "Passe le mode de boost du processeur en Agressif.",
           "Le processeur monte en frequence plus vite au reveil de charge.",
           Tier::Basic, false,
           "Temperatures et ventilation un peu plus reactives a la hausse."),
        power::kSubProcessor, power::kSetPerfBoostMode, 2));

    v.push_back(std::make_unique<PowerSettingTweak>(
        mk("power.usb_suspend.off", "Desactiver la suspension USB selective",
           "Empeche Windows de mettre en veille les peripheriques USB inactifs.",
           "Supprime les reveils de peripherique — souris, clavier, casque, volant, HOTAS.",
           Tier::Basic, false,
           "Consommation USB permanente ; a laisser actif sur portable."),
        power::kSubUsb, power::kSetUsbSelectiveSuspend, 0));

    v.push_back(std::make_unique<PowerSettingTweak>(
        mk("power.disk_timeout.never", "Ne jamais arreter les disques",
           "Met a zero le delai d'arret des disques durs.",
           "Evite les blocages d'une seconde au premier acces apres inactivite.",
           Tier::Basic, false,
           "Aucun effet sur les SSD NVMe, qui ne suivent pas ce reglage."),
        power::kSubDisk, power::kSetDiskTimeout, 0));

    v.push_back(std::make_unique<PowerSettingTweak>(
        mk("power.pcie_aspm.off", "Desactiver l'economie d'energie PCIe (ASPM)",
           "Passe la gestion d'alimentation des liens PCIe sur Desactive.",
           "Supprime la latence de sortie d'etat basse consommation du lien GPU et NVMe.",
           Tier::Advanced, false,
           "Quelques watts de plus au repos. Sur certaines cartes meres, l'ASPM "
           "desactive fait aussi monter la temperature du chipset."),
        power::kSubPciExpress, power::kSetPciExpressAspm, 0));

    v.push_back(std::make_unique<PowerSettingTweak>(
        mk("power.min_state.100", "Etat processeur minimum a 100 %",
           "Bloque le processeur a sa frequence maximale en permanence.",
           "Elimine toute rampe de frequence. Gain reel uniquement sur des charges "
           "tres irregulieres et sensibles a la latence.",
           Tier::Advanced, false,
           "Chaleur et bruit nettement superieurs au repos. Sur un processeur moderne "
           "gere par CPPC, le gain est souvent nul : a mesurer avant de garder."),
        power::kSubProcessor, power::kSetProcThrottleMin, 100));

    // --- Ordonnancement et reactivite --------------------------------------
    v.push_back(std::make_unique<RegistryTweak>(
        mk("win.power_throttling.off", "Desactiver le bridage EcoQoS",
           "Empeche Windows de brider les processus juges en arriere-plan.",
           "Evite qu'un jeu ou un moteur audio perde en priorite parce que Windows "
           "l'a classe en arriere-plan a tort.",
           Tier::Basic, true,
           "Legere hausse de consommation. A eviter sur portable sur batterie."),
        std::vector<RegEntry>{
            {reg::Root::HKLM, L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling",
             L"PowerThrottlingOff", false, 1, L""}}));

    v.push_back(std::make_unique<RegistryTweak>(
        mk("win.mmcss.responsiveness", "Reactivite MMCSS",
           "Passe SystemResponsiveness de 20 a 10.",
           "Reduit la part de temps processeur reservee aux taches multimedia de fond, "
           "au profit du premier plan.",
           Tier::Basic, true,
           "Descendre a 0, comme le conseillent beaucoup de « guides », peut faire "
           "craquer l'audio : la valeur retenue ici reste conservatrice."),
        std::vector<RegEntry>{
            {reg::Root::HKLM,
             L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
             L"SystemResponsiveness", false, 10, L""}}));

    v.push_back(std::make_unique<RegistryTweak>(
        mk("win.network_throttling.off", "Desactiver le bridage reseau multimedia",
           "Met NetworkThrottlingIndex a 0xFFFFFFFF (desactive).",
           "Windows limite par defaut le trafic reseau a 10 000 paquets/s quand une "
           "application multimedia tourne. La levee de cette limite se voit sur la "
           "regularite du ping.",
           Tier::Basic, true,
           "Aucune contrepartie connue sur une machine de bureau."),
        std::vector<RegEntry>{
            {reg::Root::HKLM,
             L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
             L"NetworkThrottlingIndex", false, 0xFFFFFFFFu, L""}}));

    v.push_back(std::make_unique<RegistryTweak>(
        mk("win.mmcss.games_task", "Priorites MMCSS de la tache Games",
           "Regle GPU Priority, Priority et les categories d'ordonnancement de la "
           "tache multimedia « Games ».",
           "Les jeux qui s'enregistrent aupres de MMCSS obtiennent une priorite "
           "processeur et GPU superieure.",
           Tier::Basic, true,
           "Sans effet sur les jeux qui n'utilisent pas MMCSS."),
        std::vector<RegEntry>{
            {reg::Root::HKLM,
             L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
             L"GPU Priority", false, 8, L""},
            {reg::Root::HKLM,
             L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
             L"Priority", false, 6, L""},
            {reg::Root::HKLM,
             L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
             L"Scheduling Category", true, 0, L"High"},
            {reg::Root::HKLM,
             L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
             L"SFIO Priority", true, 0, L"High"}}));

    v.push_back(std::make_unique<RegistryTweak>(
        mk("win.priority_separation", "Quantum court, priorite au premier plan",
           "Win32PrioritySeparation = 0x26 (quantum court et variable, boost x3 "
           "pour la fenetre au premier plan).",
           "Le processus au premier plan garde le processeur plus souvent.",
           Tier::Advanced, false,
           "Peut legerement penaliser les taches de fond (compilation, encodage, "
           "serveur local) pendant que vous jouez."),
        std::vector<RegEntry>{
            {reg::Root::HKLM, L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl",
             L"Win32PrioritySeparation", false, 0x26, L""}}));

    // --- GPU ---------------------------------------------------------------
    v.push_back(std::make_unique<RegistryTweak>(
        mk("gpu.hags.on", "Planification GPU acceleree (HAGS)",
           "Active HwSchMode = 2.",
           "Le GPU gere lui-meme sa file de travail : moins d'allers-retours avec le "
           "processeur, latence de rendu plus reguliere.",
           Tier::Advanced, true,
           "Redemarrage obligatoire. Sur certains pilotes anciens, HAGS degrade au "
           "contraire la stabilite de la capture video : a tester."),
        std::vector<RegEntry>{
            {reg::Root::HKLM, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
             L"HwSchMode", false, 2, L""}},
        [](const hw::Profile& p) { return p.caps.hags; },
        "aucun GPU dedie compatible detecte, ou version de Windows trop ancienne"));

    // --- Capture et entrees ------------------------------------------------
    v.push_back(std::make_unique<RegistryTweak>(
        mk("win.gamedvr.off", "Desactiver Game DVR / capture d'arriere-plan",
           "Coupe l'enregistrement d'arriere-plan de la Xbox Game Bar.",
           "Libere du temps GPU et supprime un hook sur la presentation des images.",
           Tier::Advanced, true,
           "Desactive aussi la capture des dernieres secondes de la Game Bar. "
           "Si vous utilisez Medal ou un autre outil de clips, celui-ci n'est pas "
           "affecte — mais verifiez avant d'appliquer."),
        std::vector<RegEntry>{
            {reg::Root::HKCU, L"System\\GameConfigStore", L"GameDVR_Enabled", false, 0, L""},
            {reg::Root::HKLM, L"SOFTWARE\\Policies\\Microsoft\\Windows\\GameDVR",
             L"AllowGameDVR", false, 0, L""},
            {reg::Root::HKCU,
             L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
             L"AppCaptureEnabled", false, 0, L""}}));

    v.push_back(std::make_unique<RegistryTweak>(
        mk("input.mouse_accel.off", "Desactiver l'acceleration de la souris",
           "Met MouseSpeed, MouseThreshold1 et MouseThreshold2 a 0.",
           "Rapport 1:1 entre le mouvement physique et le curseur : la visee devient "
           "reproductible.",
           Tier::Advanced, false,
           "Changement de ressenti immediat sur le bureau. Purement une preference."),
        std::vector<RegEntry>{
            {reg::Root::HKCU, L"Control Panel\\Mouse", L"MouseSpeed", true, 0, L"0"},
            {reg::Root::HKCU, L"Control Panel\\Mouse", L"MouseThreshold1", true, 0, L"0"},
            {reg::Root::HKCU, L"Control Panel\\Mouse", L"MouseThreshold2", true, 0, L"0"}}));

    // --- Reseau ------------------------------------------------------------
    v.push_back(std::make_unique<NagleTweak>(mk(
        "net.nagle.off", "Desactiver l'algorithme de Nagle",
        "TcpAckFrequency = 1 et TCPNoDelay = 1 sur chaque interface TCP/IP.",
        "Les petits paquets partent immediatement au lieu d'etre regroupes : "
        "c'est exactement le motif de trafic d'un jeu en ligne.",
        Tier::Advanced, true,
        "Augmente legerement le nombre de paquets envoyes. Deconseille sur une "
        "connexion tres saturee ou a forte perte.")));

    // --- Temporisation systeme --------------------------------------------
    v.push_back(std::make_unique<RegistryTweak>(
        mk("sys.timer_resolution.global", "Resolution du timer globale",
           "GlobalTimerResolutionRequests = 1.",
           "Depuis Windows 10 2004, une resolution de timer fine demandee par une "
           "application ne s'applique qu'a elle. Cette cle retablit le comportement "
           "global, ce dont beneficient les jeux qui ne la demandent pas eux-memes.",
           Tier::Advanced, true,
           "Redemarrage obligatoire. Consommation au repos legerement superieure ; "
           "sur portable, l'autonomie en patit."),
        std::vector<RegEntry>{
            {reg::Root::HKLM,
             L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\kernel",
             L"GlobalTimerResolutionRequests", false, 1, L""}}));

    return v;
}

} // namespace tf
