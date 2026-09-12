#pragma once
//
// Etat et pages de l'interface. Ne contient aucune logique metier : tout passe
// par tfcore (Engine, hw::detect, capteurs). Exactement les memes appels que
// la ligne de commande.
//
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "core/engine.hpp"
#include "hw/gpu.hpp"
#include "hw/sensors.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"
#include "ui/window.hpp"

namespace tf::ui {

// Tampon circulaire pour les courbes.
struct History {
    static constexpr int kCapacity = 160;
    float values[kCapacity]{};
    int   offset = 0;
    float last = 0.0f;
    float vmax = 1.0f;
    bool  primed = false;

    void  push(float v);
};

enum class Page { Dashboard, Tweaks, Gpu, Restore, Hardware, Settings };

// Parcours de la page Reglages. Plutot que de demander un « niveau » abstrait,
// on pose une question par reglage en montrant son gain ET sa contrepartie :
// le consentement est ainsi donne reglage par reglage, en connaissance de cause.
enum class TweakFlow { Intro, Question, Recap, Done };

enum class Answer { None, Yes, No };

// Deux niveaux seulement. Ils ne debloquent rien par eux-memes : ils decident
// simplement de la longueur du questionnaire, donc du nombre de reglages sur
// lesquels l'utilisateur sera consulte.
enum class OptLevel { Basic, Expert };

// Pilotage des ventilateurs. NVAPI n expose qu un niveau fixe : la courbe est
// donc tenue par l app, qui relit la temperature et reecrit le niveau. Si l app
// se ferme, le pilote doit reprendre la main — sinon les ventilateurs restent
// bloques au dernier niveau ecrit, meme apres la fermeture.
enum class FanMode { Driver, Fixed, Curve };

class App {
public:
    // Le destructeur rend les ventilateurs au pilote : sans cela, fermer
    // l app les laisserait bloques au dernier niveau ecrit.
    ~App();

    bool Init(Window* window);
    void Frame();

    // Page ouverte au demarrage (option --page). Utile pour un raccourci qui
    // pointe directement sur une section.
    void SetPage(Page p) { page_ = p; }

    // Ouvre directement le questionnaire (--page quiz).
    void StartQuiz();

    // Vrai une seule fois quand l'utilisateur a change l'echelle d'interface :
    // les polices et le style doivent etre recharges ENTRE deux images.
    bool ConsumeStyleReload();

private:
    void DrawTitleBar(float width);
    void DrawSidebar();
    void DrawContent();

    void PageDashboard();
    void PageTweaks();
    void PageTweaksIntro();   // avertissement + lancement du questionnaire
    void PageTweaksQuiz();    // une question par reglage
    void PageTweaksRecap();   // recapitulatif avant application
    void PageTweaksDone();    // compte rendu apres application
    void PageGpu();           // reglages GPU volatiles (v0.3)
    void PageGpuFans(float width);   // carte ventilateurs : mode et courbe
    // Pourquoi la courbe n ecrit rien, ou chaine vide si elle ecrit bien.
    std::string FanCurveBlocker() const;
    void PageRestore();       // tout ce que Tuneforge peut annuler
    void BuildQuiz(OptLevel level);
    void PageHardware();
    void PageSettings();      // theme, echelle, journal
    void DrawJournal();       // rendu du journal, affiche dans les parametres
    void WriteDiagnosticReport();   // ecrit le rapport et l'ouvre dans l'explorateur
    void SaveUiPrefs();

    // Applique la courbe si elle est active : lit la temperature, calcule le
    // niveau, et n ecrit que si l ecart justifie de traverser le pilote.
    void ApplyFanCurve();
    // Rend la main au pilote. Appelee a la fermeture, quoi qu il arrive.
    void ReleaseFans();

public:
    // Appelee avant la creation du style : le theme et l'echelle doivent etre
    // connus au premier chargement des polices.
    static void LoadUiPrefsStatic();

private:

    void SampleTelemetry();
    void RefreshLog();
    void Notify(const std::string& text, Status s);
    void DrawNotice();
    // Renvoie true uniquement si des reglages ont reellement ete appliques.
    bool ApplySelection(const std::vector<std::string>& ids, bool advanced,
                        bool expert = false);

    Window*                 window_ = nullptr;
    std::unique_ptr<Engine> engine_;
    Page                    page_ = Page::Dashboard;

    hw::SystemCounters counters_;
    hw::CpuFrequency   cpu_freq_;
    hw::HwInfoSensors  sensors_;
    hw::Nvml           nvml_;
    bool               has_hwinfo_ = false;
    bool               has_nvml_ = false;
    float              cpu_mhz_ = -1.0f;

    // Reglages GPU volatiles, par le controleur du fabricant detecte.
    // Rafraichi a cadence reduite : chaque appel traverse le pilote.
    std::unique_ptr<hw::IGpuController> gpu_ctl_;
    std::string        gpu_absent_;   // pourquoi il n'y en a pas, le cas echeant
    double             last_gpu_ = 0.0;
    int                power_target_ = 100;
    int                core_target_ = 0;
    int                mem_target_ = 0;
    int                fan_target_ = 50;

    // Ventilateurs : mode courant, courbe, et derniere valeur reellement
    // ecrite (pour ne pas reecrire le meme niveau a chaque image).
    FanMode                 fan_mode_ = FanMode::Driver;
    std::vector<FanPoint>   fan_curve_;
    int                     fan_curve_written_ = -1;
    double                  last_curve_ = 0.0;
    bool                    fans_taken_ = false;   // l app tient les ventilateurs

    hw::SystemSnapshot snapshot_{};
    hw::GpuTelemetry   gpu_{};
    double             last_sample_ = 0.0;
    double             last_log_read_ = 0.0;

    History cpu_hist_, gpu_hist_, cpu_temp_hist_, gpu_temp_hist_;
    float   cpu_temp_ = -1.0f, cpu_power_ = -1.0f, cpu_clock_ = -1.0f;

    std::vector<std::string> log_lines_;

    // Page Reglages : questionnaire, puis liste complete.
    TweakFlow                flow_ = TweakFlow::Intro;
    OptLevel                 level_ = OptLevel::Basic;
    std::vector<std::string> quiz_ids_;      // un reglage par question
    std::vector<Answer>      quiz_answers_;  // meme indice que quiz_ids_
    size_t                   quiz_index_ = 0;
    size_t                   quiz_already_ = 0;  // deja dans l'etat cible
    Engine::Outcome          last_outcome_;      // resultat de la derniere application

    // Bandeau de notification ephemere.
    std::string notice_;
    Status      notice_status_ = Status::Accent;
    double      notice_until_ = 0.0;

    bool elevated_ = false;
    bool style_reload_ = false;
};

} // namespace tf::ui
