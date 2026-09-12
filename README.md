# Tuneforge

[![build](https://github.com/CR43K3L/Tuneforge/actions/workflows/build.yml/badge.svg)](https://github.com/CR43K3L/Tuneforge/actions/workflows/build.yml)

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
git clone --recursive https://github.com/CR43K3L/Tuneforge
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

`tuneforge-gui.exe` **demande l'élévation dès son lancement** : chaque écriture GPU comme
chaque réglage Windows la réclame, et relancer à mi-parcours faisait perdre l'état de la
page en cours. La ligne de commande, elle, reste en `asInvoker` — `detect`, `list` et
`monitor` n'ont aucune raison d'ouvrir une invite UAC. Pour compiler une interface sans
élévation (développement uniquement) : `cmake -DTF_GUI_REQUIRE_ADMIN=OFF`.

Fenêtre sans chrome Windows, barre latérale, télémétrie en direct.
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

## GPU NVIDIA (v0.3)

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

### Courbe de ventilateur

NVAPI ne sait pas confier une courbe au pilote : il n'accepte qu'un **niveau fixe**. La
courbe est donc tenue par Tuneforge, qui relit la température et réécrit le niveau toutes
les deux secondes — et seulement quand l'écart dépasse trois points, sinon le ventilateur
chanterait en suivant chaque variation.

Cela a une conséquence directe : **la courbe n'existe que tant que l'interface tourne.**
Trois choses en découlent, toutes traitées :

- À la fermeture, les ventilateurs sont rendus au pilote. Sans cela ils resteraient
  bloqués au dernier niveau écrit, y compris après la fin du processus.
- Si Tuneforge meurt sans passer par là (plantage, arrêt forcé), `tuneforge-reset.exe`
  les rend au pilote dès son lancement, avant même de regarder s'il a des réglages
  Windows à restaurer.
- Une consigne refusée ne fait pas boucler : le mode courbe est abandonné et le pilote
  reprend la main.

Le plancher est à **30 %** — en dessous, la carte refuse la consigne. Le graphe dessine
cette bande en creux plutôt que de la retirer de l'axe : une limite qu'on ne voit pas est
une limite contre laquelle on bute.

La courbe est éditée à la souris dans **Overclock GPU**, et enregistrée dans `ui.json`.
Les points restent ordonnés en température et monotones en niveau : une courbe qui
redescend ferait osciller le ventilateur autour du point d'inversion.

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
| **v0.5** 🚧 | Couche GPU indépendante du fabricant ✅ · rapports de diagnostic ✅ · sondage AMD ✅ · réglage AMD en attente d'une carte dédiée | bêta publique GitHub |
| **v1.0** | Certificat de signature de code, installeur, mise à jour automatique, FR/EN | publique |

Le passage v0.5 → v1.0 est celui où apparaissent tous les cas matériels imprévus.
Il ne doit pas être sauté.


---

## Support multi-fabricants (v0.5, en cours)

`src/hw/gpu.hpp` définit ce dont l'application a besoin — mesures, capacités,
`IGpuController` — et chaque liaison s'y conforme. L'interface et la ligne de commande ne
nomment plus aucun fabricant : elles demandent au contrôleur ce qu'il **sait faire**, et
une capacité absente désactive la commande au lieu de la faire échouer au dernier moment.

Le contrôleur est choisi d'après le fabricant réellement présent sur le bus PCI, pas selon
une séquence fixe : charger `nvapi64.dll` sur une machine AMD ne renvoie rien d'utile et
produirait un message d'erreur trompeur. Quand aucune liaison ne convient, **la raison est
affichée et journalisée** — sur une machine qu'on n'a pas sous la main, c'est la seule
chose qui permettra de comprendre.

### Pourquoi ADL et pas ADLX

ADLX est l'API moderne d'AMD, mais deux choses l'écartent pour l'instant :

- Elle est distribuée sous un **accord de licence propriétaire** (un PDF ; GitHub ne lui
  reconnaît aucune licence libre). Embarquer ses en-têtes dans un projet GPLv3 n'est pas
  une décision qui se prend à la légère.
- Son interface est faite d'objets à table de fonctions virtuelles. En reconstruire les
  tables sans les en-têtes officielles reviendrait à **deviner un ordre d'appel** : se
  tromper d'une entrée, c'est appeler une autre fonction que celle voulue. Ce projet
  s'interdit déjà exactement cela côté NVIDIA.

ADL, elle, est une API C plate : des fonctions indépendantes résolues une à une par
`GetProcAddress` dans `atiadlxx.dll`, exactement comme `nvapi64.dll`. Le précédent
s'applique donc — liaison dynamique, aucune en-tête du fabricant embarquée.

### Ce que le sondage AMD fait, et ne fait pas

Il n'appelle **que des fonctions dont tous les paramètres sont des entiers** : nombre
d'adaptateurs, état d'activité, capacités Overdrive. Aucune structure n'est échangée avec
le pilote, donc aucune disposition n'est supposée — c'est ce qui le rend sûr à exécuter
sur une machine inconnue. Il ne règle rien.

Il répond à la seule question qui bloque la suite : **quelle génération d'Overdrive cette
carte expose-t-elle ?** La réponse décide de ce qu'il faudra implémenter, et elle ne peut
venir que de vraies machines AMD.

Première mesure, sur un Radeon intégré (Ryzen 7 7800X3D, pilote ADLX 1.4.0.121) :

| Indice | `ADL2_Adapter_Active_Get` | `ADL2_Overdrive_Caps` |
|---|---|---|
| 0 – 4 | OK | non supporté par cet adaptateur |
| 5 – 8 | OK | indice d'adaptateur invalide |

Deux enseignements, tous deux contre-intuitifs :

1. `ADL2_Adapter_NumberOfAdapters_Get` annonce **neuf** adaptateurs, mais quatre indices
   sur neuf sont ensuite rejetés comme invalides. **Le compte et l'espace d'indices ne
   coïncident pas** : il faut interroger chaque indice et garder ceux qui répondent, pas
   itérer aveuglément de 0 à N-1.
2. `Active_Get` accepte des indices qu'`Overdrive_Caps` rejette ensuite. Aucune des deux
   fonctions ne suffit donc à décider seule ce qu'est un adaptateur réel. Le rapport
   publie **les deux codes bruts** plutôt que de trancher avec une règle inventée.

Le « non supporté » sur un GPU intégré est la bonne réponse, pas une panne : ces puces
n'ont pas d'Overdrive. **Le réglage AMD reste donc à écrire, et il ne sera pas écrit à
l'aveugle** — il faut d'abord un rapport venant d'une carte AMD dédiée.

### Le rapport de diagnostic

C'est la pièce qui débloque le reste. `tuneforge report` produit maintenant une section
`gpu` : cartes vues sur le bus, liaison retenue ou raison de son absence, capacités
réellement obtenues, sondage AMD, et le détail propre à la liaison — pour NVAPI, la table
des points d'entrée, qui explique d'un coup pourquoi telle capacité manque sur telle
version de pilote.

```powershell
tuneforge report mon-rapport.json
```

Aucun nom d'utilisateur, aucun numéro de série.

---

## Aider

Tuneforge n'a été développé et testé que sur **une seule machine** : un Ryzen 7 7800X3D
avec une RTX 4070 SUPER. Tout le reste est de la théorie, et la théorie n'a jamais fait
tourner un ventilateur.

La contribution la plus utile n'est pas du code, c'est **un rapport de diagnostic depuis
une machine différente** :

1. **Paramètres → Diagnostic → Générer un rapport** (ou `tuneforge report mon-rapport.json`)
2. Ouvrez une issue « Rapport de compatibilité matérielle » et joignez le fichier

Les configurations qui manquent, par ordre d'utilité :

| Configuration | Ce qu'elle débloque |
|---|---|
| **GPU AMD dédié** | Le réglage AMD, aujourd'hui absent. Sans un rapport venant d'une vraie carte, il ne sera pas écrit — deviner l'API d'un fabricant est exactement ce que ce projet s'interdit. |
| **Processeur Intel** | La détection CPU et les réglages d'alimentation n'ont jamais tourné sur autre chose qu'un Ryzen. |
| **Portable** | Les plans d'alimentation, la gestion sur batterie et la suspension USB s'y comportent différemment. |
| **GPU NVIDIA plus ancien** | Les identifiants NVAPI non documentés changent d'une génération de pilote à l'autre. Le rapport dit lesquels ont été résolus. |

### Ce que la CI vérifie

Le runner GitHub n'a **ni carte NVIDIA ni carte AMD** — c'est justement le cas qu'aucune
machine de développement ne reproduit. Chaque compilation vérifie donc que les commandes
de lecture aboutissent sur une machine sans GPU supporté, que le rapport dit *pourquoi*
plutôt que de rester muet, et que les commandes d'écriture mal formées échouent avant de
demander l'élévation. Le rapport produit dans ces conditions est conservé comme artefact.

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
