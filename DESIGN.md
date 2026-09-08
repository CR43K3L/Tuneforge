# Système de design — « Ardoise + cyan glacial »

Tout est défini dans [`src/ui/theme.hpp`](src/ui/theme.hpp). **Aucune couleur et aucun
espacement n'est écrit en dur ailleurs.** Changer le thème se fait dans ce seul fichier.

---

## Règle fondatrice

> **Une couleur = un sens.**

L'accent de marque est **cyan**. C'est un choix fonctionnel avant d'être esthétique :
l'application a besoin de vert, ambre et rouge pour signifier *appliqué*, *contrepartie*
et *indisponible*. Un accent vert ou orange serait entré en collision avec ces trois-là et
aurait rendu l'interface ambiguë au premier coup d'œil.

| Couleur | Sens, et rien d'autre |
|---|---|
| **Cyan** | identité, élément actif, valeur cible, action principale |
| **Vert** | appliqué, disponible, source de données connectée |
| **Ambre** | contrepartie assumée, niveau avancé, redémarrage requis |
| **Rouge** | indisponible, échec, action destructive |
| **Gris** | neutre, inactif, information secondaire |

---

## Surfaces

Cinq niveaux d'élévation, jamais plus. L'ombre n'est pas utilisée à l'intérieur de la
fenêtre : la hiérarchie passe uniquement par la luminosité et une bordure à 1 px.

| Jeton | Hex | Usage |
|---|---|---|
| `sunken` | `#070A0E` | champs de saisie, fonds de barre de progression |
| `bg` | `#0B0F14` | fond de fenêtre |
| `sidebar` | `#0D1219` | barre latérale et barre de titre |
| `surface1` | `#121820` | cartes, panneaux |
| `surface2` | `#17202B` | survol |
| `surface3` | `#1D2833` | actif, enfoncé |
| `border_subtle` | `#1F2933` | séparation par défaut |
| `border_strong` | `#2C3A47` | contour de bouton fantôme |

## Texte

Quatre niveaux. Un cinquième signifierait que la hiérarchie de l'écran est mal posée.

| Jeton | Hex | Usage |
|---|---|---|
| `text` | `#E6EDF3` | contenu principal, valeurs |
| `text_secondary` | `#9BA9B8` | libellés d'appui |
| `text_muted` | `#7D8998` | descriptions, unités |
| `text_disabled` | `#4B5765` | étiquettes de groupe, identifiants techniques |

## Accent et états

| Jeton | Hex |
|---|---|
| `accent` / `accent_hover` / `accent_active` | `#38BDF8` / `#7DD3FC` / `#0EA5E9` |
| `ok` | `#34D399` |
| `warn` | `#FBBF24` |
| `danger` | `#F87171` |

Chaque état possède une variante `_subtle` : la même teinte à **14 % d'opacité**. C'est
le fond des pastilles et des badges — jamais une couleur pleine derrière du texte.

---

## Espacement

Base de 4, six valeurs seulement.

```
xs  4      sm  8      md 12      lg 16      xl 24      2xl 32
```

- Marge intérieure d'une carte : `lg`
- Écart entre deux cartes : `md`
- Marge de la colonne de contenu : `2xl`

## Rayons

```
sm 4   (badges carrés)
md 7   (boutons, champs)
lg 11  (cartes)
pill   (pastilles, curseurs, barres)
```

## Typographie

**Segoe UI** (Semibold pour les titres) et **Cascadia Mono** pour tout ce qui est une
valeur mesurée ou un identifiant technique. Les deux sont chargées depuis
`C:\Windows\Fonts` à l'exécution : rien n'est redistribué, donc aucune question de
licence à la diffusion publique. Repli automatique sur Arial / Consolas, puis sur la
police intégrée d'ImGui.

| Rôle | Taille de base | Graisse |
|---|---|---|
| `display` | 26 | Semibold — titre de page |
| `h1` | 21 | Semibold |
| `title` | 17,5 | Semibold — titre de carte |
| `body` | 16,5 | Regular |
| `small` | 14,5 | Regular — descriptions |
| `micro` | 13 | Semibold — étiquettes de groupe, badges |

### Échelle d'interface

Ces tailles sont multipliées par `dpi_scale × UiScale()`. **`UiScale` part à 1,20** et se
règle dans le pied de la barre latérale, de 85 % à 160 % par pas de 5 %.

Le multiplicateur agit sur **tout** — polices *et* gabarits — pour que les proportions du
système restent intactes à n'importe quelle taille. Un facteur qui n'agirait que sur les
polices casserait les hauteurs de ligne, les cartes et les pastilles.

Le rechargement des polices se fait **entre deux images**, jamais pendant : le clic pose
un drapeau que la boucle principale consomme après `EndFrame`.

**Le monospace n'est pas décoratif** : il signale « ceci est une valeur lue sur la
machine, pas une phrase ». Fréquences, températures, identifiants de réglage, contenu du
journal.

---

## Composants

Définis dans [`src/ui/widgets.hpp`](src/ui/widgets.hpp).

| Composant | Rôle |
|---|---|
| `MetricTile` | une mesure : libellé, valeur, unité, barre de remplissage |
| `Chart` | graphique **lisible** : graduations chiffrées, repère de niveau, valeur courante en haut à droite, fenêtre temporelle en abscisse |
| `Sparkline` | version sans échelle, réservée aux endroits où seule la tendance compte |
| `BeginCard` / `EndCard` | bloc de contenu sur `surface1`, hauteur automatique |
| `StatusPill` | pastille point + texte, fond `_subtle` |
| `Badge` | étiquette compacte sans point |
| `NavItem` | entrée de navigation, barre d'accent à gauche si active |
| `Toggle` | interrupteur animé |
| `Stepper` | incrémenteur segmenté `− │ valeur │ +` dans **un seul cadre** : trois éléments posés côte à côte se lisent comme trois contrôles, pas comme un |
| `PrimaryButton` / `GhostButton` / `DangerButton` | trois niveaux d'action, jamais plus d'un primaire par vue |

## Règle sur les graphiques

> **Un graphe sans échelle est une décoration, pas une mesure.**

Tout `Chart` porte obligatoirement : les graduations chiffrées de l'axe des ordonnées, un
repère horizontal en pointillés au niveau courant, la valeur du dernier échantillon dans
une étiquette calée en haut à droite, et la largeur de la fenêtre temporelle en abscisse
(`passé` → `maintenant (40 s)`).

L'étiquette de valeur est **fixe**, pas accrochée à la courbe : collée à la courbe, elle
devenait illisible contre le bord du cadre dès que la valeur approchait de zéro.

## Garde-fou avant le catalogue

La page Réglages n'expose rien tant qu'un **niveau d'optimisation** n'a pas été choisi :
un avertissement général, trois cartes (Basique / Avancé / Expert) avec le nombre de
réglages réellement disponibles sur *cette* machine, puis le détail du niveau retenu.

Deux règles :

- Au-delà de **Basique**, une confirmation explicite est exigée avant d'accéder à la liste.
- Le niveau choisi **borne** l'affichage : au niveau Basique les réglages avancés ne sont
  pas grisés, ils ne sont simplement pas là. Un contrôle désactivé invite à chercher
  comment l'activer ; un contrôle absent ne pose pas la question.

Le choix est volontairement **remis à zéro à chaque lancement**. C'est un garde-fou, pas
une préférence : quelqu'un qui rouvre l'outil trois mois plus tard doit revoir
l'avertissement.

## Densité des listes

Une liste de dix-sept éléments dépliés d'office est inutilisable. La page Réglages
applique donc le modèle **ligne compacte + panneau à la demande** : 48 px par réglage
(titre, identifiant, état, marqueurs), et le détail complet ne s'ouvre qu'au clic. Un
champ de recherche filtre sur le titre, l'identifiant et la description.

## Mouvement

Une seule fonction : `Animate(id, cible, vitesse)`, une convergence exponentielle
indépendante du nombre d'images par seconde.

```cpp
*v += (cible - *v) * (1 - exp(-vitesse * dt));
```

- Survol de la navigation : vitesse 14
- Interrupteur : vitesse 18

Pas de rebond, pas de fondu d'apparition. Un outil système doit paraître instantané.

---

## Fenêtre

Le chrome Windows est supprimé via `WM_NCCALCSIZE`, mais `WS_OVERLAPPEDWINDOW` est
conservé : l'ancrage (Snap), l'animation de réduction et la gestion multi-écran
continuent de fonctionner. L'ombre portée et les coins arrondis de Windows 11 sont
restitués par DWM (`DwmExtendFrameIntoClientArea` + `DWMWA_WINDOW_CORNER_PREFERENCE`).

Les boutons système et le losange de marque sont **tracés à la main** avec `ImDrawList` :
aucune police d'icônes n'est embarquée, donc rien à redistribuer et aucun glyphe manquant
possible.

## DPI

Tous les jetons de dimension sont multipliés une seule fois par l'échelle DPI, dans
`ApplyStyle()`. Un changement d'écran déclenche `WM_DPICHANGED`, qui recharge les polices
et réapplique le style — sans redémarrage.

---

## Modifier le thème

1. Éditer la structure `Palette` dans `src/ui/theme.hpp`.
2. Recompiler.

Rien d'autre. Aucun composant ne connaît de valeur littérale.
