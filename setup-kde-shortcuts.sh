#!/usr/bin/env bash
# setup-kde-shortcuts.sh — Register tile-zone keyboard shortcuts in KDE Plasma 6
#
# This creates global shortcuts in KDE's Custom Shortcuts (khotkeys).
# After running, shortcuts appear in System Settings > Shortcuts > Custom Shortcuts.
#
# Default bindings use Meta+<key>. Edit the SHORTCUTS array below to customize.
#
# Usage:
#   ./setup-kde-shortcuts.sh          # install shortcuts
#   ./setup-kde-shortcuts.sh --remove # remove tile-zone shortcuts

set -euo pipefail

TILE_ZONE_SH="${TILE_ZONE_SH:-$HOME/bin/tile-zone.sh}"
TILE_ZONE_PICKER="${TILE_ZONE_PICKER:-$HOME/bin/tile-zone-picker}"
KHOTKEYS_RC="$HOME/.config/khotkeysrc"

# Shortcut definitions: "name|command|key-combo"
# Adjust key combos to your preference. Uses Qt key syntax.
SHORTCUTS=(
    # Picker
    "tile-zone: Picker|$TILE_ZONE_PICKER|Meta+Z"

    # Vi navigation — edges and center
    "tile-zone: Left full (H)|$TILE_ZONE_SH q1-full|Meta+H"
    "tile-zone: Right full (L)|$TILE_ZONE_SH q4-full|Meta+L"
    "tile-zone: Center top (K)|$TILE_ZONE_SH center-top|Meta+K"
    "tile-zone: Center bottom (J)|$TILE_ZONE_SH center-bot|Meta+J"
    "tile-zone: Center full (A)|$TILE_ZONE_SH center-full|Meta+A"
    "tile-zone: Maximize (N)|$TILE_ZONE_SH maximize|Meta+N"

    # Top half — left hand
    "tile-zone: Q1 top (W)|$TILE_ZONE_SH q1-top|Meta+W"
    "tile-zone: Left-50 top (E)|$TILE_ZONE_SH left-top|Meta+E"
    "tile-zone: Q2 top (R)|$TILE_ZONE_SH q2-top|Meta+R"

    # Top half — right hand
    "tile-zone: Q3 top (U)|$TILE_ZONE_SH q3-top|Meta+U"
    "tile-zone: Right-50 top (I)|$TILE_ZONE_SH right-top|Meta+I"
    "tile-zone: Q4 top (O)|$TILE_ZONE_SH q4-top|Meta+O"

    # Full height
    "tile-zone: Left-50 full (S)|$TILE_ZONE_SH left-full|Meta+S"
    "tile-zone: Q2 full (D)|$TILE_ZONE_SH q2-full|Meta+D"
    "tile-zone: Q3 full (F)|$TILE_ZONE_SH q3-full|Meta+F"
    "tile-zone: Right-50 full (G)|$TILE_ZONE_SH right-full|Meta+G"

    # Bottom half — left hand
    "tile-zone: Q1 bottom (X)|$TILE_ZONE_SH q1-bot|Meta+X"
    "tile-zone: Left-50 bottom (C)|$TILE_ZONE_SH left-bot|Meta+C"
    "tile-zone: Q2 bottom (V)|$TILE_ZONE_SH q2-bot|Meta+V"

    # Bottom half — right hand
    "tile-zone: Q3 bottom (M)|$TILE_ZONE_SH q3-bot|Meta+M"
    "tile-zone: Right-50 bottom (,)|$TILE_ZONE_SH right-bot|Meta+,"
    "tile-zone: Q4 bottom (.)|$TILE_ZONE_SH q4-bot|Meta+."
)

GROUP_NAME="tile-zone"

remove_shortcuts() {
    if [ ! -f "$KHOTKEYS_RC" ]; then
        echo "No khotkeysrc found, nothing to remove."
        return
    fi
    # Find and remove the tile-zone group
    python3 -c "
import configparser, sys, os

path = '$KHOTKEYS_RC'
# khotkeysrc uses a non-standard format; parse manually
lines = open(path).readlines()
out = []
skip_section = False
skip_keys = set()

# First pass: find tile-zone data sections
for i, line in enumerate(lines):
    stripped = line.strip()
    if stripped.startswith('[Data_') and stripped.endswith(']'):
        # Check if next few lines contain our group name
        block = ''.join(lines[i:i+5])
        if 'Name=$GROUP_NAME' in block or 'Name=tile-zone:' in block:
            # Extract the data index
            import re
            m = re.match(r'\[Data_(\d+)', stripped)
            if m:
                skip_keys.add(m.group(1))

if not skip_keys:
    print('No tile-zone shortcuts found.')
    sys.exit(0)

# Second pass: remove matching sections
for line in lines:
    stripped = line.strip()
    if stripped.startswith('['):
        import re
        m = re.match(r'\[Data_(\d+)', stripped)
        skip_section = m is not None and m.group(1) in skip_keys
    if not skip_section:
        out.append(line)

open(path, 'w').writelines(out)
print(f'Removed {len(skip_keys)} tile-zone shortcut(s).')
" 2>/dev/null || echo "Could not parse khotkeysrc. Remove tile-zone entries manually in System Settings > Shortcuts."
}

install_shortcuts() {
    if [ ! -f "$KHOTKEYS_RC" ]; then
        echo "Creating $KHOTKEYS_RC"
        cat > "$KHOTKEYS_RC" << 'EOF'
[$Version]
update_info=

[Data]
DataCount=0
EOF
    fi

    # Find the next available data index
    NEXT_IDX=$(python3 -c "
import re
lines = open('$KHOTKEYS_RC').readlines()
indices = set()
for line in lines:
    m = re.match(r'\[Data_(\d+)\]', line.strip())
    if m:
        indices.add(int(m.group(1)))
highest = max(indices) if indices else 0
# Also read DataCount
for line in lines:
    if line.strip().startswith('DataCount='):
        highest = max(highest, int(line.strip().split('=')[1]))
print(highest + 1)
")

    echo "Adding ${#SHORTCUTS[@]} shortcuts starting at index $NEXT_IDX..."

    IDX=$NEXT_IDX
    for entry in "${SHORTCUTS[@]}"; do
        IFS='|' read -r name cmd key <<< "$entry"
        UUID=$(python3 -c "import uuid; print('{' + str(uuid.uuid4()) + '}')")

        cat >> "$KHOTKEYS_RC" << EOF

[Data_${IDX}]
Comment=
Enabled=true
Name=${name}
Type=SIMPLE_ACTION_DATA

[Data_${IDX}Actions]
ActionsCount=1

[Data_${IDX}Actions0]
CommandURL=bash -c "${cmd}"
Type=COMMAND_URL

[Data_${IDX}Conditions]
Comment=
ConditionsCount=0

[Data_${IDX}Triggers]
Comment=Simple_action
TriggersCount=1

[Data_${IDX}Triggers0]
Key=${key}
Type=SHORTCUT
Uuid=${UUID}
EOF
        IDX=$((IDX + 1))
    done

    # Update DataCount
    TOTAL=$((IDX - 1))
    python3 -c "
lines = open('$KHOTKEYS_RC').readlines()
out = []
for line in lines:
    if line.strip().startswith('DataCount='):
        out.append('DataCount=$TOTAL\n')
    else:
        out.append(line)
open('$KHOTKEYS_RC', 'w').writelines(out)
"

    echo "Done. Added ${#SHORTCUTS[@]} shortcuts (indices $NEXT_IDX–$((IDX-1)))."
    echo ""
    echo "Reload with:  qdbus org.kde.kglobalaccel /khotkeys reread_configuration"
    echo "Or log out and back in."

    # Try to reload
    qdbus org.kde.kglobalaccel /khotkeys reread_configuration 2>/dev/null && echo "Shortcuts reloaded." || true
}

case "${1:-}" in
    --remove)
        remove_shortcuts
        ;;
    *)
        install_shortcuts
        ;;
esac
