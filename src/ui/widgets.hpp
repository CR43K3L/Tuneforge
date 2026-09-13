#pragma once
//
// Composants de l'interface. Aucun n'ecrit de couleur en dur : tout vient de
// theme.hpp. C'est ce qui permet de changer le theme en un seul endroit.
//
#include <string>
#include <vector>

#include "imgui.h"
#include "ui/theme.hpp"

namespace tf::ui {

// --- Texte -----------------------------------------------------------------
void TextAt(ImFont* font, float size, const ImVec4& color, const char* text);
void Display(const char* text);                       // titre de page
void H1(const char* text);                            // titre de section
void CardTitle(const char* text);
void Body(const char* text);
void Muted(const char* text);
void Small(const char* text, const ImVec4& color);
void Mono(const char* text, const ImVec4& color);
void SectionLabel(const char* text);                  // etiquette de groupe
void WrappedMuted(const char* text, float wrap_width);

// --- Primitives ------------------------------------------------------------
void Divider(float vertical_margin = -1.0f);
void VSpace(float h);
void Dot(Status s, float radius = -1.0f, bool filled = true);
void StatusPill(const char* text, Status s);
void Badge(const char* text, const ImVec4& fg, const ImVec4& bg);

// --- Cartes ----------------------------------------------------------------
// Une carte englobe un bloc de contenu sur une surface elevee.
// A appeler quand le contenu affiche change de page : les cartes rejouent
// alors leur entree, decalee de l'une a l'autre. Sans cela l'animation ne se
// verrait qu'une fois dans la vie du programme.
void BeginContentEpoch();
void ResetCardStagger();
// Numero de la page affichee depuis le lancement. Sert de cle aux
// animations qui doivent rejouer a chaque changement de page.
int  ContentEpoch();

bool BeginCard(const char* id, const ImVec2& size = ImVec2(0, 0), bool interactive = false);
void EndCard();
void CardHeader(const char* title, const char* subtitle = nullptr);

// --- Navigation ------------------------------------------------------------
bool NavItem(const char* label, bool selected, Status indicator = Status::Neutral);

// --- Controles -------------------------------------------------------------
bool Toggle(const char* id, bool* value, bool enabled = true);
// Incrementeur segmente : « moins | valeur | plus » dans un seul cadre, pour
// que l'ensemble se lise comme un controle et non comme trois elements poses
// cote a cote. Renvoie -1, 0 ou +1.
int Stepper(const char* id, const char* value, bool can_decrease, bool can_increase,
            float width = 0.0f);

// Les trois boutons partagent un meme cycle d'etats. Le parametre par defaut
// laisse intacts tous les appels qui n'en ont pas besoin.
enum class BtnState;
bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(0, 0), bool enabled = true,
                   BtnState state = BtnState{});
bool GhostButton(const char* label, const ImVec2& size = ImVec2(0, 0), bool enabled = true,
                 BtnState state = BtnState{});
bool DangerButton(const char* label, const ImVec2& size = ImVec2(0, 0), bool enabled = true,
                  BtnState state = BtnState{});

// --- Donnees ---------------------------------------------------------------
// Tuile de mesure : libelle, valeur, unite, et une barre de remplissage
// optionnelle (fraction < 0 pour la masquer).
void MetricTile(const char* label, const char* value, const char* unit, float fraction,
                Status s, const ImVec2& size);

// Courbe compacte, sans echelle : reservee aux endroits ou seule la tendance
// compte. Pour une valeur qu'on doit pouvoir lire, utiliser Chart.
void Sparkline(const char* id, const float* values, int count, int offset, float vmin,
               float vmax, const ImVec2& size, Status s);

// Graphique lisible : graduations, echelle, valeur courante et fenetre
// temporelle. Un graphe sans echelle est une decoration, pas une mesure.
struct ChartOptions {
    float       vmin = 0.0f;
    float       vmax = 100.0f;
    const char* unit = "%";
    const char* x_label = nullptr;    // ex. « 40 dernieres secondes »
    Status      status = Status::Accent;
    int         grid_lines = 3;       // graduations intermediaires
    bool        show_current = true;  // etiquette de la derniere mesure
};

// `values` est un tampon circulaire de `count` elements dont le plus recent est
// a l'indice `offset - 1`.
void Chart(const char* id, const float* values, int count, int offset, const ImVec2& size,
           const ChartOptions& opt);

void ProgressBar(float fraction, const ImVec2& size, Status s);

// --- Courbe de ventilateur --------------------------------------------------
// Temperature en abscisse, niveau en ordonnee. NVAPI ne sait pas confier une
// courbe au pilote : elle est donc appliquee par l app, qui relit la
// temperature et reecrit le niveau. Le widget ne fait que l editer.
struct FanPoint {
    int temp_c = 0;
    int level_pct = 0;
};

struct FanCurveView {
    int   temp_min = 20, temp_max = 95;
    // L ordonnee va toujours de 0 a 100 %, mais la carte refuse les consignes
    // sous le plancher : la bande correspondante est dessinee en creux plutot
    // que retiree de l axe, sinon l utilisateur bute sur une limite invisible.
    int   level_floor = 30;
    float live_temp_c = -1.0f;               // < 0 : pas de repere de mesure
    bool  editable = true;
};

// Niveau interpole a une temperature donnee. Plat avant le premier point et
// apres le dernier.
int FanCurveLevelAt(const FanPoint* pts, int count, float temp_c);

// Renvoie true quand un point vient d etre deplace. Les points restent
// ordonnes en temperature et monotones en niveau.
bool FanCurveEditor(const char* id, FanPoint* pts, int count, const ImVec2& size,
                    const FanCurveView& v);

// --- Divers ----------------------------------------------------------------
void HelpMarker(const char* text);
// Interpolation exponentielle stable quel que soit le pas de temps.
float Animate(ImGuiID id, float target, float speed = 14.0f);

// ===========================================================================
// Refonte v0.6 — primitives de profondeur, de mouvement et d'etat
// ===========================================================================
//
// Trois regles gouvernent tout ce qui suit.
//
// 1. Une animation sert a EXPLIQUER un changement, pas a decorer. Si elle
//    n'aide pas a comprendre ce qui vient de se passer, elle est retiree.
// 2. Rien n'est anime plus longtemps que MO().base pour une interaction
//    directe : au-dela, l'interface se sent molle.
// 3. Une valeur MESUREE n'est jamais inventee. On lisse sa representation,
//    jamais la mesure elle-meme, et on ne depasse jamais la cible.

// --- Mouvement --------------------------------------------------------------
// Horloge a sens unique : 0 -> 1 en `duration` secondes, puis reste a 1. Pour
// les entrees et les progressions, la ou une convergence exponentielle
// n'atteindrait jamais tout a fait sa cible.
float Timeline(ImGuiID id, float duration, bool run = true);
void  TimelineReset(ImGuiID id);

// Lissage d'une valeur mesuree vers sa cible. La mesure reste intacte : seule
// sa representation glisse. `speed` est en unites par seconde relatives.
float SmoothValue(ImGuiID id, float target, float speed = 9.0f);

// --- Profondeur -------------------------------------------------------------
// ImGui ne sait pas flouter. Une ombre portee douce s'obtient par empilement
// de contours de plus en plus transparents : c'est peu couteux et suffisant
// tant que l'etalement reste petit.
void SoftShadow(ImDrawList* dl, const ImVec2& a, const ImVec2& b, float rounding,
                float spread, const ImVec4& color);

// Degrade vertical. Donne aux surfaces une inclinaison de lumiere coherente :
// plus clair en haut, comme si la source etait au-dessus de l'ecran.
void VerticalGradient(ImDrawList* dl, const ImVec2& a, const ImVec2& b, const ImVec4& top,
                      const ImVec4& bottom, float rounding);

// Filet clair sur l'arete superieure. C'est ce detail, plus que la couleur,
// qui fait qu'une surface parait posee sur le fond plutot que peinte dessus.
void TopHairline(ImDrawList* dl, const ImVec2& a, const ImVec2& b, float rounding,
                 const ImVec4& color);

// --- Etats de bouton ---------------------------------------------------------
// Un bouton qui declenche une operation longue doit dire ou elle en est. Les
// trois etats transitoires reviennent d'eux-memes a Idle, c'est l'appelant qui
// decide quand.
enum class BtnState { Idle, Loading, Success, Error };

// --- Jauge circulaire --------------------------------------------------------
struct GaugeOpts {
    float       vmax = 100.0f;
    const char* unit = "%";
    Status      status = Status::Accent;
    // Valeur secondaire affichee sous le chiffre principal (temperature,
    // frequence...). Ignoree si nulle.
    const char* caption = nullptr;
    // < 0 : pas de mesure disponible. La jauge le dit au lieu d'afficher zero,
    // qui serait une mesure fausse.
    bool        valid = true;
};
void Gauge(const char* id, const char* label, float value, const ImVec2& size,
           const GaugeOpts& opt);

// --- Liste d'etapes ----------------------------------------------------------
// Utilisee par l'ecran d'application. Chaque etape reflete une operation
// reellement effectuee par le moteur — aucune n'est ajoutee pour meubler.
enum class StepState { Pending, Running, Done, Failed, Skipped };

struct StepItem {
    std::string label;
    std::string detail;   // facultatif, affiche en petit a droite
    StepState   state = StepState::Pending;
};

void StepList(const char* id, const StepItem* items, int count, float width);

// --- Barre de progression ----------------------------------------------------
// `fraction` < 0 : progression inconnue, la barre defile au lieu de mentir sur
// un pourcentage.
void ProgressTrack(const char* id, float fraction, float width, Status s);

// --- Squelette de chargement --------------------------------------------------
void Skeleton(const char* id, const ImVec2& size);

// --- Voile modal ---------------------------------------------------------------
// Assombrit toute la fenetre. `t` est la progression d'ouverture [0,1].
void Scrim(float t);

} // namespace tf::ui
