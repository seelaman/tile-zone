/*
 * tile-zone-picker — visual zone picker for tile-zone.sh
 *
 * Shows a fullscreen overlay with colored zone outlines and letter labels.
 * Type a single letter to tile the active window to that zone.
 *
 * 22-zone quarter-based layout — left-hand quarter grid + right-hand halves/center:
 *   Q W E R   q1-top  q2-top  q3-top  q4-top   (top-half quarters)
 *   A S D F   q1-full q2-full q3-full q4-full  (full-height quarters)
 *   Z X C V   q1-bot  q2-bot  q3-bot  q4-bot   (bottom-half quarters)
 *   G = center-full (Q2+Q3, dark green)
 *   H = left half full,   L = right half full          (vi H/L)
 *   T = left half top,    I = right half top
 *   B = left half bottom, , = right half bottom
 *   K = center top,       J = center bottom,   M = maximize
 *
 * Build:
 *   make -C ~/bin tile-zone-picker
 *   — or —
 *   g++ -O2 -std=c++17 -o tile-zone-picker tile-zone-picker.cpp \
 *       $(pkg-config --cflags --libs Qt6Widgets)
 *
 * Usage:
 *   tile-zone-picker
 *
 * Bind to a keyboard shortcut (e.g. RMeta+W via kanata).
 * Requires: tile-zone.sh, qdbus
 */



#include <cstring>

#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <QScreen>
#include <QKeyEvent>
#include <QProcess>
#include <QTimer>
#include <QWindow>
#include <QDir>
#include <QFont>
#include <QFontMetrics>
#include <QRectF>
#include <QPointF>
#include <QColor>
#include <QPen>
#include <QMouseEvent>
#include <QDBusConnection>
#include <QDBusVirtualObject>
#include <QDBusMessage>
#include <QDBusReply>
#include <QEventLoop>
#include <QTemporaryFile>

#include <cmath>
#include <vector>

// ── Zone definition ─────────────────────────────────────────────────────

struct Zone {
    int id;            // tile-zone.sh zone number
    char key;          // trigger key (lowercase)
    QColor color;      // outline and label color
    QRectF outline;    // outline rectangle (screen-local coordinates)
    QPointF labelOff{0, 0};  // offset from outline center (to avoid overlap)
};

// ── Zone colors — small to large: red → yellow → blue → green ────────────
// Innermost (smallest) to outermost (largest). Center zones use darker shades.
static const QColor CLR_QTR_HALF    (0xFF, 0x44, 0x44);  // red         — 25% × half  (smallest)
static const QColor CLR_QTR_FULL    (0xFF, 0xDD, 0x00);  // yellow      — 25% × full
static const QColor CLR_HALF_HALF   (0x44, 0x88, 0xFF);  // blue        — 50% × half
static const QColor CLR_HALF_HALF_DK(0x22, 0x55, 0xAA);  // dark blue   — 50% × half center (A,H)
static const QColor CLR_HALF_FULL   (0x44, 0xFF, 0x44);  // green       — 50% × full  (largest)
static const QColor CLR_HALF_FULL_DK(0x22, 0x99, 0x22);  // dark green  — 50% × full center (K)

static const QColor CLR_MAX      (0xFF, 0xFF, 0xFF);  // white  — maximize (full screen)

static const QColor BG_DIM  (  0,   0,   0, 140);
static const QColor PILL_BG (  0,   0,   0, 190);

// ── Build zones — quarter-based, 22 zones ──────────────────────────────

static std::vector<Zone> buildZones(QRect area, QPoint screenOrigin) {
    const int gap = 10;
    int ax = area.x(), ay = area.y();
    int W = area.width(), H = area.height();
    int usable = W - 2 * gap;

    int quarterW  = (usable - 3 * gap) / 4;
    int quarter4W = usable - 3 * quarterW - 3 * gap;
    int halfW     = qRound((usable - gap) / 2.0);
    int center2W  = 2 * quarterW + gap;  // center 50% spans Q2+Q3
    int rowH  = qRound((H - 3 * gap) / 2.0);
    int fullH = rowH * 2 + gap;

    int leftX = ax + gap;
    int halfRightX = ax + W - gap - halfW;
    int q1X = leftX;
    int q2X = q1X + quarterW + gap;
    int q3X = q2X + quarterW + gap;
    int q4X = q3X + quarterW + gap;
    int topY = ay + gap;
    int botY = topY + rowH + gap;

    auto loc = [&](int x, int y, int w, int h, int ins) -> QRectF {
        return QRectF(x - screenOrigin.x() + ins,
                      y - screenOrigin.y() + ins,
                      w - 2 * ins, h - 2 * ins);
    };

    // Uniform 12px inset step. Quarter zones don't overlap each other so
    // they sit at 0/12. The 50%-wide zones overlap quarters and each other,
    // stacking inward. Center zones (K, A, H) get their own level.
    const int S = 12;  // uniform step between every pair of adjacent outlines

    // The maximize zone uses the full available area (no gaps)
    int maxW = W, maxH = H;

    std::vector<Zone> z;
    z.reserve(22);

    // Draw order: outermost (largest) first → innermost (smallest) on top.
    // green (50% full) → blue (50% half) → yellow (25% full) → red (25% half)
    //
    //   Q W E R → q1-top q2-top q3-top q4-top        (left hand, top-half quarters)
    //   A S D F → q1-full q2-full q3-full q4-full    (left hand, full-height quarters)
    //   Z X C V → q1-bot q2-bot q3-bot q4-bot        (left hand, bottom-half quarters)
    //   G = center-full(dark, spans Q2+Q3)
    //   H = left-half full,  L = right-half full     (vi H/L)
    //   T = left-top,  I = right-top                 (half-width × half-height)
    //   B = left-bot,  , = right-bot
    //   K = center-top, J = center-bot,  M = maximize

    // Maximize — white, outermost (shifted left to avoid G)
    z.push_back({21, 'm', CLR_MAX,
        QRectF(ax - screenOrigin.x(), ay - screenOrigin.y(), maxW, maxH), {-30, 0}});

    // Inset 0: 50% × full-height left/right — green (outermost, largest)
    z.push_back({12, 'h', CLR_HALF_FULL,    loc(leftX,      topY, halfW,    fullH, 0)});
    z.push_back({13, 'l', CLR_HALF_FULL,    loc(halfRightX, topY, halfW,    fullH, 0)});

    // Inset 12: 50% × full-height center — dark green (shifted right to avoid N)
    z.push_back({14, 'g', CLR_HALF_FULL_DK, loc(q2X, topY, center2W, fullH, S), {30, 0}});

    // Inset 24: 50% × half-height left/right — blue
    z.push_back({15, 't', CLR_HALF_HALF,    loc(leftX,      topY, halfW, rowH, 2*S)});
    z.push_back({16, 'i', CLR_HALF_HALF,    loc(halfRightX, topY, halfW, rowH, 2*S)});
    z.push_back({17, 'b', CLR_HALF_HALF,    loc(leftX,      botY, halfW, rowH, 2*S)});
    z.push_back({18, ',', CLR_HALF_HALF,    loc(halfRightX, botY, halfW, rowH, 2*S)});

    // Inset 36: 50% × half-height center — dark blue
    z.push_back({19, 'k', CLR_HALF_HALF_DK, loc(q2X, topY, center2W, rowH, 3*S)});
    z.push_back({20, 'j', CLR_HALF_HALF_DK, loc(q2X, botY, center2W, rowH, 3*S)});

    // Inset 48: 25% × full-height — yellow
    z.push_back({ 0, 'a', CLR_QTR_FULL, loc(q1X, topY, quarterW,  fullH, 4*S)});
    z.push_back({ 1, 's', CLR_QTR_FULL, loc(q2X, topY, quarterW,  fullH, 4*S)});
    z.push_back({ 2, 'd', CLR_QTR_FULL, loc(q3X, topY, quarterW,  fullH, 4*S)});
    z.push_back({ 3, 'f', CLR_QTR_FULL, loc(q4X, topY, quarter4W, fullH, 4*S)});

    // Inset 60: 25% × half-height — red (innermost, smallest)
    z.push_back({ 4, 'q', CLR_QTR_HALF, loc(q1X, topY, quarterW,  rowH, 5*S)});
    z.push_back({ 5, 'w', CLR_QTR_HALF, loc(q2X, topY, quarterW,  rowH, 5*S)});
    z.push_back({ 6, 'e', CLR_QTR_HALF, loc(q3X, topY, quarterW,  rowH, 5*S)});
    z.push_back({ 7, 'r', CLR_QTR_HALF, loc(q4X, topY, quarter4W, rowH, 5*S)});
    z.push_back({ 8, 'z', CLR_QTR_HALF, loc(q1X, botY, quarterW,  rowH, 5*S)});
    z.push_back({ 9, 'x', CLR_QTR_HALF, loc(q2X, botY, quarterW,  rowH, 5*S)});
    z.push_back({10, 'c', CLR_QTR_HALF, loc(q3X, botY, quarterW,  rowH, 5*S)});
    z.push_back({11, 'v', CLR_QTR_HALF, loc(q4X, botY, quarter4W, rowH, 5*S)});

    return z;
}

// ── Forward declaration ──────────────────────────────────────────────────

class ZoneController;

// ── Per-screen transparent overlay widget ────────────────────────────────

class ScreenOverlay : public QWidget {
    QScreen        *scr_;
    ZoneController *ctrl_;
    int             scrIdx_;   // index into controller's screen list

public:
    ScreenOverlay(QScreen *scr, int idx, ZoneController *ctrl)
        : QWidget(nullptr), scr_(scr), ctrl_(ctrl), scrIdx_(idx)
    {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
    }

    void showOnScreen() {
        show();
        if (auto *h = windowHandle())
            h->setScreen(scr_);
        showFullScreen();
    }

    int screenIdx() const { return scrIdx_; }

protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
};

// ── Capture the currently-active window's UUID + output from KWin ──────
//
// We want to tile whichever window the user had focused when they invoked the
// picker — not whatever `workspace.activeWindow` happens to be later (the
// picker overlays steal focus, especially when switching screens). We also
// need that window's output name to decide which screen the picker should
// start "active" on — relying on KWin's activeOutputName picks up the cursor
// instead on some setups. We do both by briefly registering a DBus service
// and running a one-shot KWin script that calls back with both values.

struct ActiveWindow {
    QString uuid;
    QString output;
};

class ActiveWindowCapture : public QDBusVirtualObject {
public:
    ActiveWindow result;
    QEventLoop *loop = nullptr;

    QString introspect(const QString &) const override { return QString(); }

    bool handleMessage(const QDBusMessage &msg,
                       const QDBusConnection &conn) override {
        if (msg.member() == QLatin1String("SetActiveWindow")
            && msg.arguments().size() >= 2) {
            result.uuid   = msg.arguments().at(0).toString();
            result.output = msg.arguments().at(1).toString();
            conn.send(msg.createReply());
            if (loop) loop->quit();
            return true;
        }
        return false;
    }
};

static ActiveWindow captureActiveWindow() {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return {};

    const QString service = QStringLiteral("org.tilezone.Picker");
    const QString path    = QStringLiteral("/ActiveWindow");

    if (!bus.registerService(service)) return {};

    ActiveWindowCapture svc;
    if (!bus.registerVirtualObject(path, &svc)) {
        bus.unregisterService(service);
        return {};
    }

    QTemporaryFile f(QDir::tempPath() + "/tile-zone-picker-XXXXXX.js");
    f.setAutoRemove(true);
    if (!f.open()) {
        bus.unregisterObject(path);
        bus.unregisterService(service);
        return {};
    }
    f.write(
        "(function() {\n"
        "  var w = workspace.activeWindow;\n"
        "  var id  = w ? String(w.internalId) : '';\n"
        "  var out = (w && w.output) ? String(w.output.name) : '';\n"
        "  callDBus('org.tilezone.Picker', '/ActiveWindow',\n"
        "           'org.tilezone.Picker', 'SetActiveWindow', id, out);\n"
        "})();\n");
    f.flush();

    const QString plugin = QStringLiteral("tile-zone-picker-%1")
                               .arg(QCoreApplication::applicationPid());

    auto load = QDBusMessage::createMethodCall(
        "org.kde.KWin", "/Scripting",
        "org.kde.kwin.Scripting", "loadScript");
    load << f.fileName() << plugin;
    QDBusReply<int> scriptId = bus.call(load);
    if (scriptId.isValid()) {
        auto run = QDBusMessage::createMethodCall(
            "org.kde.KWin",
            QStringLiteral("/Scripting/Script%1").arg(scriptId.value()),
            "org.kde.kwin.Script", "run");
        bus.call(run);

        QEventLoop loop;
        svc.loop = &loop;
        QTimer::singleShot(800, &loop, &QEventLoop::quit);
        loop.exec();
        svc.loop = nullptr;
    }

    auto unload = QDBusMessage::createMethodCall(
        "org.kde.KWin", "/Scripting",
        "org.kde.kwin.Scripting", "unloadScript");
    unload << plugin;
    bus.call(unload);

    bus.unregisterObject(path);
    bus.unregisterService(service);
    return svc.result;
}

// ── Controller (manages all screens) ────────────────────────────────────

class ZoneController {
public:
    struct ScreenData {
        QString            name;     // connector name (e.g. "DP-1")
        QScreen           *qscreen;
        std::vector<Zone>  zones;
    };

private:
    QApplication               *app_;
    std::vector<ScreenOverlay*> overlays_;
    std::vector<ScreenData>     screens_;
    QString activeWinId_;     // UUID captured at startup (may be empty)
    int  activeScr_   = 0;    // screen receiving keyboard input
    int  hoveredScr_  = -1;   // screen the mouse is on
    int  hoveredZone_ = -1;   // zone index within hoveredScr_
    bool ready_       = false;

    void refresh() { for (auto *ov : overlays_) ov->repaint(); }

    void dismiss() {
        for (auto *ov : overlays_) ov->close();
        app_->quit();
    }

    void selectZone(int scrIdx, const Zone &zone) {
        for (auto *ov : overlays_) ov->hide();
        app_->processEvents();

        QString cmd;
        if (!activeWinId_.isEmpty()) {
            // Target the window we captured at picker startup — focus races
            // from switching overlays between screens can't misdirect us.
            cmd = QString("%1/bin/tile-zone.sh --screen %2 --window '%3' %4")
                .arg(QDir::homePath(), screens_[scrIdx].name,
                     activeWinId_, QString::number(zone.id));
        } else {
            // Fallback: let tile-zone.sh use workspace.activeWindow (the
            // 0.1s sleep gives KWin time to restore focus after overlay close).
            cmd = QString("sleep 0.1 && %1/bin/tile-zone.sh --screen %2 %3")
                .arg(QDir::homePath(), screens_[scrIdx].name,
                     QString::number(zone.id));
        }
        QProcess::startDetached(QStringLiteral("/bin/bash"),
                                QStringList{"-c", cmd});
        app_->quit();
    }

public:
    ZoneController(QApplication *app, const QString &activeOutput,
                   const QString &activeWinId)
        : app_(app), activeWinId_(activeWinId)
    {
        // Sort screens left-to-right, then top-to-bottom
        auto list = app->screens();
        std::sort(list.begin(), list.end(), [](QScreen *a, QScreen *b) {
            if (a->geometry().x() != b->geometry().x())
                return a->geometry().x() < b->geometry().x();
            return a->geometry().y() < b->geometry().y();
        });

        // The initially-active screen follows the active window's output —
        // not the mouse cursor. Fall back to primary only if capture failed.
        QScreen *initialScr = nullptr;
        if (!activeOutput.isEmpty()) {
            for (auto *s : list) {
                if (s->name() == activeOutput) {
                    initialScr = s;
                    break;
                }
            }
        }
        if (!initialScr) initialScr = app->primaryScreen();

        for (int i = 0; i < (int)list.size(); i++) {
            QScreen *s = list[i];
            auto avail  = s->availableGeometry();
            auto origin = s->geometry().topLeft();

            ScreenData sd;
            sd.name    = s->name();
            sd.qscreen = s;
            sd.zones   = buildZones(avail, origin);
            screens_.push_back(std::move(sd));

            if (s == initialScr) activeScr_ = i;
        }
    }

    void start() {
        for (int i = 0; i < (int)screens_.size(); i++) {
            auto *ov = new ScreenOverlay(screens_[i].qscreen, i, this);
            ov->showOnScreen();
            overlays_.push_back(ov);
        }
        QTimer::singleShot(50, [this]() {
            if (activeScr_ < (int)overlays_.size()) {
                overlays_[activeScr_]->activateWindow();
                overlays_[activeScr_]->raise();
                overlays_[activeScr_]->setFocus();
            }
            ready_ = true;
        });
    }

    // ── Called by ScreenOverlay delegates ─────────────────────────────

    void paint(QPainter &p, int scrIdx, QRect widgetRect) {
        bool active = (scrIdx == activeScr_);
        auto &sd = screens_[scrIdx];
        int dimAlpha = 60;

        p.fillRect(widgetRect, active ? BG_DIM : QColor(0, 0, 0, 180));

        // ── Highlight hovered zone fill ──
        if (scrIdx == hoveredScr_ && hoveredZone_ >= 0
            && hoveredZone_ < (int)sd.zones.size()) {
            QColor fill = sd.zones[hoveredZone_].color;
            fill.setAlpha(45);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRect(sd.zones[hoveredZone_].outline);
        }

        // ── Zone outlines ──
        for (int i = 0; i < (int)sd.zones.size(); i++) {
            const auto &z = sd.zones[i];
            QColor oc = z.color;
            bool hov = (scrIdx == hoveredScr_ && i == hoveredZone_);
            oc.setAlpha(active ? (hov ? 255 : 200) : dimAlpha);
            p.setPen(QPen(oc, hov ? 4 : 3));
            p.setBrush(Qt::NoBrush);
            p.drawRect(z.outline);
        }

        // ── Zone labels ──
        int fontSz = std::max(14, std::min(36,
            static_cast<int>(widgetRect.height() * 0.022)));
        QFont font(QStringLiteral("monospace"), fontSz, QFont::Bold);
        p.setFont(font);
        QFontMetrics fm(font);

        for (int i = 0; i < (int)sd.zones.size(); i++) {
            const auto &z = sd.zones[i];
            bool hov = (scrIdx == hoveredScr_ && i == hoveredZone_);
            QString label = QString(QChar(z.key)).toUpper();
            int tw = fm.horizontalAdvance(label);
            int th = fm.height();
            QPointF ctr = z.outline.center() + z.labelOff;

            double pw = tw + 24, ph = th + 12;
            QRectF pill(ctr.x() - pw/2.0, ctr.y() - ph/2.0, pw, ph);

            QColor pillBg = hov ? z.color : PILL_BG;
            QColor pillFg = hov ? QColor(0, 0, 0) : z.color;
            if (!active) {
                pillBg.setAlpha(pillBg.alpha() * dimAlpha / 255);
                pillFg.setAlpha(dimAlpha);
            }

            p.setPen(Qt::NoPen);
            p.setBrush(pillBg);
            p.drawRoundedRect(pill, 6, 6);
            p.setPen(pillFg);
            p.drawText(pill, Qt::AlignCenter, label);
        }

        // ── Screen number badge ──
        {
            QString num = QString::number(scrIdx + 1);
            if (active) {
                // Small badge in top-left corner
                QFont bf(QStringLiteral("monospace"), fontSz, QFont::Bold);
                p.setFont(bf);
                QFontMetrics bfm(bf);
                int bw = bfm.horizontalAdvance(num) + 16;
                int bh = bfm.height() + 8;
                QRectF badge(8, 8, bw, bh);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 40));
                p.drawRoundedRect(badge, 4, 4);
                p.setPen(QColor(255, 255, 255, 120));
                p.drawText(badge, Qt::AlignCenter, num);
            } else {
                // Large centered number with "Press N" hint
                QFont bf(QStringLiteral("monospace"), fontSz * 4, QFont::Bold);
                p.setFont(bf);
                p.setPen(QColor(255, 255, 255, 100));
                p.drawText(widgetRect, Qt::AlignCenter, num);

                QFont sf(QStringLiteral("monospace"), fontSz, QFont::Normal);
                p.setFont(sf);
                p.setPen(QColor(255, 255, 255, 80));
                QRect hintRect = widgetRect;
                hintRect.setTop(widgetRect.center().y() + fontSz * 3);
                p.drawText(hintRect, Qt::AlignHCenter | Qt::AlignTop,
                           QString("Press %1").arg(num));
            }
        }
    }

    void onKey(QKeyEvent *ev) {
        if (!ready_) return;
        if (ev->key() == Qt::Key_Escape) { dismiss(); return; }

        // Digit → switch active screen
        if (ev->key() >= Qt::Key_1 && ev->key() <= Qt::Key_9) {
            int idx = ev->key() - Qt::Key_1;
            if (idx < (int)screens_.size() && idx != activeScr_) {
                activeScr_ = idx;
                // Move focus to that screen's overlay
                overlays_[idx]->activateWindow();
                overlays_[idx]->raise();
                overlays_[idx]->setFocus();
                refresh();
            }
            return;
        }

        // Zone letter → tile on active screen
        QByteArray kb = ev->text().toLower().toUtf8();
        if (kb.isEmpty()) return;
        char k = kb[0];

        for (const auto &z : screens_[activeScr_].zones) {
            if (z.key == k) {
                selectZone(activeScr_, z);
                return;
            }
        }
    }

    void onMouseMove(int scrIdx, QPointF pos) {
        if (!ready_) return;

        // Auto-activate screen on mouse hover
        if (scrIdx != activeScr_) {
            activeScr_ = scrIdx;
            overlays_[scrIdx]->activateWindow();
            overlays_[scrIdx]->raise();
            overlays_[scrIdx]->setFocus();
        }

        // Find closest zone label
        auto &zones = screens_[scrIdx].zones;
        int closest = -1;
        double bestDist = 1e18;
        for (int i = 0; i < (int)zones.size(); i++) {
            QPointF ctr = zones[i].outline.center();
            double dx = pos.x() - ctr.x(), dy = pos.y() - ctr.y();
            double dist = dx*dx + dy*dy;
            if (dist < bestDist) { bestDist = dist; closest = i; }
        }

        if (closest != hoveredZone_ || scrIdx != hoveredScr_) {
            hoveredZone_ = closest;
            hoveredScr_  = scrIdx;
            for (auto *ov : overlays_)
                ov->setCursor(Qt::PointingHandCursor);
            refresh();
        }
    }

    void onMousePress(int scrIdx, QMouseEvent *ev) {
        if (!ready_) { dismiss(); return; }
        if (ev->button() == Qt::LeftButton
            && scrIdx == hoveredScr_ && hoveredZone_ >= 0) {
            selectZone(scrIdx, screens_[scrIdx].zones[hoveredZone_]);
        } else {
            dismiss();
        }
    }
};

// ── ScreenOverlay method implementations ────────────────────────────────

void ScreenOverlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    ctrl_->paint(p, scrIdx_, rect());
    p.end();
}

void ScreenOverlay::keyPressEvent(QKeyEvent *ev) { ctrl_->onKey(ev); }
void ScreenOverlay::mouseMoveEvent(QMouseEvent *ev) {
    ctrl_->onMouseMove(scrIdx_, ev->position());
}
void ScreenOverlay::mousePressEvent(QMouseEvent *ev) {
    ctrl_->onMousePress(scrIdx_, ev);
}

// ── Main ────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            puts("tile-zone-picker — visual zone picker for window tiling\n"
                 "Usage: tile-zone-picker\n"
                 "  Shows zone overlay on ALL screens. The active window's\n"
                 "  screen starts active; press a digit to switch screens.\n\n"
                 "    Q W E R = q1..q4 top,    A S D F = q1..q4 full,\n"
                 "    Z X C V = q1..q4 bottom, G = center-full\n"
                 "    H = left half, L = right half (vi H/L)\n"
                 "    T/I = left/right top,  B/, = left/right bottom\n"
                 "    K = center-top, J = center-bot,  M = maximize\n\n"
                 "  1-9 = switch screen, Escape/right-click = cancel.");
            return 0;
        }
    }

    QApplication app(argc, argv);

    // Capture the active window's UUID and output name before any overlays
    // appear. UUID survives focus races when tiling; output name decides
    // which screen the picker starts "active" on (based on the active
    // window, not the mouse cursor).
    ActiveWindow aw = captureActiveWindow();

    ZoneController ctrl(&app, aw.output, aw.uuid);
    ctrl.start();
    return app.exec();
}
