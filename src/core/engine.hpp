#pragma once
//
// Moteur : catalogue, etat persiste, profils, filet de securite.
//
// Trois mecanismes de securite, tous obligatoires des la v0.1 parce qu'ils
// sont impossibles a rajouter proprement apres coup :
//
//   1. Instantane avant modification  — StateStore
//   2. Drapeau « application en cours » — DirtyFlag, detecte un crash ou une
//      coupure au milieu d'un lot et permet la reprise
//   3. Chien de garde a confirmation  — Watchdog, annule automatiquement si
//      l'utilisateur ne confirme pas (meme principe qu'un changement de
//      resolution d'ecran)
//
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/tweak.hpp"

namespace tf {

// ---------------------------------------------------------------------------
// Etat persiste
// ---------------------------------------------------------------------------
class StateStore {
public:
    bool load();
    bool save() const;

    bool        has_snapshot(const std::string& id) const;
    const Json* snapshot(const std::string& id) const;
    void        put_snapshot(const std::string& id, Json data);
    void        erase_snapshot(const std::string& id);

    std::vector<std::string> applied_ids() const;

    void        set_active_profile(std::string name) { active_profile_ = std::move(name); }
    std::string active_profile() const { return active_profile_; }

    std::wstring path() const;
    const Json&  raw() const { return root_; }

private:
    Json        root_ = Json::object();
    std::string active_profile_;
};

// ---------------------------------------------------------------------------
// Drapeau « application en cours »
// ---------------------------------------------------------------------------
class DirtyFlag {
public:
    static std::wstring path();
    static bool         exists();
    static Json         read();
    static void         begin(const std::vector<std::string>& ids);
    static void         clear();
};

// ---------------------------------------------------------------------------
// Chien de garde
// ---------------------------------------------------------------------------
class Watchdog {
public:
    Watchdog(int seconds, std::function<void()> on_timeout);
    ~Watchdog();
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

    void confirm();
    bool fired() const;

private:
    std::mutex              m_;
    std::condition_variable cv_;
    bool                    confirmed_ = false;
    bool                    fired_ = false;
    std::thread             thread_;
};

// ---------------------------------------------------------------------------
// Profils
// ---------------------------------------------------------------------------
struct TuneProfile {
    std::string              name;
    std::string              title;
    std::string              description;
    std::vector<std::string> tweaks;
    std::wstring             source;
};

// ---------------------------------------------------------------------------
// Moteur
// ---------------------------------------------------------------------------
class Engine {
public:
    Engine();

    const hw::Profile&           hardware() const;
    const std::vector<TweakPtr>& tweaks() const { return tweaks_; }
    ITweak*                      find(std::string_view id);
    const ITweak*                find(std::string_view id) const;

    const std::vector<TuneProfile>& profiles() const { return profiles_; }
    const TuneProfile*              find_profile(std::string_view name) const;

    struct Options {
        bool dry_run = false;
        bool allow_advanced = false;  // les reglages Tier::Advanced sont ignores sans ce drapeau
        bool allow_expert = false;
    };

    struct Outcome {
        std::vector<std::string> applied;
        std::vector<std::string> already;
        std::vector<std::string> skipped;   // "id : raison"
        std::vector<std::string> failed;    // "id : raison"
        bool                     needs_reboot = false;

        bool any_change() const { return !applied.empty(); }
    };

    Outcome apply_ids(const std::vector<std::string>& ids, const Options& opt);
    Outcome revert_ids(const std::vector<std::string>& ids);
    Outcome revert_all();

    // Rejoue les instantanes laisses par un lot interrompu.
    Outcome recover();

    StateStore&       state() { return state_; }
    const StateStore& state() const { return state_; }

    Json diagnostic_report();

private:
    void load_profiles();
    void load_profiles_from(const std::wstring& dir);

    std::vector<TweakPtr>    tweaks_;
    std::vector<TuneProfile> profiles_;
    StateStore               state_;
};

} // namespace tf
