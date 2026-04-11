/*
 * tile-zone-picker — visual zone picker for tile-zone.sh
 *
 * Shows a fullscreen overlay with colored zone outlines and letter labels.
 * Type a single letter to tile the active window to that zone.
 *
 * Adapts to screen resolution:
 *
 * Large screen (4K+, >= 3840px):
 *   Q  W  E  R  T  Y    ← Q/Y=top 50% left/right, W-T=top quarters
 *   A  S  D  F  G  H    ← A/H=top/bot center 50%, S-G=full quarters
 *   Z  X  C  V  B  N    ← Z/N=bottom 50% left/right, X-B=bottom quarters
 *      J  K  L           ← J=left 50%, K=center 50%, L=right 50%
 *
 * Standard screen (< 3840px):
 *   Q        Y    ← top-half 26% columns
 *   A        H    ← full-height 26% columns
 *   Z        N    ← bottom-half 26% columns
 *      J K L      ← wide zones (left 50%, center 48%, right 50%)
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
 * Requires: tile-zone.sh, kdotool (~/.cargo/bin/kdotool)
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
#include <QRegularExpression>

#include <cmath>
#include <vector>

// ── Zone definition ─────────────────────────────────────────────────────

struct Zone {
    int id;            // tile-zone.sh zone number
    char key;          // trigger key (lowercase)
    QColor color;      // outline and label color
    QRectF outline;    // outline rectangle (screen-local coordinates)
};

// ── Zone colors — one color per shape group ──────────────────────────────
// 5 shape groups (width × height), 5 maximally distinct colors:
static const QColor CLR_QTR_HALF (0xFF, 0x44, 0x44);  // red    — 25% × half  (W,E,R,T,X,C,V,B)
static const QColor CLR_QTR_FULL (0xFF, 0xBB, 0x00);  // orange — 25% × full  (S,D,F,G)
static const QColor CLR_HALF_HALF(0x00, 0xCC, 0xFF);  // cyan   — 50% × half  (Q,Y,Z,N,A,H)
static const QColor CLR_HALF_FULL(0x44, 0xFF, 0x44);  // green  — 50% × full  (J,L,K)

static const QColor BG_DIM  (  0,   0,   0, 140);
static const QColor PILL_BG (  0,   0,   0, 190);

// ── Physical screen size detection via edid-decode ──────────────────────

static const double LARGE_SCREEN_INCHES = 40.0;

static double screenDiagonalInches(QScreen *screen) {
    QString name = screen->name();  // e.g. "DP-1"

    // Search sysfs for matching EDID
    QDir drmDir("/sys/class/drm");
    for (const auto &entry : drmDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        // entry = "card1-DP-1" → connector = "DP-1"
        int dash = entry.indexOf('-');
        if (dash < 0) continue;
        QString connector = entry.mid(dash + 1);
        if (connector != name) continue;

        QString edidPath = drmDir.filePath(entry) + "/edid";
        QProcess proc;
        proc.start(QStringLiteral("edid-decode"),
                    {"--skip-hex-dump", edidPath});
        if (!proc.waitForFinished(2000)) continue;
        if (proc.exitCode() != 0) continue;

        QString out = QString::fromUtf8(proc.readAllStandardOutput());
        QRegularExpression re(
            "Maximum image size:\\s*(\\d+)\\s*cm\\s*x\\s*(\\d+)\\s*cm");
        auto match = re.match(out);
        if (match.hasMatch()) {
            double w = match.captured(1).toDouble() * 10.0;  // mm
            double h = match.captured(2).toDouble() * 10.0;
            return std::sqrt(w * w + h * h) / 25.4;
        }
    }
    return 0.0;  // unknown
}

// ── Build zones for large screen (4K+) — quarter-based ─────────────────

static std::vector<Zone> buildLargeZones(QRect area, QPoint screenOrigin) {
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

    // Inset levels (14px base step).
    // Full/half quarter columns at 0/14.
    // Wide zones that overlap each other get DIFFERENT insets so no edges
    // coincide: J/L share inset 28, K gets 34 (J/L don't overlap each other
    // but both overlap K). Half-height wide at 44/48 similarly staggered.
    const int IQF = 0, IQH = 14;
    const int IWF_JL = 28, IWF_K = 34;
    const int IWH_C = 42, IWH_QZ = 48, IWH_YN = 54;

    std::vector<Zone> z;
    z.reserve(21);

    // Layer 0: 25% × full-height — orange (S,D,F,G)
    z.push_back({ 0, 's', CLR_QTR_FULL, loc(q1X, topY, quarterW,  fullH, IQF)});
    z.push_back({ 1, 'd', CLR_QTR_FULL, loc(q2X, topY, quarterW,  fullH, IQF)});
    z.push_back({ 2, 'f', CLR_QTR_FULL, loc(q3X, topY, quarterW,  fullH, IQF)});
    z.push_back({ 3, 'g', CLR_QTR_FULL, loc(q4X, topY, quarter4W, fullH, IQF)});

    // Layer 1: 25% × half-height — red (W,E,R,T,X,C,V,B)
    z.push_back({ 4, 'w', CLR_QTR_HALF, loc(q1X, topY, quarterW,  rowH, IQH)});
    z.push_back({ 5, 'e', CLR_QTR_HALF, loc(q2X, topY, quarterW,  rowH, IQH)});
    z.push_back({ 6, 'r', CLR_QTR_HALF, loc(q3X, topY, quarterW,  rowH, IQH)});
    z.push_back({ 7, 't', CLR_QTR_HALF, loc(q4X, topY, quarter4W, rowH, IQH)});
    z.push_back({ 8, 'x', CLR_QTR_HALF, loc(q1X, botY, quarterW,  rowH, IQH)});
    z.push_back({ 9, 'c', CLR_QTR_HALF, loc(q2X, botY, quarterW,  rowH, IQH)});
    z.push_back({10, 'v', CLR_QTR_HALF, loc(q3X, botY, quarterW,  rowH, IQH)});
    z.push_back({11, 'b', CLR_QTR_HALF, loc(q4X, botY, quarter4W, rowH, IQH)});

    // Layer 2: 50% × full-height — green (J,L,K)
    z.push_back({12, 'j', CLR_HALF_FULL, loc(leftX,      topY, halfW,    fullH, IWF_JL)});
    z.push_back({13, 'l', CLR_HALF_FULL, loc(halfRightX,  topY, halfW,    fullH, IWF_JL)});
    z.push_back({14, 'k', CLR_HALF_FULL, loc(q2X,         topY, center2W, fullH, IWF_K)});

    // Layer 3: 50% × half-height — cyan (A,H,Q,Y,Z,N)
    z.push_back({19, 'a', CLR_HALF_HALF, loc(q2X, topY, center2W, rowH, IWH_C)});
    z.push_back({20, 'h', CLR_HALF_HALF, loc(q2X, botY, center2W, rowH, IWH_C)});
    z.push_back({15, 'q', CLR_HALF_HALF, loc(leftX,      topY, halfW, rowH, IWH_QZ)});
    z.push_back({16, 'y', CLR_HALF_HALF, loc(halfRightX, topY, halfW, rowH, IWH_YN)});
    z.push_back({17, 'z', CLR_HALF_HALF, loc(leftX,      botY, halfW, rowH, IWH_QZ)});
    z.push_back({18, 'n', CLR_HALF_HALF, loc(halfRightX, botY, halfW, rowH, IWH_YN)});

    return z;
}

// ── Build zones for standard screen — side-column layout ────────────────

static std::vector<Zone> buildSmallZones(QRect area, QPoint screenOrigin) {
    const int gap = 10;
    int ax = area.x(), ay = area.y();
    int W = area.width(), H = area.height();
    int usable = W - 2 * gap;

    int sideW   = qRound(usable * 0.26);
    int halfW   = qRound((usable - gap) / 2.0);
    int centerW = usable - 2 * sideW - 2 * gap;
    int rowH  = qRound((H - 3 * gap) / 2.0);
    int fullH = rowH * 2 + gap;

    int leftX      = ax + gap;
    int rightX     = ax + W - gap - sideW;
    int halfRightX = ax + W - gap - halfW;
    int centerX    = leftX + sideW + gap;
    int topY = ay + gap;
    int botY = topY + rowH + gap;

    auto loc = [&](int x, int y, int w, int h, int ins) -> QRectF {
        return QRectF(x - screenOrigin.x() + ins,
                      y - screenOrigin.y() + ins,
                      w - 2 * ins, h - 2 * ins);
    };

    const int IF = 0, IH = 14, IW = 30;

    std::vector<Zone> z;
    z.reserve(9);

    // Layer 0: 26% × full-height — orange
    z.push_back({2, 'a', CLR_QTR_FULL, loc(leftX,  topY, sideW, fullH, IF)});
    z.push_back({6, 'h', CLR_QTR_FULL, loc(rightX, topY, sideW, fullH, IF)});

    // Layer 1: 26% × half-height — red
    z.push_back({1, 'q', CLR_QTR_HALF, loc(leftX,  topY, sideW, rowH, IH)});
    z.push_back({0, 'z', CLR_QTR_HALF, loc(leftX,  botY, sideW, rowH, IH)});
    z.push_back({7, 'y', CLR_QTR_HALF, loc(rightX, topY, sideW, rowH, IH)});
    z.push_back({8, 'n', CLR_QTR_HALF, loc(rightX, botY, sideW, rowH, IH)});

    // Layer 2: Wide zones — green (50%/48% × full)
    z.push_back({3, 'j', CLR_HALF_FULL, loc(leftX,      topY, halfW,   fullH, IW)});
    z.push_back({4, 'k', CLR_HALF_FULL, loc(centerX,    topY, centerW, fullH, IW)});
    z.push_back({5, 'l', CLR_HALF_FULL, loc(halfRightX, topY, halfW,   fullH, IW)});

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
    QString                     activeWindowId_;
    std::vector<ScreenOverlay*> overlays_;
    std::vector<ScreenData>     screens_;
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

        QString home = QDir::homePath();
        QString tileCmd = QString("%1/bin/tile-zone.sh --screen %2 %3")
            .arg(home, screens_[scrIdx].name, QString::number(zone.id));
        QString cmd;
        if (!activeWindowId_.isEmpty()) {
            cmd = QString("%1/.cargo/bin/kdotool windowactivate %2 "
                          "&& sleep 0.05 && %3")
                .arg(home, activeWindowId_, tileCmd);
        } else {
            cmd = QString("sleep 0.15 && %1").arg(tileCmd);
        }
        QProcess::startDetached(QStringLiteral("/bin/bash"),
                                QStringList{"-c", cmd});
        app_->quit();
    }

public:
    ZoneController(QApplication *app, const QString &winId)
        : app_(app), activeWindowId_(winId)
    {
        // Sort screens left-to-right, then top-to-bottom
        auto list = app->screens();
        std::sort(list.begin(), list.end(), [](QScreen *a, QScreen *b) {
            if (a->geometry().x() != b->geometry().x())
                return a->geometry().x() < b->geometry().x();
            return a->geometry().y() < b->geometry().y();
        });

        // Detect which screen the active window is on via kdotool,
        // falling back to cursor position
        QScreen *initialScr = nullptr;
        if (!winId.isEmpty()) {
            QProcess proc;
            proc.start(QDir::homePath() + "/.cargo/bin/kdotool",
                        {"getwindowgeometry", winId});
            if (proc.waitForFinished(500) && proc.exitCode() == 0) {
                QString out = proc.readAllStandardOutput();
                QRegularExpression re("Position:\\s*(\\d+),(\\d+)");
                auto m = re.match(out);
                if (m.hasMatch()) {
                    QPoint pos(m.captured(1).toInt(), m.captured(2).toInt());
                    initialScr = QApplication::screenAt(pos);
                }
            }
        }
        if (!initialScr) {
            initialScr = QApplication::screenAt(QCursor::pos());
            if (!initialScr) initialScr = app->primaryScreen();
        }

        for (int i = 0; i < (int)list.size(); i++) {
            QScreen *s = list[i];
            double diag = screenDiagonalInches(s);
            bool large = diag >= LARGE_SCREEN_INCHES;
            auto avail  = s->availableGeometry();
            auto origin = s->geometry().topLeft();

            ScreenData sd;
            sd.name    = s->name();
            sd.qscreen = s;
            sd.zones   = large ? buildLargeZones(avail, origin)
                               : buildSmallZones(avail, origin);
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
            QPointF ctr = z.outline.center();

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
                 "  Shows zone overlay on ALL screens. The cursor's screen\n"
                 "  is active; press a digit to switch screens.\n\n"
                 "  Large screen (>= 40\", via edid-decode):\n"
                 "    Q/Y (top L/R 50%), A/H (top/bot center 50%)\n"
                 "    W/E/R/T (top qtr), S/D/F/G (full qtr), X/C/V/B (bot qtr)\n"
                 "    Z/N (bot L/R 50%), J/K/L (left/center/right 50%)\n\n"
                 "  Standard (< 40\"):\n"
                 "    Q/Y (top 26%), A/H (full 26%), Z/N (bot 26%)\n"
                 "    J (left 50%), K (center 48%), L (right 50%)\n\n"
                 "  1-9 = switch screen, Escape/right-click = cancel.");
            return 0;
        }
    }

    QApplication app(argc, argv);

    // Capture active window ID before showing any overlay
    QString activeWindowId;
    {
        QProcess proc;
        proc.start(QDir::homePath() + "/.cargo/bin/kdotool",
                    QStringList{"getactivewindow"});
        if (proc.waitForFinished(500))
            activeWindowId = QString::fromUtf8(
                proc.readAllStandardOutput()).trimmed();
    }

    ZoneController ctrl(&app, activeWindowId);
    ctrl.start();
    return app.exec();
}
