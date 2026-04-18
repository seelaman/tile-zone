# tile-zone

Keyboard-driven window tiling zones for KDE Plasma 6. A lightweight alternative to KZones that uses direct KWin script injection — no plugins to install, no state to get out of sync.

**tile-zone** consists of two tools:

- **`tile-zone.sh`** — A shell script that tiles the active window to a named zone by injecting a one-shot KWin script via D-Bus. Supports cycling through zones with `next`/`prev`, jumping to a specific zone by number, and moving windows between screens.

- **`tile-zone-picker`** — A Qt6 visual overlay that shows all available zones on every connected screen. Type a letter or click to tile instantly.

## How it works

### Zone layout — 22 zones, quarter-based

Four 25% columns, each available at full height, top half, or bottom half, plus 50% left/right/center zones and maximize. The left hand maps directly to the on-screen quarter grid; the right hand handles halves and center zones using vi-style HJKL:

```
  Q W E R   q1-top   q2-top   q3-top   q4-top    ← top-half quarters
  A S D F   q1-full  q2-full  q3-full  q4-full   ← full-height quarters
  Z X C V   q1-bot   q2-bot   q3-bot   q4-bot    ← bottom-half quarters

  G = center-full (Q2+Q3, dark green)
  H = left half full   L = right half full        (vi H/L)
  T = left half top    I = right half top
  B = left half bot    , = right half bot
  K = center top       J = center bot    M = maximize
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

Example using [kanata](https://github.com/jtroo/kanata) with cross-hand layers — the right-meta layer gives the left hand a full quarter grid, the left-meta layer gives the right hand halves/center/picker:

```lisp
;; Right meta layer (hold ; with right hand, type left-hand keys):
;;   Q W E R = q1..q4 top, A S D F = q1..q4 full, Z X C V = q1..q4 bottom
(deflayermap (rmeta_layer)
  q (cmd bash -c "$HOME/bin/tile-zone.sh q1-top")
  w (cmd bash -c "$HOME/bin/tile-zone.sh q2-top")
  e (cmd bash -c "$HOME/bin/tile-zone.sh q3-top")
  r (cmd bash -c "$HOME/bin/tile-zone.sh q4-top")
  a (cmd bash -c "$HOME/bin/tile-zone.sh q1-full")
  s (cmd bash -c "$HOME/bin/tile-zone.sh q2-full")
  d (cmd bash -c "$HOME/bin/tile-zone.sh q3-full")
  f (cmd bash -c "$HOME/bin/tile-zone.sh q4-full")
  g (cmd bash -c "$HOME/bin/tile-zone.sh center-full")
  z (cmd bash -c "$HOME/bin/tile-zone.sh q1-bot")
  x (cmd bash -c "$HOME/bin/tile-zone.sh q2-bot")
  c (cmd bash -c "$HOME/bin/tile-zone.sh q3-bot")
  v (cmd bash -c "$HOME/bin/tile-zone.sh q4-bot")
)

;; Left meta layer (hold A with left hand, type right-hand keys):
;;   vi HJKL navigation + picker + maximize
(deflayermap (lmeta_layer)
  h (cmd bash -c "$HOME/bin/tile-zone.sh left-full")    ;; vi left
  j (cmd bash -c "$HOME/bin/tile-zone.sh center-bot")   ;; vi down
  k (cmd bash -c "$HOME/bin/tile-zone.sh center-top")   ;; vi up
  l (cmd bash -c "$HOME/bin/tile-zone.sh right-full")   ;; vi right
  o (cmd bash -c "$HOME/bin/tile-zone-picker")          ;; visual picker
  m (cmd bash -c "$HOME/bin/tile-zone.sh maximize")
)
```

The four half-width × half-height zones (`left-top`, `right-top`, `left-bot`, `right-bot`) aren't bound to dedicated shortcuts — they're reachable via the picker (keys `T`, `I`, `B`, `,`).

Available zone names for `tile-zone.sh`:

| Full-height | Top half | Bottom half |
|---|---|---|
| `q1-full` `q2-full` `q3-full` `q4-full` | `q1-top` `q2-top` `q3-top` `q4-top` | `q1-bot` `q2-bot` `q3-bot` `q4-bot` |
| `left-full` `right-full` `center-full` | `left-top` `right-top` `center-top` | `left-bot` `right-bot` `center-bot` |
| `maximize` | | |

#### KDE Custom Shortcuts (no kanata needed)

If you don't use kanata, run the included setup script to register Meta+key shortcuts directly in KDE:

```bash
./setup-kde-shortcuts.sh          # install all shortcuts (Meta+A/S/D/F, Meta+Q/W/E/R, Meta+Z/X/C/V, Meta+H/J/K/L/...)
./setup-kde-shortcuts.sh --remove # remove them
```

Edit the `SHORTCUTS` array at the top of the script to customize key bindings. Shortcuts appear in System Settings > Shortcuts > Custom Shortcuts.

## Customizing zones

The zone layout is defined directly in `tile-zone.sh` (the `zonesForArea` function) and `tile-zone-picker.cpp` (the `buildZones` function). Want different proportions, more zones, or a different arrangement? Just ask your friendly LLM to modify them — that's how this project was built in the first place.

## License

GNU General Public License v3.0 — see [LICENSE](LICENSE).
