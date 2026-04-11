#!/usr/bin/env bash
# tile-zone.sh - Direct window zone tiling for KDE Plasma 6
# Replaces KZones with a single atomic KWin script execution.
# Detects current zone from window geometry — no state to get out of sync.
#
# Usage: tile-zone.sh <action>
#
# Actions:
#   next            — cycle to next zone on current screen
#   prev            — cycle to previous zone on current screen
#   <zone_number>   — jump to specific zone on current screen
#   screen-next     — move window to same zone on next screen
#   screen-prev     — move window to same zone on previous screen
#
# Zone layout adapts to physical screen size (via edid-decode):
#
# Large screen (>= 40") — quarter-based:
#   0-3:   quarter 1-4       (25% wide, full height)
#   4-7:   top quarter 1-4   (25% wide, top half)
#   8-11:  bottom quarter 1-4 (25% wide, bottom half)
#   12-13: left/right 50%    (full height)
#   14:    center 50%        (full height, spans Q2+Q3)
#   15-16: top left/right 50%
#   17-18: bottom left/right 50%
#   19-20: top/bottom center 50%
#   21:    maximize
#
# Medium screen (15"–40") — side-column layout:
#   0: bottom-left  (26%, bottom half)
#   1: top-left     (26%, top half)
#   2: left 26%     (full height)
#   3: left 50%     (full height)
#   4: center 48%   (full height)
#   5: right 50%    (full height)
#   6: right 26%    (full height)
#   7: top-right    (26%, top half)
#   8: bottom-right (26%, bottom half)
#   9: maximize
#
# Small screen (< 15") — halves only:
#   0: left 50%  (full height)
#   1: right 50% (full height)
#   2: maximize
#
# 10px gap between all zones and screen edges.
# Screens are ordered spatially (left-to-right, then top-to-bottom).

set -euo pipefail

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    sed -n '2,47s/^# \?//p' "$0"
    exit 0
fi

ACTION="next"
TARGET_SCREEN=""
GAP=10

while [[ $# -gt 0 ]]; do
    case "$1" in
        --screen) TARGET_SCREEN="$2"; shift 2 ;;
        *) ACTION="$1"; shift ;;
    esac
done

# Detect physical screen diagonals via edid-decode (connector → inches)
SCREEN_DIAGS=$(python3 -c "
import subprocess, glob, os, re, math, json
diags = {}
for p in sorted(glob.glob('/sys/class/drm/card*-*/edid')):
    conn = os.path.basename(os.path.dirname(p)).split('-', 1)[1]
    try:
        r = subprocess.run(['edid-decode', '--skip-hex-dump', p],
                           capture_output=True, text=True, timeout=2)
        if r.returncode != 0: continue
        m = re.search(r'Maximum image size:\s*(\d+)\s*cm\s*x\s*(\d+)\s*cm', r.stdout)
        if m:
            w, h = int(m.group(1)) * 10, int(m.group(2)) * 10
            diags[conn] = round(math.sqrt(w*w + h*h) / 25.4, 1)
    except Exception: pass
print(json.dumps(diags))
" 2>/dev/null || echo '{}')

PLUGIN_NAME="tile-zone-$$"
TMPFILE=$(mktemp /tmp/tile-zone-XXXXXX.js)
trap 'rm -f "$TMPFILE"' EXIT

cat > "$TMPFILE" <<JSEOF
(function() {
    var gap = ${GAP};
    var action = "${ACTION}";
    var targetScreenName = "${TARGET_SCREEN}";
    var screenDiags = ${SCREEN_DIAGS};

    var win = workspace.activeWindow;
    if (!win || win.specialWindow) return;

    // screenSize: 'large' (>= 40"), 'medium' (15"-40"), 'small' (< 15")
    function zonesForArea(area, screenSize) {
        var ax = area.x, ay = area.y, W = area.width, H = area.height;
        var usable = W - 2 * gap;
        var rowH = Math.round((H - 3 * gap) / 2);
        var topY = ay + gap;
        var botY = topY + rowH + gap;
        var fullH = rowH * 2 + gap;
        var leftX = ax + gap;
        var halfW = Math.round((usable - gap) / 2);
        var halfRightX = ax + W - gap - halfW;

        if (screenSize === 'large') {
            var quarterW = Math.floor((usable - 3 * gap) / 4);
            var quarter4W = usable - 3 * quarterW - 3 * gap;
            var q1X = leftX;
            var q2X = q1X + quarterW + gap;
            var q3X = q2X + quarterW + gap;
            var q4X = q3X + quarterW + gap;

            return [
                { x: q1X, y: topY, w: quarterW,  h: fullH },  // 0
                { x: q2X, y: topY, w: quarterW,  h: fullH },  // 1
                { x: q3X, y: topY, w: quarterW,  h: fullH },  // 2
                { x: q4X, y: topY, w: quarter4W, h: fullH },  // 3
                { x: q1X, y: topY, w: quarterW,  h: rowH  },  // 4
                { x: q2X, y: topY, w: quarterW,  h: rowH  },  // 5
                { x: q3X, y: topY, w: quarterW,  h: rowH  },  // 6
                { x: q4X, y: topY, w: quarter4W, h: rowH  },  // 7
                { x: q1X, y: botY, w: quarterW,  h: rowH  },  // 8
                { x: q2X, y: botY, w: quarterW,  h: rowH  },  // 9
                { x: q3X, y: botY, w: quarterW,  h: rowH  },  // 10
                { x: q4X, y: botY, w: quarter4W, h: rowH  },  // 11
                { x: leftX,      y: topY, w: halfW, h: fullH },  // 12
                { x: halfRightX, y: topY, w: halfW, h: fullH },  // 13
                { x: q2X, y: topY, w: 2 * quarterW + gap, h: fullH },  // 14
                { x: leftX,      y: topY, w: halfW, h: rowH  },  // 15
                { x: halfRightX, y: topY, w: halfW, h: rowH  },  // 16
                { x: leftX,      y: botY, w: halfW, h: rowH  },  // 17
                { x: halfRightX, y: botY, w: halfW, h: rowH  },  // 18
                { x: q2X, y: topY, w: 2 * quarterW + gap, h: rowH },  // 19
                { x: q2X, y: botY, w: 2 * quarterW + gap, h: rowH },  // 20
                { x: ax, y: ay, w: W, h: H },  // 21: maximize
            ];
        } else if (screenSize === 'medium') {
            var sideW = Math.round(usable * 0.26);
            var centerW = usable - 2 * sideW - 2 * gap;
            var rightX = ax + W - gap - sideW;
            var centerX = leftX + sideW + gap;

            return [
                { x: leftX,      y: botY, w: sideW,   h: rowH  },  // 0: bottom-left
                { x: leftX,      y: topY, w: sideW,   h: rowH  },  // 1: top-left
                { x: leftX,      y: topY, w: sideW,   h: fullH },  // 2: left 26%
                { x: leftX,      y: topY, w: halfW,   h: fullH },  // 3: left 50%
                { x: centerX,    y: topY, w: centerW, h: fullH },  // 4: center 48%
                { x: halfRightX, y: topY, w: halfW,   h: fullH },  // 5: right 50%
                { x: rightX,     y: topY, w: sideW,   h: fullH },  // 6: right 26%
                { x: rightX,     y: topY, w: sideW,   h: rowH  },  // 7: top-right
                { x: rightX,     y: botY, w: sideW,   h: rowH  },  // 8: bottom-right
                { x: ax, y: ay, w: W, h: H },  // 9: maximize
            ];
        } else {
            return [
                { x: leftX,      y: topY, w: halfW, h: fullH },  // 0: left 50%
                { x: halfRightX, y: topY, w: halfW, h: fullH },  // 1: right 50%
                { x: ax, y: ay, w: W, h: H },  // 2: maximize
            ];
        }
    }

    // Detect which zone best matches the window geometry
    function detectZone(zones, fg) {
        var current = 0;
        var bestDist = Infinity;
        for (var i = 0; i < zones.length; i++) {
            var z = zones[i];
            var dist = Math.abs(fg.x - z.x) + Math.abs(fg.y - z.y)
                     + Math.abs(fg.width - z.w) + Math.abs(fg.height - z.h);
            if (dist < bestDist) {
                bestDist = dist;
                current = i;
            }
        }
        return current;
    }

    // Sort screens spatially: left-to-right, then top-to-bottom
    var screens = workspace.screens.slice().sort(function(a, b) {
        if (a.geometry.x !== b.geometry.x) return a.geometry.x - b.geometry.x;
        return a.geometry.y - b.geometry.y;
    });

    // Find current screen index
    var curScreenIdx = 0;
    for (var i = 0; i < screens.length; i++) {
        if (screens[i].name === win.output.name) {
            curScreenIdx = i;
            break;
        }
    }

    // Determine which screen to tile on: --screen override or the window's current screen
    var tileOutput = win.output;
    if (targetScreenName) {
        for (var i = 0; i < screens.length; i++) {
            if (screens[i].name === targetScreenName) {
                tileOutput = screens[i];
                break;
            }
        }
    }

    function sizeClass(d) { return d >= 40 ? 'large' : d >= 15 ? 'medium' : 'small'; }

    var curSize = sizeClass(screenDiags[tileOutput.name] || 0);
    var curArea = workspace.clientArea(KWin.MaximizeArea, tileOutput, win.desktops[0]);
    var curZones = zonesForArea(curArea, curSize);
    var curZone = detectZone(curZones, win.frameGeometry);

    var targetZone = curZone;
    var targetZones = curZones;

    if (action === "screen-next" || action === "screen-prev") {
        var dir = (action === "screen-next") ? 1 : -1;
        var newIdx = (curScreenIdx + dir + screens.length) % screens.length;
        var targetScreen = screens[newIdx];
        var targetSize = sizeClass(screenDiags[targetScreen.name] || 0);
        var targetArea = workspace.clientArea(KWin.MaximizeArea, targetScreen, win.desktops[0]);
        targetZones = zonesForArea(targetArea, targetSize);
        // Keep same zone index on the new screen (clamped)
        if (targetZone >= targetZones.length)
            targetZone = 0;
    } else if (action === "next") {
        targetZone = (curZone + 1) % curZones.length;
    } else if (action === "prev") {
        targetZone = (curZone - 1 + curZones.length) % curZones.length;
    } else {
        // Named zone aliases that resolve per screen size
        var namedZones = {
            large:  { 'maximize': 21, 'center-full': 14, 'left-full': 0, 'right-full': 3 },
            medium: { 'maximize': 9,  'center-full': 4,  'left-full': 2, 'right-full': 6 },
            small:  { 'maximize': 2,  'center-full': -1, 'left-full': 0, 'right-full': 1 },
        };
        var names = namedZones[curSize] || {};
        if (action in names) {
            targetZone = names[action];
            if (targetZone < 0) return;
        } else {
            targetZone = parseInt(action);
            if (isNaN(targetZone) || targetZone < 0 || targetZone >= curZones.length) return;
        }
    }

    var tg = targetZones[targetZone];
    // Clear any KDE tile so geometry can be set freely
    if (win.tile) win.tile = null;
    win.frameGeometry = {x: tg.x, y: tg.y, width: tg.w, height: tg.h};
})();
JSEOF

# Load and run (unique plugin name per invocation; cleanup in EXIT trap)
SCRIPT_ID=$(qdbus org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$TMPFILE" "$PLUGIN_NAME")
qdbus org.kde.KWin "/Scripting/Script${SCRIPT_ID}" org.kde.kwin.Script.run
