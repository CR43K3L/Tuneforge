# Tuneforge

Outil d'optimisation Windows **entièrement réversible**, écrit en C++20, sans driver noyau.

> ⚠️ **v0.1 — usage privé.** Cette version n'est pas signée et n'est pas destinée
> à une diffusion publique. Voir [Feuille de route](#feuille-de-route).

---

## Ce que fait Tuneforge

Il applique un ensemble restreint de réglages Windows dont l'effet est **mesurable** :
plan d'alimentation, core parking, priorités d'ordonnancement MMCSS, planification GPU
matérielle, bridage réseau, algorithme de Nagle, résolution du timer.

## Ce qu'il ne fait pas — et ne fera pas dans cette version

- **Aucune tension, aucune fréquence matérielle.** Pas d'overclock, pas d'undervolt.
- **Aucun driver noyau.** Rien n'est chargé en ring 0.
- **Aucun « nettoyage de registre », aucun « RAM booster ».** Ces fonctions n'ont
  jamais rien accéléré ; elles ne servent qu'à donner l'impression que l'outil travaille.
- **Aucune promesse chiffrée de gain de FPS.**

## Principe de conception

Trois mécanismes de sécurité, présents dès la v0.1 parce qu'ils sont impossibles à
rajouter proprement après coup :

| Mécanisme | Rôle |
|---|---|
| **Instantané avant modification** | L'état exact est capturé et persisté *avant* toute écriture. « La valeur n'existait pas » est un état restaurable : la restauration la supprime. |
| **Drapeau d'application** | Un lot interrompu (crash, coupure, arrêt brutal) est détecté au démarrage suivant et se répare avec `tuneforge recover`. |
| **Chien de garde** | `--watchdog N` annule automatiquement si l'utilisateur ne confirme pas dans le délai — le même principe qu'un changement de résolution d'écran. |

Et une règle qui gouverne tout le reste :

> **Matériel inconnu = fonctionnalité désactivée.** Aucun réglage ne s'applique « au cas où ».
> Chaque réglage déclare ce dont il a besoin, et la couche de détection dit si la machine
> le fournit.

---

## Compilation

Prérequis : Visual Studio 2022 Build Tools (composant C++) et CMake.

```powershell
winget install Kitware.CMake
winget install Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Dear ImGui est un sous-module, nécessaire à l'interface graphique. Clonez avec
`--recursive` :

```powershell
git clone --recursive https://github.com/<compte>/Tuneforge
cd Tuneforge
.\build.ps1
```

Si le clone a été fait sans `--recursive`, `build.ps1` récupère le sous-module tout seul.
Sans Dear ImGui, CMake le signale et produit uniquement les binaires en ligne de commande.

Les binaires sortent dans `build\bin\` :

- `tuneforge-gui.exe` — l'interface graphique
- `tuneforge.exe` — la ligne de commande
- `tuneforge-reset.exe` — restauration d'urgence autonome, sans dépendance à la configuration

Le runtime C++ est lié statiquement : aucun VC++ Redistributable n'est requis chez l'utilisateur.

---

## Interface graphique

`tuneforge-gui.exe` — fenêtre sans chrome Windows, barre latérale, télémétrie en direct.
Six pages : Tableau de bord, Optimisations, Overclock GPU, Restauration, Matériel,
Paramètres.

Quatre thèmes sont livrés — Ardoise & cyan, Encre & violet, OLED & acier, Ardoise &
magenta — et se choisissent dans **Paramètres**, avec l'échelle de l'interface et le
journal. Les couleurs d'état (vert, ambre, rouge) sont identiques dans les quatre : une
couleur, un sens.

La page **Optimisations** ne présente pas un catalogue : elle déroule un **questionnaire**.
Vous choisissez d'abord un niveau (Basique ou Expert, qui détermine la longueur du
questionnaire), puis une question par réglage affiche côte à côte **ce qu'il apporte et ce
qu'il coûte**, à hauteur égale. Rien n'est appliqué avant le récapitulatif final.

Les réglages déjà dans l'état visé sur la machine ne font pas l'objet d'une question :
y répondre ne changerait rien. Le nombre de questions dépend donc de la machine.

La page **Restauration** liste tout ce que Tuneforge a modifié et permet de l'annuler,
individuellement ou en bloc. Elle reste visible même quand il n'y a rien à restaurer —
et explique alors pourquoi.

Options de ligne de commande : `--page dashboard|reglages|quiz|restauration|gpu|materiel|parametres`.

Elle consomme **exactement le même cœur** que la ligne de commande : `Engine`,
`hw::detect()`, les capteurs. Aucune logique n'y est dupliquée.

Le système de design complet est documenté dans [DESIGN.md](DESIGN.md).

## Ligne de commande

### Lecture — aucune élévation nécessaire

```powershell
tuneforge detect      # matériel, système, sécurité, capacités
tuneforge list        # catalogue des réglages et leur état actuel
tuneforge list --all  # y compris ceux indisponibles sur cette machine
tuneforge status      # ce que Tuneforge a modifié ici
tuneforge monitor     # télémétrie temps réel
tuneforge report      # rapport de diagnostic JSON, anonyme
```

### Modification — élévation requise (relance UAC automatique)

```powershell
tuneforge apply gpu.hags.on --dry-run    # simulation, n'écrit rien
tuneforge apply gpu.hags.on --advanced   # un réglage précis
tuneforge apply power.core_parking.off win.mmcss.games_task --advanced
tuneforge apply gpu.hags.on --advanced --watchdog 30
tuneforge revert gpu.hags.on             # restaure un réglage
tuneforge revert --all                   # restaure tout
tuneforge recover                        # répare un lot interrompu
```

**Commencez toujours par `--dry-run`.**

---

## GPU NVIDIA (v0.3, en cours)

```powershell
tuneforge gpu                    # tout ce que NVAPI expose, en lecture seule
tuneforge gpu --probe-fans       # sonde la disposition de structure des ventilateurs
tuneforge gpu-power 90           # limite de puissance à 90 %, avec chien de garde
tuneforge gpu-power 90 --dry-run # simulation
tuneforge gpu-power --reset      # retour à la valeur par défaut

tuneforge gpu-clock --mem 500    # décalage mémoire, avec chien de garde
tuneforge gpu-clock --core -100  # décalage GPU
tuneforge gpu-clock --reset      # remet les deux à zéro
```

NVAPI n'exporte qu'un seul symbole utile, `nvapi_QueryInterface` ; les fonctions
d'overclocking s'obtiennent par identifiant numérique et ne sont **pas documentées** par
NVIDIA. Conséquence assumée : un identifiant inconnu du pilote renvoie un pointeur nul et
une structure mal versionnée un code d'erreur — jamais un plantage. `tuneforge gpu` liste
précisément ce qui a pu être résolu.

**Ces réglages sont volatiles** : ils disparaissent au redémarrage et au rechargement du
pilote. C'est ce qui les rend sûrs — un mauvais réglage se corrige en rebootant.

Trois garde-fous sur chaque écriture :

1. **Jamais hors bornes.** La valeur est refusée avant tout appel si elle sort de la plage
   que le pilote annonce lui-même.
2. **Relue après écriture.** Un pilote peut accepter l'appel sans appliquer la valeur ;
   un écart fait échouer l'opération.
3. **Chien de garde.** Sans `--yes`, quinze secondes pour confirmer, sinon retour
   automatique à la valeur précédente.

Les écritures exigent les droits administrateur — sans quoi le pilote répond
`NVAPI_INVALID_USER_PRIVILEGE`. Les commandes se relancent en élevé automatiquement.

Les décalages d'horloge ajoutent un delta à la courbe tension/fréquence d'origine, ils ne
la remplacent pas : le pilote continue de gérer les tensions correspondantes.

## Trois niveaux d'exposition

| Niveau | Contenu | Déblocage |
|---|---|---|
| **basique** | 100 % réversible, aucun risque matériel | par défaut |
| **avancé** | effets de bord possibles (chaleur, bruit, compatibilité) | `--advanced` |
| **expert** | réservé à ceux qui savent ce qu'ils font | `--expert` |

---

## Télémétrie

`tuneforge monitor` agrège trois sources, toutes optionnelles :

1. **Compteurs Windows** — charge CPU, mémoire. Toujours disponibles.
2. **NVML** (`nvml.dll`, chargée dynamiquement) — températures, puissance, fréquences et
   VRAM des GPU NVIDIA.
3. **Mémoire partagée HWiNFO** — capteurs détaillés (VRM, puissances, ventilateurs) sans
   écrire la moindre ligne de driver. Lancez HWiNFO64 et activez
   *Settings → Shared Memory Support*.

---

## Fichiers créés

Tout est sous `%LOCALAPPDATA%\Tuneforge\` :

| Fichier | Rôle |
|---|---|
| `state.json` | Instantanés d'origine. **Ne pas supprimer** : c'est ce qui permet de tout restaurer. |
| `apply.lock` | Présent uniquement pendant un lot. S'il subsiste, un lot a été interrompu. |
| `tuneforge.log` | Journal horodaté de toutes les opérations. |
| `rapport-*.json` | Rapports de diagnostic générés à la demande. |

Aucune donnée n'est envoyée nulle part. Le rapport de diagnostic ne contient ni nom
d'utilisateur, ni numéro de série.

---

## En cas de problème

```powershell
tuneforge-reset.exe
```

Il ne lit aucune configuration, ne pose qu'une question,
et remet tout dans l'état capturé avant modification.

---

## Feuille de route

| Version | Contenu | Diffusion |
|---|---|---|
| **v0.1** ✅ | Détection, tweaks Windows réversibles, instantanés, watchdog, monitoring, CLI | privée |
| **v0.2** ✅ | Interface graphique (Dear ImGui + D3D11) sur le même cœur, système de design | privée |
| **v0.3** ✅ | NVAPI sans driver : lecture, limite de puissance, décalages cœur/mémoire, ventilateurs. Courbe V/F reportée (voir plus bas) | cercle restreint |
| **v0.5** | ADLX (AMD), abstraction matérielle éprouvée sur plusieurs configurations, rapports de diagnostic | bêta publique GitHub |
| **v1.0** | Certificat de signature de code, installeur, mise à jour automatique, FR/EN | publique |

Le passage v0.5 → v1.0 est celui où apparaissent tous les cas matériels imprévus.
Il ne doit pas être sauté.

---

## Avertissement

Cet outil modifie des réglages système. Bien qu'il capture l'état d'origine avant chaque
modification et sache le restaurer, l'auteur ne fournit aucune garantie. Vous l'utilisez
sous votre propre responsabilité.

Il ne touche ni à la tension ni à la fréquence du **processeur** : ce réglage-là reste au
BIOS, et aucune version n'y touchera.

Sur le **GPU NVIDIA**, il écrit en revanche trois choses : la limite de puissance, les
décalages d'horloge cœur et mémoire, et la vitesse des ventilateurs. Chacune est bornée
aux valeurs que le pilote lui-même déclare acceptables, relue après écriture, et perdue au
redémarrage. Un décalage d'horloge trop ambitieux peut faire planter le pilote ou un jeu —
c'est le pire qui puisse arriver, et un redémarrage l'efface.

---

## Licence

Tuneforge est distribué sous **GNU General Public License v3.0** — voir [LICENSE](LICENSE).

Vous êtes libre de l'utiliser, de l'étudier, de le modifier et de le redistribuer, à la
condition que toute version modifiée reste elle aussi sous GPLv3 et publie ses sources.

Le **nom « Tuneforge » et son logo ne sont pas couverts par cette licence.** Un fork est
le bienvenu, mais doit porter un autre nom : un outil qui écrit dans le registre et pilote
un GPU ne peut pas se permettre que n'importe quelle variante circule sous la même
identité.

Dear ImGui (`third_party/imgui`) est un sous-module sous licence MIT, dont les termes
restent les siens.
