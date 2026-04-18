# tile-zone

Keyboard-driven window tiling zones for KDE Plasma 6. A lightweight alternative to KZones that uses direct KWin script injection — no plugins to install, no state to get out of sync.

**tile-zone** consists of two tools:

- **`tile-zone.sh`** — A shell script that tiles the active window to a named zone by injecting a one-shot KWin script via D-Bus. Supports cycling through zones with `next`/`prev`, jumping to a specific zone by number, and moving windows between screens.

- **`tile-zone-picker`** — A Qt6 visual overlay that shows all available zones on every connected screen. Type a letter or click to tile instantly.

## How it works

### Zone layout — 22 zones, quarter-based

Four 25% columns, each available at full height or half height, plus 50% left/right/center zones and maximize. Keys use vi-style HJKL (H=left half, L=right half, J=down, K=up) with remaining keys finger-aligned to the home row. Left hand covers the left side of the screen, right hand covers the right side:

```
  W  E  R  |  U  I  O     Q1  L50  Q2 | Q3  R50  Q4  (top half)
  A  S        D  F        Q1       Q2 | Q3       Q4  (full-height quarters)
  H = left half full      L = right half full          (vi H/L)
  X  C  V  |  M  ,  .     Q1  L50  Q2 | Q3  R50  Q4  (bottom half)

  G = center 50% full     K = center 50% top     J = center 50% bottom
  N = maximize
```

![Zone picker overlay showing all 22 zones](screenshots/picker.png)

### Multi-screen support

The picker shows overlays on all connected screens simultaneously:

- The **active window's screen** starts highlighted — its zones respond to letter keys directly
- **Other screens** are dimmed with a number badge — press `1`-`9` to switch
- **Mouse hover** on any screen auto-activates it and highlights the nearest zone
- **Click** to tile, or type the zone letter on the keyboard
- When tiling to a different screen, the window moves there automatically

### Visual cues

- **Colors encode zone shape** so you can tell at a glance how big the tile will be: red (small quarter), orange (tall quarter), cyan (wide half), green (large half), white (maximize)
- **Center zones** (G, K, J) use darker shades of their group color to distinguish from left/right zones they overlap
- **Overlapping zones** are drawn with uniform 12px inset steps so every outline is clearly separated
- **Hover highlight** fills the zone and inverts the label so you can preview before committing

### Direct tiling script

`tile-zone.sh` works standalone for keyboard-shortcut-driven tiling:

```bash
tile-zone.sh next              # cycle to next zone
tile-zone.sh prev              # cycle to previous zone
tile-zone.sh 3                 # jump to zone 3
tile-zone.sh screen-next       # move window to same zone on next screen
tile-zone.sh --screen DP-1 5   # tile zone 5 on a specific screen
```

## Installation

### Install dependencies and build

```bash
# Debian/Ubuntu/KDE Neon
sudo apt install qt6-base-dev g++ pkg-config qdbus-qt6

# Fedora
sudo dnf install qt6-qtbase-devel gcc-c++ pkgconfig qdbus-qt6

# Arch
sudo pacman -S qt6-base

# Clone and build
git clone https://github.com/seelaman/tile-zone.git
cd tile-zone
make tile-zone-picker
```

All other dependencies (`qdbus`, `bash`) ship with KDE Plasma 6.

### Setup

1. Copy `tile-zone.sh` and `tile-zone-picker` to a directory in your `$PATH` (e.g. `~/bin/`)
2. Make sure `tile-zone.sh` is executable: `chmod +x tile-zone.sh`
3. Bind to keyboard shortcuts.

The picker is great for discovering zones, but once you know the layout you can bind `tile-zone.sh` directly for instant tiling without the overlay.

Example using [kanata](https://github.com/jtroo/kanata) with cross-hand layers (left modifier → right hand keys, right modifier → left hand keys):

```lisp
;; Right meta layer (hold ; with right hand, type left-hand keys)
(deflayermap (rmeta_layer)
  q (cmd bash -c "$HOME/bin/tile-zone-picker")  ;; visual picker
  w (cmd bash -c "$HOME/bin/tile-zone.sh q1-top")
  e (cmd bash -c "$HOME/bin/tile-zone.sh left-top")
  r (cmd bash -c "$HOME/bin/tile-zone.sh q2-top")
  a (cmd bash -c "$HOME/bin/tile-zone.sh q1-full")
  s (cmd bash -c "$HOME/bin/tile-zone.sh q2-full")
  d (cmd bash -c "$HOME/bin/tile-zone.sh q3-full")
  f (cmd bash -c "$HOME/bin/tile-zone.sh q4-full")
  g (cmd bash -c "$HOME/bin/tile-zone.sh center-full")
  x (cmd bash -c "$HOME/bin/tile-zone.sh q1-bot")
  c (cmd bash -c "$HOME/bin/tile-zone.sh left-bot")
  v (cmd bash -c "$HOME/bin/tile-zone.sh q2-bot")
)

;; Left meta layer (hold A with left hand, type right-hand keys)
(deflayermap (lmeta_layer)
  h (cmd bash -c "$HOME/bin/tile-zone.sh left-full")    ;; vi left
  j (cmd bash -c "$HOME/bin/tile-zone.sh center-bot")   ;; vi down
  k (cmd bash -c "$HOME/bin/tile-zone.sh center-top")   ;; vi up
  l (cmd bash -c "$HOME/bin/tile-zone.sh right-full")   ;; vi right
  u (cmd bash -c "$HOME/bin/tile-zone.sh q3-top")
  i (cmd bash -c "$HOME/bin/tile-zone.sh right-top")
  o (cmd bash -c "$HOME/bin/tile-zone.sh q4-top")
  n (cmd bash -c "$HOME/bin/tile-zone.sh maximize")
  m (cmd bash -c "$HOME/bin/tile-zone.sh q3-bot")
  , (cmd bash -c "$HOME/bin/tile-zone.sh right-bot")
  . (cmd bash -c "$HOME/bin/tile-zone.sh q4-bot")
)
```

Available zone names for `tile-zone.sh`:

| Full-height | Top half | Bottom half |
|---|---|---|
| `q1-full` `q2-full` `q3-full` `q4-full` | `q1-top` `q2-top` `q3-top` `q4-top` | `q1-bot` `q2-bot` `q3-bot` `q4-bot` |
| `left-full` `right-full` `center-full` | `left-top` `right-top` `center-top` | `left-bot` `right-bot` `center-bot` |
| `maximize` | | |

#### KDE Custom Shortcuts (no kanata needed)

If you don't use kanata, run the included setup script to register Meta+key shortcuts directly in KDE:

```bash
./setup-kde-shortcuts.sh          # install all shortcuts (Meta+H/J/K/L/W/E/R/...)
./setup-kde-shortcuts.sh --remove # remove them
```

Edit the `SHORTCUTS` array at the top of the script to customize key bindings. Shortcuts appear in System Settings > Shortcuts > Custom Shortcuts.

## Customizing zones

The zone layout is defined directly in `tile-zone.sh` (the `zonesForArea` function) and `tile-zone-picker.cpp` (the `buildZones` function). Want different proportions, more zones, or a different arrangement? Just ask your friendly LLM to modify them — that's how this project was built in the first place.

## License

GNU General Public License v3.0 — see [LICENSE](LICENSE).
