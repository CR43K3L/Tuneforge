//
// Choix du controleur GPU — v0.5.
//
// L'ordre d'essai suit le fabricant reellement detecte sur le bus PCI plutot
// qu'une sequence fixe : charger nvapi64.dll sur une machine AMD ne renvoie
// rien d'utile, et le message d'echec qui en resulterait serait trompeur.
//
#include "hw/gpu.hpp"

#include "hw/adl.hpp"
#include "hw/nvapi.hpp"

namespace tf::hw {

GpuCapabilities NvapiController::capabilities() const {
    GpuCapabilities c;
    c.set_power_limit  = nv_.can_set_power_limit();
    c.set_clock_offset = nv_.can_set_clock_offset();
    c.set_fan          = nv_.can_set_fan();
    return c;
}

Json NvapiController::backend_details() const {
    Json j = Json::object();
    Json entries = Json::array();
    int  resolved = 0;
    for (const auto& e : nv_.entry_points()) {
        Json x = Json::object();
        x.set("name", e.name);
        x.set("resolved", e.resolved);
        entries.push(std::move(x));
        if (e.resolved) ++resolved;
    }
    j.set("entry_points_resolved", static_cast<int64_t>(resolved));
    j.set("entry_points_total", static_cast<int64_t>(nv_.entry_points().size()));
    j.set("entry_points", std::move(entries));
    return j;
}

namespace {

// Vrai si la machine porte au moins une carte de ce fabricant. On interroge la
// detection PCI, qui ne depend d'aucun pilote proprietaire : elle repond meme
// quand le fabricant n'a rien installe.
bool has_vendor(GpuVendor v) {
    for (const auto& g : detect().gpus) {
        if (g.vendor == v) return true;
    }
    return false;
}

std::string describe_gpus() {
    std::string out;
    for (const auto& g : detect().gpus) {
        if (!out.empty()) out += ", ";
        out += g.description.empty() ? to_string(g.vendor) : g.description;
    }
    return out.empty() ? "aucune carte detectee" : out;
}

}  // namespace

std::unique_ptr<IGpuController> make_gpu_controller(std::string* why_not) {
    auto fail = [&](std::string reason) -> std::unique_ptr<IGpuController> {
        log_info("aucun controleur GPU : {}", reason);
        if (why_not) *why_not = std::move(reason);
        return nullptr;
    };

    if (has_vendor(GpuVendor::Nvidia)) {
        auto ctl = std::make_unique<NvapiController>();
        if (!ctl->init()) {
            return fail("carte NVIDIA detectee mais NVAPI n'a pas pu etre initialise : " +
                        ctl->raw().load_error());
        }
        if (!ctl->refresh() || ctl->gpu_count() == 0) {
            return fail("NVAPI initialise mais n'a rapporte aucune carte");
        }
        log_info("controleur GPU : NVAPI, {} carte(s), pilote {}", ctl->gpu_count(),
                 ctl->driver_version());
        return ctl;
    }

    // AMD : la liaison ADLX n'est pas encore ecrite. Le dire explicitement vaut
    // mieux que de laisser croire a une carte non reconnue — c'est la premiere
    // chose qu'un testeur AMD verra, et il doit comprendre que le manque vient
    // de Tuneforge et non de son materiel.
    if (has_vendor(GpuVendor::Amd)) {
        const AdlProbe p = probe_adl();
        std::string    detail;
        if (!p.loaded) {
            detail = p.error;
        } else {
            int od = 0;
            for (const auto& a : p.adapters) {
                if (a.overdrive_supported) od = a.overdrive_version;
            }
            if (od) {
                detail = "ADL repond, Overdrive version " + std::to_string(od) +
                         " — le reglage n'est pas encore implemente";
            } else {
                // La raison exacte du refus, pas un simple « rien trouve » :
                // « non supporte par cet adaptateur » sur un GPU integre est
                // une reponse correcte, pas une panne.
                int worst = 0;
                for (const auto& a : p.adapters) {
                    // -5 signale un indice qu'ADL ne connait pas : ce n'est pas
                    // un verdict sur le materiel, seulement sur l'indice.
                    if (a.caps_status != 0 && a.caps_status != -5) worst = a.caps_status;
                }
                detail = std::string("ADL repond mais aucun Overdrive expose (") +
                         adl_status_text(worst) + ")";
            }
        }
        return fail("carte AMD detectee : " + detail);
    }
    if (has_vendor(GpuVendor::Intel)) {
        return fail("carte Intel detectee — aucun reglage GPU n'est prevu pour ce "
                    "fabricant");
    }

    return fail("aucun fabricant supporte parmi : " + describe_gpus());
}


// ---------------------------------------------------------------------------
// Rapport
// ---------------------------------------------------------------------------
Json IGpuController::reading_to_json(size_t index) const {
    Json j = Json::object();
    if (index >= gpu_count()) return j;
    const GpuReading& g = gpu(index);
    j.set("name", g.name);

    if (g.clocks.valid) {
        Json c = Json::object();
        c.set("graphics_mhz", static_cast<int64_t>(g.clocks.graphics_khz / 1000));
        c.set("memory_mhz", static_cast<int64_t>(g.clocks.memory_khz / 1000));
        j.set("clocks", std::move(c));
    }
    if (g.thermal.valid) {
        Json t = Json::object();
        t.set("gpu_c", static_cast<int64_t>(g.thermal.gpu_c));
        t.set("sensor_count", static_cast<int64_t>(g.thermal.sensor_count));
        j.set("thermal", std::move(t));
    }
    if (g.voltage.valid) j.set("voltage_v", g.voltage.volts());
    if (g.power.valid) {
        Json p = Json::object();
        p.set("current_pct", g.power.current_pct);
        p.set("default_pct", g.power.default_pct);
        p.set("min_pct", g.power.min_pct);
        p.set("max_pct", g.power.max_pct);
        p.set("editable", g.power.editable);
        j.set("power_limit", std::move(p));
    }
    if (g.offsets.valid) {
        Json o = Json::object();
        o.set("graphics_delta_mhz", static_cast<int64_t>(g.offsets.graphics_delta_khz / 1000));
        o.set("memory_delta_mhz", static_cast<int64_t>(g.offsets.memory_delta_khz / 1000));
        o.set("graphics_min_mhz", static_cast<int64_t>(g.offsets.graphics_min_khz / 1000));
        o.set("graphics_max_mhz", static_cast<int64_t>(g.offsets.graphics_max_khz / 1000));
        o.set("pstate_count", static_cast<int64_t>(g.offsets.pstate_count));
        o.set("editable", g.offsets.editable);
        j.set("clock_offsets", std::move(o));
    }
    if (g.coolers.valid) {
        Json f = Json::array();
        for (const auto& c : g.coolers.items) {
            Json e = Json::object();
            e.set("index", static_cast<int64_t>(c.index));
            e.set("level_pct", static_cast<int64_t>(c.current_level));
            e.set("active", c.active);
            e.set("range_known", c.range_known);
            f.push(std::move(e));
        }
        Json fans = Json::object();
        fans.set("items", std::move(f));
        if (g.coolers.tach_valid) fans.set("tach_rpm", static_cast<int64_t>(g.coolers.tach_rpm));
        j.set("fans", std::move(fans));
    }
    return j;
}

Json gpu_diagnostic_json() {
    Json j = Json::object();

    // Ce que le bus PCI annonce, independamment de tout pilote proprietaire.
    // Sur une machine ou aucune liaison ne repond, c'est la seule chose qui
    // restera — et elle suffit a savoir quel fabricant il faudrait supporter.
    Json detected = Json::array();
    for (const auto& g : detect().gpus) {
        Json e = Json::object();
        e.set("vendor", to_string(g.vendor));
        e.set("description", g.description);
        e.set("vendor_id", static_cast<int64_t>(g.vendor_id));
        e.set("device_id", static_cast<int64_t>(g.device_id));
        e.set("integrated", g.is_integrated);
        e.set("driver_version", g.driver_version);
        detected.push(std::move(e));
    }
    j.set("detected", std::move(detected));

    // Sondage AMD, qu'un controleur ait ete trouve ou non. Sur une machine
    // NVIDIA avec un Radeon integre il documente le materiel secondaire ;
    // sur une machine purement AMD, c'est la seule chose qui dira quelle
    // generation d'Overdrive il faudra implementer.
    for (const auto& g : detect().gpus) {
        if (g.vendor != GpuVendor::Amd) continue;
        j.set("amd_probe", adl_probe_json(probe_adl()));
        break;
    }

    std::string why;
    auto        ctl = make_gpu_controller(&why);
    if (!ctl) {
        j.set("controller", nullptr);
        j.set("unsupported_reason", why);
        return j;
    }

    j.set("controller", ctl->backend_name());
    j.set("vendor", to_string(ctl->vendor()));
    j.set("driver_version", ctl->driver_version());

    const GpuCapabilities caps = ctl->capabilities();
    Json                  c = Json::object();
    c.set("set_power_limit", caps.set_power_limit);
    c.set("set_clock_offset", caps.set_clock_offset);
    c.set("set_fan", caps.set_fan);
    j.set("capabilities", std::move(c));

    Json gpus = Json::array();
    for (size_t i = 0; i < ctl->gpu_count(); ++i) gpus.push(ctl->reading_to_json(i));
    j.set("gpus", std::move(gpus));

    j.set("backend_details", ctl->backend_details());
    return j;
}

} // namespace tf::hw
