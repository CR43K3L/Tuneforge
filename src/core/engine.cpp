#include "core/engine.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>

#include "hw/sensors.hpp"
#include "platform/elevation.hpp"

namespace tf {
namespace {

constexpr int kStateVersion = 1;

std::wstring state_path()  { return data_dir() + L"\\state.json"; }
std::wstring dirty_path()  { return data_dir() + L"\\apply.lock"; }

} // namespace

// ===========================================================================
// StateStore
// ===========================================================================
std::wstring StateStore::path() const { return state_path(); }

bool StateStore::load() {
    root_ = Json::object();
    root_.set("version", kStateVersion);
    root_.set("snapshots", Json::object());

    auto text = read_text_file(state_path());
    if (!text) return false;

    std::string err;
    auto parsed = Json::parse(*text, &err);
    if (!parsed || !parsed->is_object()) {
        // Un etat illisible est plus dangereux qu'un etat absent : on le met de
        // cote plutot que de l'ecraser, pour pouvoir restaurer a la main.
        const std::wstring backup = data_dir() + L"\\state.corrupt-" +
                                    widen(timestamp_compact()) + L".json";
        ::MoveFileW(state_path().c_str(), backup.c_str());
        log_error("etat illisible ({}) : sauvegarde dans {}", err, narrow(backup));
        return false;
    }
    root_ = std::move(*parsed);
    if (!root_.has("snapshots")) root_.set("snapshots", Json::object());
    active_profile_ = root_["active_profile"].as_string();

    // Invariant : sans le moindre instantane, aucun profil ne peut etre actif.
    // Une version anterieure enregistrait le profil meme quand l'application
    // avait ete refusee faute d'elevation ; on repare l'etat plutot que de
    // continuer a afficher une information fausse.
    if (!active_profile_.empty() && root_["snapshots"].size() == 0) {
        log_warn("profil « {} » declare actif sans aucun instantane : etat corrige",
                 active_profile_);
        active_profile_.clear();
        root_.set("active_profile", "");
        save();  // on corrige le fichier, pas seulement la copie en memoire
    }
    return true;
}

bool StateStore::save() const {
    Json out = root_;
    out.set("version", kStateVersion);
    out.set("updated", timestamp_iso());
    out.set("active_profile", active_profile_);
    if (!write_text_file(state_path(), out.dump(2))) {
        log_error("ecriture de l'etat impossible : {}", narrow(state_path()));
        return false;
    }
    return true;
}

bool StateStore::has_snapshot(const std::string& id) const {
    return root_["snapshots"].find(id) != nullptr;
}

const Json* StateStore::snapshot(const std::string& id) const {
    const Json* entry = root_["snapshots"].find(id);
    if (!entry) return nullptr;
    return entry->find("data");
}

void StateStore::put_snapshot(const std::string& id, Json data) {
    Json entry = Json::object();
    entry.set("captured", timestamp_iso());
    entry.set("data", std::move(data));

    Json snaps = root_["snapshots"];
    if (!snaps.is_object()) snaps = Json::object();
    snaps.set(id, std::move(entry));
    root_.set("snapshots", std::move(snaps));
}

void StateStore::erase_snapshot(const std::string& id) {
    Json snaps = root_["snapshots"];
    if (!snaps.is_object()) return;
    auto& f = snaps.fields();
    f.erase(std::remove_if(f.begin(), f.end(),
                           [&](const std::pair<std::string, Json>& kv) { return kv.first == id; }),
            f.end());
    root_.set("snapshots", std::move(snaps));
}

std::vector<std::string> StateStore::applied_ids() const {
    std::vector<std::string> out;
    const Json& snaps = root_["snapshots"];
    if (!snaps.is_object()) return out;
    for (const auto& kv : snaps.fields()) out.push_back(kv.first);
    return out;
}

// ===========================================================================
// DirtyFlag
// ===========================================================================
std::wstring DirtyFlag::path() { return dirty_path(); }

bool DirtyFlag::exists() { return file_exists(dirty_path()); }

Json DirtyFlag::read() {
    auto text = read_text_file(dirty_path());
    if (!text) return Json::object();
    auto j = Json::parse(*text);
    return j ? *j : Json::object();
}

void DirtyFlag::begin(const std::vector<std::string>& ids) {
    Json j = Json::object();
    j.set("started", timestamp_iso());
    j.set("pid", static_cast<int64_t>(::GetCurrentProcessId()));
    Json arr = Json::array();
    for (const auto& id : ids) arr.push(Json(id));
    j.set("tweaks", std::move(arr));
    write_text_file(dirty_path(), j.dump(2));
}

void DirtyFlag::clear() { delete_file(dirty_path()); }

// ===========================================================================
// Watchdog
// ===========================================================================
Watchdog::Watchdog(int seconds, std::function<void()> on_timeout) {
    thread_ = std::thread([this, seconds, cb = std::move(on_timeout)]() {
        std::unique_lock lk(m_);
        const bool ok = cv_.wait_for(lk, std::chrono::seconds(seconds),
                                     [this] { return confirmed_; });
        if (!ok && !confirmed_) {
            fired_ = true;
            lk.unlock();
            log_warn("chien de garde : aucune confirmation recue, annulation automatique");
            if (cb) cb();
        }
    });
}

void Watchdog::confirm() {
    {
        std::lock_guard lk(m_);
        confirmed_ = true;
    }
    cv_.notify_all();
}

bool Watchdog::fired() const { return fired_; }

Watchdog::~Watchdog() {
    confirm();
    if (thread_.joinable()) thread_.join();
}

// ===========================================================================
// Engine
// ===========================================================================
Engine::Engine() {
    tweaks_ = build_all_tweaks();
    state_.load();
    load_profiles();
}

const hw::Profile& Engine::hardware() const { return hw::detect(); }

ITweak* Engine::find(std::string_view id) {
    for (auto& t : tweaks_) {
        if (t->meta().id == id) return t.get();
    }
    return nullptr;
}

const ITweak* Engine::find(std::string_view id) const {
    for (const auto& t : tweaks_) {
        if (t->meta().id == id) return t.get();
    }
    return nullptr;
}

const TuneProfile* Engine::find_profile(std::string_view name) const {
    for (const auto& p : profiles_) {
        if (iequals(p.name, name)) return &p;
    }
    return nullptr;
}

void Engine::load_profiles() {
    profiles_.clear();
    // Les profils livres avec le binaire, puis ceux de l'utilisateur (qui
    // peuvent redefinir les premiers).
    load_profiles_from(exe_dir() + L"\\profiles");
    load_profiles_from(data_dir() + L"\\profiles");
}

void Engine::load_profiles_from(const std::wstring& dir) {
    if (!dir_exists(dir)) return;

    WIN32_FIND_DATAW fd{};
    HANDLE h = ::FindFirstFileW((dir + L"\\*.json").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring full = dir + L"\\" + fd.cFileName;
        auto text = read_text_file(full);
        if (!text) continue;

        std::string err;
        auto j = Json::parse(*text, &err);
        if (!j || !j->is_object()) {
            log_warn("profil ignore ({}) : {}", narrow(full), err);
            continue;
        }
        TuneProfile p;
        p.name        = (*j)["name"].as_string();
        p.title       = (*j)["title"].as_string(p.name);
        p.description = (*j)["description"].as_string();
        p.source      = full;
        for (const auto& t : (*j)["tweaks"].items()) {
            if (t.is_string()) p.tweaks.push_back(t.as_string());
        }
        if (p.name.empty() || p.tweaks.empty()) {
            log_warn("profil ignore ({}) : nom ou liste de reglages manquants", narrow(full));
            continue;
        }
        // Un profil utilisateur du meme nom remplace celui du binaire.
        auto it = std::find_if(profiles_.begin(), profiles_.end(),
                               [&](const TuneProfile& e) { return iequals(e.name, p.name); });
        if (it != profiles_.end()) {
            *it = std::move(p);
        } else {
            profiles_.push_back(std::move(p));
        }
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
}

Engine::Outcome Engine::apply_ids(const std::vector<std::string>& ids, const Options& opt) {
    Outcome out;
    const hw::Profile& p = hardware();

    // --- Phase 1 : selection, sans rien ecrire -----------------------------
    std::vector<ITweak*> todo;
    for (const auto& id : ids) {
        ITweak* t = find(id);
        if (!t) {
            out.failed.push_back(id + " : reglage inconnu");
            continue;
        }
        const TweakMeta& m = t->meta();

        if (!t->available(p)) {
            std::string why = t->unavailable_reason(p);
            out.skipped.push_back(id + " : " + (why.empty() ? "indisponible sur ce materiel" : why));
            continue;
        }
        if (m.tier == Tier::Advanced && !opt.allow_advanced) {
            out.skipped.push_back(id + " : niveau avance, relancez avec --advanced");
            continue;
        }
        if (m.tier == Tier::Expert && !opt.allow_expert) {
            out.skipped.push_back(id + " : niveau expert, relancez avec --expert");
            continue;
        }
        if (t->state() == TweakState::Applied) {
            out.already.push_back(id);
            continue;
        }
        todo.push_back(t);
    }

    if (opt.dry_run || todo.empty()) {
        for (ITweak* t : todo) out.applied.push_back(t->meta().id);
        for (ITweak* t : todo) out.needs_reboot |= t->meta().needs_reboot;
        return out;
    }

    // --- Phase 2 : application, sous drapeau -------------------------------
    std::vector<std::string> todo_ids;
    todo_ids.reserve(todo.size());
    for (ITweak* t : todo) todo_ids.push_back(t->meta().id);
    DirtyFlag::begin(todo_ids);

    for (ITweak* t : todo) {
        const std::string& id = t->meta().id;

        // L'instantane d'origine ne doit jamais etre ecrase : s'il existe deja,
        // c'est celui d'avant notre premiere modification.
        if (!state_.has_snapshot(id)) {
            state_.put_snapshot(id, t->capture());
            state_.save();  // persiste AVANT d'ecrire quoi que ce soit
        }

        Result r = t->apply();
        if (!r) {
            log_error("{} : application echouee — {}", id, r.message);
            out.failed.push_back(id + " : " + r.message);
            // Retour immediat a l'etat capture pour ce reglage.
            if (const Json* snap = state_.snapshot(id)) {
                Result rb = t->restore(*snap);
                if (!rb) log_error("{} : annulation echouee — {}", id, rb.message);
            }
            state_.erase_snapshot(id);
            continue;
        }
        log_info("{} : applique", id);
        out.applied.push_back(id);
        out.needs_reboot |= t->meta().needs_reboot;
    }

    state_.save();
    DirtyFlag::clear();
    return out;
}

Engine::Outcome Engine::revert_ids(const std::vector<std::string>& ids) {
    Outcome out;
    for (const auto& id : ids) {
        ITweak* t = find(id);
        if (!t) {
            out.failed.push_back(id + " : reglage inconnu");
            continue;
        }
        const Json* snap = state_.snapshot(id);
        if (!snap) {
            out.skipped.push_back(id + " : aucun instantane, ce reglage n'a pas ete modifie "
                                       "par Tuneforge");
            continue;
        }
        Result r = t->restore(*snap);
        if (!r) {
            log_error("{} : restauration echouee — {}", id, r.message);
            out.failed.push_back(id + " : " + r.message);
            continue;
        }
        state_.erase_snapshot(id);
        out.applied.push_back(id);
        out.needs_reboot |= t->meta().needs_reboot;
        log_info("{} : restaure", id);
    }
    state_.save();
    return out;
}

Engine::Outcome Engine::revert_all() {
    // Ordre inverse de l'application : le plan d'alimentation revient en dernier.
    std::vector<std::string> ids = state_.applied_ids();
    std::reverse(ids.begin(), ids.end());
    Outcome out = revert_ids(ids);
    state_.set_active_profile({});
    state_.save();
    DirtyFlag::clear();
    return out;
}

Engine::Outcome Engine::recover() {
    Outcome out;
    if (!DirtyFlag::exists()) return out;

    Json info = DirtyFlag::read();
    log_warn("lot interrompu detecte (demarre a {}) : restauration", info["started"].as_string());

    std::vector<std::string> ids;
    for (const auto& t : info["tweaks"].items()) {
        if (t.is_string() && state_.has_snapshot(t.as_string())) ids.push_back(t.as_string());
    }
    std::reverse(ids.begin(), ids.end());
    out = revert_ids(ids);
    DirtyFlag::clear();
    return out;
}

Json Engine::diagnostic_report() {
    Json j = Json::object();
    j.set("tool", "Tuneforge");
    j.set("version", TF_VERSION);
    j.set("generated", timestamp_iso());
    j.set("elevated", is_elevated());
    j.set("hardware", hardware().to_json());

    Json jt = Json::array();
    const hw::Profile& p = hardware();
    for (const auto& t : tweaks_) {
        const TweakMeta& m = t->meta();
        Json e = Json::object();
        e.set("id", m.id);
        e.set("title", m.title);
        e.set("tier", to_string(m.tier));
        e.set("needs_reboot", m.needs_reboot);
        e.set("available", t->available(p));
        if (!t->available(p)) e.set("unavailable_reason", t->unavailable_reason(p));
        e.set("state", to_string(t->state()));
        e.set("current", t->current_value());
        e.set("target", t->target_value());
        e.set("managed_by_tuneforge", state_.has_snapshot(m.id));
        jt.push(std::move(e));
    }
    j.set("tweaks", std::move(jt));
    j.set("state", state_.raw());
    j.set("pending_batch", DirtyFlag::exists() ? DirtyFlag::read() : Json());

    // Capteurs, si HWiNFO tourne : tres utile pour diagnostiquer a distance.
    hw::HwInfoSensors sensors;
    if (sensors.open() && sensors.refresh()) {
        Json jr = Json::array();
        for (const auto& r : sensors.readings()) {
            if (r.type != hw::ReadingType::Temperature && r.type != hw::ReadingType::Power &&
                r.type != hw::ReadingType::Clock) {
                continue;
            }
            Json e = Json::object();
            e.set("sensor", r.sensor);
            e.set("label", r.label);
            e.set("unit", r.unit);
            e.set("value", r.value);
            e.set("max", r.value_max);
            jr.push(std::move(e));
        }
        j.set("sensors", std::move(jr));
    }
    return j;
}

} // namespace tf
