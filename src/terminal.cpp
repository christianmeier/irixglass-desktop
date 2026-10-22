#include "terminal.h"

#include <QClipboard>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QGuiApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QMimeData>
#include <QRegularExpression>
#include <QPainter>
#include <QTimer>
#include <QVariantAnimation>

#include <cmath>
#include <csignal>
#include <pwd.h>
#include <unistd.h>

#include <KPtyDevice>
#include <KPtyProcess>

// QT_LOGGING_RULES="irixfm.terminal.debug=true" logs drags and pinches (to the journal).
Q_LOGGING_CATEGORY(lcTerminal, "irixfm.terminal", QtInfoMsg)

namespace
{
// The user's Alacritty colours (configs/alacritty/alacritty.toml): Nord.
const QColor kFg(0xD8, 0xDE, 0xE9), kBg(0x2E, 0x34, 0x40);
const QRgb kPalette[16] = {0x3B4252, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x88C0D0, 0xE5E9F0,
                           0x4C566A, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x8FBCBB, 0xECEFF4};
constexpr int kScrollback = 10000;   // Alacritty: scrolling.history

// Ctrl++ / Ctrl+= / Ctrl+- / Ctrl+0 change the font size, as in Alacritty.
bool isZoomKey(const QKeyEvent *k)
{
    if (!(k->modifiers() & Qt::ControlModifier) || (k->modifiers() & Qt::AltModifier))
        return false;
    const int key = k->key();
    return key == Qt::Key_Plus || key == Qt::Key_Equal || key == Qt::Key_Minus || key == Qt::Key_Underscore
        || (key == Qt::Key_0 && !(k->modifiers() & Qt::ShiftModifier));
}

// The user's login shell (fish here), as Alacritty and Konsole use it. Not $SHELL: fm can be
// started from another shell (or a tool) whose $SHELL is different. IRIXFM_SHELL overrides (tests).
QString loginShell()
{
    if (qEnvironmentVariableIsSet("IRIXFM_SHELL"))
        return qEnvironmentVariable("IRIXFM_SHELL");
    if (const passwd *pw = getpwuid(getuid()); pw && pw->pw_shell && *pw->pw_shell)
        return QString::fromLocal8Bit(pw->pw_shell);
    return qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
}

TerminalView *self(void *user)
{
    return static_cast<TerminalView *>(user);
}

const VTermScreenCallbacks kCallbacks = {
    /* damage */ [](VTermRect r, void *u) { return self(u)->onDamage(r); },
    /* moverect */ nullptr,
    /* movecursor */ [](VTermPos p, VTermPos o, int v, void *u) { return self(u)->onMoveCursor(p, o, v); },
    /* settermprop */ [](VTermProp p, VTermValue *v, void *u) { return self(u)->onSetTermProp(p, v); },
    /* bell */ nullptr,
    /* resize */ nullptr,
    /* sb_pushline */ [](int cols, const VTermScreenCell *cells, void *u) { return self(u)->onPushLine(cols, cells); },
    /* sb_popline */ [](int cols, VTermScreenCell *cells, void *u) { return self(u)->onPopLine(cols, cells); },
    /* sb_clear */ nullptr,
};

VTermModifier modifiers(Qt::KeyboardModifiers m, bool withShift = true)
{
    int v = VTERM_MOD_NONE;
    if (withShift && (m & Qt::ShiftModifier))
        v |= VTERM_MOD_SHIFT;
    if (m & Qt::AltModifier)
        v |= VTERM_MOD_ALT;
    if (m & Qt::ControlModifier)
        v |= VTERM_MOD_CTRL;
    return VTermModifier(v);
}

bool before(VTermPos a, VTermPos b)
{
    return a.row < b.row || (a.row == b.row && a.col <= b.col);
}
} // namespace

TerminalView::TerminalView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setFlag(ItemAcceptsInputMethod, true);
    setFlag(ItemIsFocusScope, true);
    setActiveFocusOnTab(true);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(false);
    setFlag(ItemAcceptsDrops, true);
    setFillColor(Qt::transparent);
    setAntialiasing(true);
    m_font = QFont(QStringLiteral("Noto Sans Mono"));   // the terminal font stays Noto Sans Mono 12
    m_font.setStyleHint(QFont::Monospace);
    updateMetrics();
    connect(this, &TerminalView::fontChanged, this, [this] { updateMetrics(); resizeTerminal(); update(); });
    // After the shell prints (a prompt after `cd`), look where it is: /proc/<pid>/cwd. Works for
    // any shell, without prompt hooks (OSC 7).
    m_cwdCheck = new QTimer(this);
    m_cwdCheck->setSingleShot(true);
    m_cwdCheck->setInterval(120);
    connect(m_cwdCheck, &QTimer::timeout, this, [this] {
        if (!running())
            return;
        const QString cwd = QFileInfo(QStringLiteral("/proc/%1/cwd").arg(m_pty->processId())).symLinkTarget();
        if (!cwd.isEmpty() && cwd != m_cwd) {
            m_cwd = cwd;
            Q_EMIT currentDirectoryChanged();
        }
    });
    m_sizeHint = new QTimer(this);
    m_sizeHint->setSingleShot(true);
    m_sizeHint->setInterval(1200);
    connect(m_sizeHint, &QTimer::timeout, this, [this] { update(); });

    m_vt = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vt, 1);
    vterm_output_set_callback(m_vt, [](const char *s, size_t len, void *u) { self(u)->onOutput(s, len); }, this);
    VTermState *state = vterm_obtain_state(m_vt);
    VTermColor fg, bg;
    vterm_color_rgb(&fg, kFg.red(), kFg.green(), kFg.blue());
    vterm_color_rgb(&bg, kBg.red(), kBg.green(), kBg.blue());
    vterm_state_set_default_colors(state, &fg, &bg);
    for (int i = 0; i < 16; ++i) {
        VTermColor c;
        vterm_color_rgb(&c, qRed(kPalette[i]), qGreen(kPalette[i]), qBlue(kPalette[i]));
        vterm_state_set_palette_color(state, i, &c);
    }
    vterm_state_get_default_colors(state, &m_blank.fg, &m_blank.bg);   // with their "default" flags
    m_blank.width = 1;
    vterm_state_set_bold_highbright(state, 1);   // draw_bold_text_with_bright_colors = true
    m_screen = vterm_obtain_screen(m_vt);
    vterm_screen_set_callbacks(m_screen, &kCallbacks, this);
    vterm_screen_set_damage_merge(m_screen, VTERM_DAMAGE_SCROLL);
    vterm_screen_enable_altscreen(m_screen, 1);
    vterm_screen_reset(m_screen, 1);
    // No re-flow of wrapped lines on resize: libvterm 0.3.3's reflow aborts the whole program
    // ("screen_resize failed to update cursor position") in cases no dry run caught reliably
    // (user core dumps, 2026-10-21/22). New output uses the new width; the shell redraws its prompt.
    vterm_screen_enable_reflow(m_screen, false);
}

TerminalView::~TerminalView()
{
    // End the program and delete the pty here, while everything it might call back into still
    // exists. Left to ~QObject (a child, deleted after this), KPtyProcess waited for a still
    // running shell itself and crashed in KPty::logout (and readyRead could reach freed vterm).
    if (m_pty) {
        m_pty->disconnect(this);
        m_pty->pty()->disconnect(this);
        if (m_pty->state() != QProcess::NotRunning) {
            ::kill(m_pty->processId(), SIGHUP);   // as when a terminal window closes; bash ignores SIGTERM
            if (!m_pty->waitForFinished(300)) {
                m_pty->kill();
                m_pty->waitForFinished(1000);
            }
        }
        delete m_pty;
    }
    if (m_vt)
        vterm_free(m_vt);
}

bool TerminalView::running() const
{
    return m_pty && m_pty->state() != QProcess::NotRunning;
}

void TerminalView::start()
{
    if (running())
        return;
    m_pty = new KPtyProcess(this);
    const QString shell = loginShell();
    const QString program = m_program.isEmpty() ? shell : m_program;
    m_pty->setProgram(program, m_arguments);
    if (!m_cwd.isEmpty())
        m_pty->setWorkingDirectory(m_cwd);
    m_pty->setPtyChannels(KPtyProcess::AllChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("SHELL"), shell);   // tmux, nvim's :terminal etc. start the same shell
    env.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    env.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    env.insert(QStringLiteral("TERM_PROGRAM"), QStringLiteral("irixfm"));
    m_pty->setProcessEnvironment(env);
    connect(m_pty->pty(), &KPtyDevice::readyRead, this, [this] {
        const QByteArray data = m_pty->pty()->readAll();
        vterm_input_write(m_vt, data.constData(), size_t(data.size()));
        vterm_screen_flush_damage(m_screen);
        m_cwdCheck->start();
    });
    connect(m_pty, &QProcess::finished, this, [this](int code) {
        Q_EMIT runningChanged();
        Q_EMIT finished(code);
    });
    m_pty->start();
    resizeTerminal();
    Q_EMIT runningChanged();
}

void TerminalView::onOutput(const char *s, size_t len)
{
    if (m_pty)
        m_pty->pty()->write(s, qint64(len));
}

void TerminalView::sendText(const QString &text)
{
    for (const uint c : text.toUcs4())
        vterm_keyboard_unichar(m_vt, c, VTERM_MOD_NONE);
}

void TerminalView::paste()
{
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty())
        return;
    vterm_keyboard_start_paste(m_vt);
    sendText(text);
    vterm_keyboard_end_paste(m_vt);
}

// ---- screen state from libvterm -------------------------------------------------------------

// Repaint only the cells that changed (QQuickPaintedItem clips the painter to the dirty area):
// typing then repaints a cell or two, not the whole screen. Scrolled back, rows are shifted, so
// everything.
void TerminalView::updateCells(int startRow, int startCol, int endRow, int endCol)
{
    if (m_scrollOffset > 0) {
        update();
        return;
    }
    const QRectF r(4 + startCol * m_cellW, 3 + startRow * m_cellH, (endCol - startCol) * m_cellW, (endRow - startRow) * m_cellH);
    update(r.toAlignedRect().adjusted(-1, -1, 1, 1));
}

int TerminalView::onDamage(VTermRect r)
{
    updateCells(r.start_row, r.start_col, r.end_row, r.end_col);
    return 1;
}

int TerminalView::onMoveCursor(VTermPos pos, VTermPos old, int visible)
{
    m_cursor = pos;
    m_cursorVisible = visible;
    updateCells(old.row, old.col, old.row + 1, old.col + 1);
    updateCells(pos.row, pos.col, pos.row + 1, pos.col + 1);
    return 1;
}

int TerminalView::onSetTermProp(VTermProp prop, VTermValue *val)
{
    switch (prop) {
    case VTERM_PROP_TITLE: {
        static QByteArray buffer;
        if (val->string.initial)
            buffer.clear();
        buffer.append(val->string.str, int(val->string.len));
        if (val->string.final) {
            m_title = QString::fromUtf8(buffer);
            Q_EMIT titleChanged();
        }
        return 1;   // nothing on screen changes (the prompt sets it every time)
    }
    case VTERM_PROP_CURSORVISIBLE:
        m_cursorVisible = val->boolean;
        updateCells(m_cursor.row, m_cursor.col, m_cursor.row + 1, m_cursor.col + 1);
        return 1;
    case VTERM_PROP_ALTSCREEN:
        m_altScreen = val->boolean;
        m_scrollOffset = 0;
        break;
    case VTERM_PROP_MOUSE:
        m_mouseMode = val->number;
        return 1;
    default:
        break;
    }
    update();
    return 1;
}

int TerminalView::onPushLine(int cols, const VTermScreenCell *cells)
{
    int n = cols;
    while (n > 0 && !cells[n - 1].chars[0] && VTERM_COLOR_IS_DEFAULT_BG(&cells[n - 1].bg) && !cells[n - 1].attrs.reverse
           && !cells[n - 1].attrs.underline)
        --n;
    if (int(m_scrollback.size()) >= kScrollback)
        m_scrollback.pop_front();
    SbLine &line = m_scrollback.emplace_back();
    line.cells.reserve(size_t(n));
    for (int i = 0; i < n; ++i) {
        const VTermScreenCell &c = cells[i];
        line.cells.push_back(SbCell{c.chars[0], c.fg, c.bg, c.attrs, uint8_t(c.width)});
        if (c.chars[0] && c.chars[1]) {
            line.more.push_back(uint32_t(i));
            for (int k = 1; k < VTERM_MAX_CHARS_PER_CELL; ++k)
                line.more.push_back(c.chars[k]);
        }
    }
    return 1;
}

void TerminalView::unpackLine(const SbLine &line, VTermScreenCell *out, int cols) const
{
    const int n = std::min(cols, int(line.cells.size()));
    for (int i = 0; i < n; ++i) {
        const SbCell &c = line.cells[size_t(i)];
        VTermScreenCell &o = out[i];
        o.chars[0] = c.ch;
        std::fill(o.chars + 1, o.chars + VTERM_MAX_CHARS_PER_CELL, 0);
        o.width = char(c.width);
        o.attrs = c.attrs;
        o.fg = c.fg;
        o.bg = c.bg;
    }
    std::fill(out + n, out + cols, m_blank);
    for (size_t i = 0; i < line.more.size(); i += VTERM_MAX_CHARS_PER_CELL) {
        const int col = int(line.more[i]);
        if (col < cols)
            std::copy(line.more.begin() + qsizetype(i) + 1, line.more.begin() + qsizetype(i) + VTERM_MAX_CHARS_PER_CELL, out[col].chars + 1);
    }
}

int TerminalView::onPopLine(int cols, VTermScreenCell *cells)
{
    if (m_scrollback.empty())
        return 0;
    unpackLine(m_scrollback.back(), cells, cols);
    m_scrollback.pop_back();
    return 1;
}

// ---- geometry ----------------------------------------------------------------------------------

void TerminalView::setFontSize(qreal points)
{
    if (!std::isfinite(points))   // e.g. a QML binding mid-update
        return;
    points = std::clamp(points, 6.0, 48.0);
    if (qAbs(points - m_pointSize) < .01)
        return;
    m_pointSize = s_lastPointSize = points;
    Q_EMIT fontChanged();
    m_sizeHint->start();
}

void TerminalView::fitWidth()
{
    int longest = 0, current = 0;
    if (!m_altScreen) {
        const VTermState *state = vterm_obtain_state(m_vt);
        for (int row = 0; row < m_rows; ++row) {
            const VTermLineInfo *info = vterm_state_get_lineinfo(state, row);
            if (!(info && info->continuation))
                current = 0;
            int used = 0;   // up to the last non-blank cell
            for (int col = 0; col < m_cols; ++col) {
                VTermScreenCell cell{};
                vterm_screen_get_cell(m_screen, VTermPos{row, col}, &cell);
                if (cell.chars[0] && cell.chars[0] != ' ')
                    used = col + 1;
            }
            const VTermLineInfo *next = row + 1 < m_rows ? vterm_state_get_lineinfo(state, row + 1) : nullptr;
            current += (next && next->continuation) ? m_cols : used;
            longest = std::max(longest, current);
        }
    }
    qreal points = 12;
    if (longest > 0 && width() > 8 + m_cellW) {   // not while hidden (width 0)
        const qreal cellW = (width() - 8) / (longest + 1);   // +1: room for the cursor
        points = std::min(12.0, std::floor(m_pointSize * cellW / m_cellW * 10) / 10);
    }
    // Glide there, so the output visibly re-flows into the new width.
    if (!m_sizeAnim) {
        m_sizeAnim = new QVariantAnimation(this);
        m_sizeAnim->setDuration(220);
        m_sizeAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_sizeAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) { setFontSize(v.toReal()); });
    }
    m_sizeAnim->stop();
    m_sizeAnim->setStartValue(m_pointSize);
    m_sizeAnim->setEndValue(points);
    m_sizeAnim->start();
}

void TerminalView::zoomBy(qreal factor)
{
    setFontSize(m_pointSize * factor);
}

void TerminalView::updateMetrics()
{
    m_font.setPointSizeF(m_pointSize);
    const QFontMetricsF fm(m_font);
    m_cellW = fm.horizontalAdvance(QLatin1Char('M'));
    m_boldFont = m_font;
    m_boldFont.setBold(true);
    m_italicFont = m_font;
    m_italicFont.setItalic(true);
    const QFont fonts[3] = {m_font, m_boldFont, m_italicFont};
    for (int i = 0; i < 3; ++i) {
        m_raw[i] = QRawFont::fromFont(fonts[i]);
        m_ascii[i].fill(quint32(-1));
        m_glyphs[i].clear();
    }
    m_cellH = std::ceil(fm.lineSpacing());
    m_ascent = fm.ascent();
}

void TerminalView::geometryChange(const QRectF &n, const QRectF &o)
{
    QQuickPaintedItem::geometryChange(n, o);
    if (n.size() != o.size())
        resizeTerminal();
}

void TerminalView::resizeTerminal()
{
    if (width() < 1 || height() < 1 || !(m_cellW > 0) || !(m_cellH > 0))   // hidden / not laid out yet
        return;
    const int cols = std::clamp(int((width() - 8) / m_cellW), 10, 2000);
    const int rows = std::clamp(int((height() - 6) / m_cellH), 3, 1000);
    if (cols != m_cols || rows != m_rows) {
        // Re-flow unless that would trip libvterm's abort (see above); the shell redraws anyway.
        vterm_set_size(m_vt, rows, cols);
        m_cols = cols;
        m_rows = rows;
        vterm_screen_flush_damage(m_screen);
        Q_EMIT sizeChanged();
    }
    if (m_pty)
        m_pty->pty()->setWinSize(m_rows, m_cols);
    update();
}

// ---- painting ----------------------------------------------------------------------------------

// The cells of a visible row: from the screen, or with a scroll offset from the history.
// Returns false for rows above the oldest history line.
bool TerminalView::rowCells(int row, VTermScreenCell *out) const
{
    const int histRow = int(m_scrollback.size()) - m_scrollOffset + row;
    if (m_scrollOffset > 0 && histRow < int(m_scrollback.size())) {
        if (histRow < 0)
            return false;
        unpackLine(m_scrollback[size_t(histRow)], out, m_cols);
        return true;
    }
    for (int col = 0; col < m_cols; ++col)
        vterm_screen_get_cell(m_screen, VTermPos{row - (m_scrollOffset > 0 ? m_scrollOffset : 0), col}, &out[col]);
    return true;
}

QRgb TerminalView::toRgb(const VTermColor &c, bool fg) const
{
    if (fg ? VTERM_COLOR_IS_DEFAULT_FG(&c) : VTERM_COLOR_IS_DEFAULT_BG(&c))
        return fg ? kFg.rgba() : 0;   // default background: none (the item's see-through one)
    if (VTERM_COLOR_IS_RGB(&c))
        return qRgb(c.rgb.red, c.rgb.green, c.rgb.blue);
    VTermColor rgb = c;
    vterm_screen_convert_color_to_rgb(m_screen, &rgb);
    return qRgb(rgb.rgb.red, rgb.rgb.green, rgb.rgb.blue);
}

// Glyph of a character in the (regular / bold / italic) font; 0 if it isn't there, then the cell
// is drawn with drawText, which falls back to other fonts.
quint32 TerminalView::glyphFor(int style, char32_t c) const
{
    if (c < 128 && m_ascii[style][c] != quint32(-1))
        return m_ascii[style][c];
    auto it = m_glyphs[style].constFind(c);
    if (it != m_glyphs[style].cend())
        return *it;
    char16_t utf16[2];
    const int n = QChar::requiresSurrogates(c) ? 2 : 1;
    if (n == 2) {
        utf16[0] = QChar::highSurrogate(c);
        utf16[1] = QChar::lowSurrogate(c);
    } else {
        utf16[0] = char16_t(c);
    }
    quint32 glyph = 0;
    int count = 1;
    if (!m_raw[style].glyphIndexesForChars(reinterpret_cast<const QChar *>(utf16), n, &glyph, &count) || count != 1)
        glyph = 0;
    if (c < 128)
        m_ascii[style][c] = glyph;
    else
        m_glyphs[style].insert(c, glyph);
    return glyph;
}

void TerminalView::paint(QPainter *p)
{
    // Nord background, slightly see-through like the user's Alacritty (opacity 0.8).
    QColor bg = kBg;
    bg.setAlphaF(.8);   // Alacritty: window.opacity
    p->fillRect(boundingRect(), bg);
    const QFont fonts[3] = {m_font, m_boldFont, m_italicFont};
    const qreal x0 = 4, y0 = 3;
    const VTermPos selA = before(m_selA, m_selB) ? m_selA : m_selB, selB = before(m_selA, m_selB) ? m_selB : m_selA;
    // Only the rows in the damaged part (update(rect) clips the painter to it).
    const QRectF clip = p->hasClipping() ? p->clipBoundingRect() : boundingRect();
    // (A row more each side, for glyphs that reach into the next row.)
    const int firstRow = std::max(0, int((clip.top() - y0) / m_cellH) - 1);
    const int lastRow = std::min(m_rows - 1, int((clip.bottom() - y0) / m_cellH) + 1);
    m_rowBuf.resize(size_t(m_cols));
    VTermScreenCell *cells = m_rowBuf.data();
    // All backgrounds first, then the text: one glyph run per colour and style for the whole
    // screen, each glyph placed at its cell. Per-cell drawText (a QString, a shaping pass and a
    // font switch each) was most of the painting time.
    for (TextRun &r : m_runs) {
        r.glyphs.clear();
        r.positions.clear();
    }
    struct Extra   // drawn after the runs: cells drawText has to do, underlines
    {
        QRectF rect;
        QRgb fg;
        int style;
        QString text;
    };
    std::vector<Extra> extras;
    TextRun *run = nullptr;

    for (int row = firstRow; row <= lastRow; ++row) {
        if (!rowCells(row, cells))
            continue;
        const int top = qRound(y0 + row * m_cellH), bottom = qRound(y0 + row * m_cellH + m_cellH);
        const qreal baseline = y0 + row * m_cellH + m_ascent;
        // backgrounds, a run of cells with the same colour at a time (whole pixels, no seams)
        QRgb runBg = 0;
        int runStart = 0;
        auto flushBg = [&](int end) {
            if (qAlpha(runBg)) {
                const int left = qRound(x0 + runStart * m_cellW), right = qRound(x0 + end * m_cellW);
                p->fillRect(QRect(left, top, right - left, bottom - top), QColor::fromRgba(runBg));
            }
        };
        for (int col = 0; col < m_cols;) {
            const VTermScreenCell &cell = cells[col];
            const int w = std::max(1, int(cell.width));
            QRgb cbg = toRgb(cell.bg, false);
            if (cell.attrs.reverse)
                cbg = toRgb(cell.fg, true);
            const VTermPos here{row, col};
            if (m_selA.row >= 0 && before(selA, here) && before(here, selB))
                cbg = qRgb(0x43, 0x4C, 0x5E);   // nord2
            if (cbg != runBg) {
                flushBg(col);
                runBg = cbg;
                runStart = col;
            }
            col += w;
        }
        flushBg(m_cols);
        // text and underlines, collected
        for (int col = 0; col < m_cols; ++col) {
            const VTermScreenCell &cell = cells[col];
            const bool text = cell.chars[0] && cell.chars[0] != ' ';
            if (!text && !cell.attrs.underline)
                continue;
            QRgb fg = toRgb(cell.fg, true);
            if (cell.attrs.reverse) {
                const QRgb b = toRgb(cell.bg, false);
                fg = qAlpha(b) ? b : kBg.rgba();
            }
            const qreal left = x0 + col * m_cellW;
            const int style = cell.attrs.bold ? 1 : cell.attrs.italic ? 2 : 0;
            if (text) {
                const quint32 glyph = cell.width == 1 && !cell.chars[1] ? glyphFor(style, char32_t(cell.chars[0])) : 0;
                if (glyph) {
                    if (!run || run->fg != fg || run->style != style) {
                        run = nullptr;
                        for (TextRun &r : m_runs)
                            if (r.fg == fg && r.style == style)
                                run = &r;
                        if (!run)
                            run = &m_runs.emplace_back(TextRun{fg, style, {}, {}});
                    }
                    run->glyphs.append(glyph);
                    run->positions.append(QPointF(left, baseline));
                } else {   // wide, combined or not in the font: drawText, with font fallback
                    QString chars;
                    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i)
                        chars += QString::fromUcs4(reinterpret_cast<const char32_t *>(&cell.chars[i]), 1);
                    extras.push_back(Extra{QRectF(left, baseline, 0, 0), fg, style, chars});
                }
            }
            if (cell.attrs.underline)
                extras.push_back(Extra{QRectF(left, y0 + row * m_cellH + m_cellH - 2, m_cellW * std::max(1, int(cell.width)), 1), fg, -1, {}});
            if (cell.width > 1)
                ++col;
        }
    }
    for (TextRun &r : m_runs) {
        if (r.glyphs.isEmpty())
            continue;
        QGlyphRun glyphRun;
        glyphRun.setRawFont(m_raw[r.style]);
        glyphRun.setGlyphIndexes(r.glyphs);
        glyphRun.setPositions(r.positions);
        p->setPen(QColor::fromRgba(r.fg));
        p->drawGlyphRun(QPointF(), glyphRun);
    }
    for (const Extra &e : extras) {
        if (e.style < 0) {
            p->fillRect(e.rect, QColor::fromRgba(e.fg));
        } else {
            p->setFont(fonts[e.style]);
            p->setPen(QColor::fromRgba(e.fg));
            p->drawText(e.rect.topLeft(), e.text);
        }
    }
    if (m_runs.size() > 64)   // e.g. after a truecolour gradient: don't keep them all
        m_runs.clear();
    // Cursor: underline when focused (as in Alacritty's config), hollow box when not.
    if (m_cursorVisible && m_scrollOffset == 0) {
        const QRectF c(x0 + m_cursor.col * m_cellW, y0 + m_cursor.row * m_cellH, m_cellW, m_cellH);
        p->setPen(QPen(kFg, 1));
        if (m_focused)
            p->fillRect(QRectF(c.left(), c.bottom() - std::max(2.0, m_cellH * .15), c.width(), std::max(2.0, m_cellH * .15)), kFg);
        else
            p->drawRect(c.adjusted(.5, .5, -.5, -.5));
    }
    // "14 pt" in the top-right corner while the size changes.
    if (m_sizeHint->isActive()) {
        QFont f = m_font;
        f.setPointSizeF(11);
        f.setBold(true);
        p->setFont(f);
        const QString text = QString::number(m_pointSize, 'f', m_pointSize < 10 ? 1 : 0) + QStringLiteral(" pt");
        const QRectF box(width() - 84, 10, 72, 26);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x3B, 0x42, 0x52, 230));
        p->drawRoundedRect(box, 8, 8);
        p->setPen(QColor(0x88, 0xC0, 0xD0));
        p->drawText(box, Qt::AlignCenter, text);
    }
}

QString TerminalView::screenText() const
{
    QStringList lines;
    for (int row = 0; row < m_rows; ++row) {
        QString line;
        for (int col = 0; col < m_cols; ++col) {
            VTermScreenCell cell{};
            vterm_screen_get_cell(m_screen, VTermPos{row, col}, &cell);
            line += cell.chars[0] ? QString::fromUcs4(reinterpret_cast<const char32_t *>(&cell.chars[0]), 1) : QStringLiteral(" ");
        }
        lines << line.trimmed();
    }
    return lines.join(QLatin1Char('\n'));
}

// ---- input -------------------------------------------------------------------------------------

// Touchpad pinch: the font follows the fingers. Qt Quick doesn't reliably hand a real touchpad
// gesture (Wayland zwp_pointer_gestures) to a painted item, so the terminal also watches its
// window and takes pinches that happen over it.
bool TerminalView::handleGesture(QEvent *e)
{
    if (e->type() != QEvent::NativeGesture)
        return false;
    auto *g = static_cast<QNativeGestureEvent *>(e);
    if (g->gestureType() == Qt::ZoomNativeGesture && g->value() != 0)
        zoomBy(1 + g->value());
    e->accept();
    return true;
}

void TerminalView::itemChange(ItemChange change, const ItemChangeData &data)
{
    if (change == ItemSceneChange) {
        if (m_watchedWindow)
            m_watchedWindow->removeEventFilter(this);
        m_watchedWindow = data.window;
        if (m_watchedWindow)
            m_watchedWindow->installEventFilter(this);
    }
    QQuickPaintedItem::itemChange(change, data);
}

// Drags and pinches are taken at the window, before Qt Quick's own delivery: real Wayland
// drags and touchpad gestures didn't reliably reach this item that way.
bool TerminalView::eventFilter(QObject *watched, QEvent *e)
{
    if (watched != m_watchedWindow || !isVisible())
        return QQuickPaintedItem::eventFilter(watched, e);
    switch (e->type()) {
    case QEvent::NativeGesture: {
        auto *g = static_cast<QNativeGestureEvent *>(e);
        if (!contains(mapFromScene(g->scenePosition())))
            break;
        if (g->gestureType() == Qt::BeginNativeGesture)
            qCDebug(lcTerminal) << "pinch over the terminal, device" << (g->device() ? g->device()->name() : QString());
        return handleGesture(e);
    }
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto *d = static_cast<QDragMoveEvent *>(e);
        const bool over = contains(mapFromScene(d->position())) && (d->mimeData()->hasUrls() || d->mimeData()->hasText());
        if (!over) {
            m_dragOver = false;
            break;
        }
        if (!m_dragOver) {
            qCDebug(lcTerminal) << "drag over the terminal:" << d->mimeData()->formats();
            m_dragOver = true;
            if (e->type() == QEvent::DragMove) {   // whatever Qt Quick had under the drag lets go
                QDragLeaveEvent leave;
                QCoreApplication::sendEvent(m_watchedWindow, &leave);
            }
        }
        d->setDropAction(Qt::CopyAction);   // nothing is moved: only the path is typed
        d->accept();
        return true;
    }
    case QEvent::DragLeave:
        m_dragOver = false;
        break;
    case QEvent::Drop: {
        auto *d = static_cast<QDropEvent *>(e);
        if (!m_dragOver && !contains(mapFromScene(d->position())))
            break;
        m_dragOver = false;
        qCDebug(lcTerminal) << "drop on the terminal:" << d->mimeData()->urls();
        dropEvent(d);
        return true;
    }
    default:
        break;
    }
    return QQuickPaintedItem::eventFilter(watched, e);
}

bool TerminalView::event(QEvent *e)
{
    if (handleGesture(e))
        return true;
    // The terminal gets the keys (Ctrl+C, Ctrl+W, …) before the window's shortcuts — except
    // Ctrl+Shift+… (copy / paste / tab commands), Ctrl+PgUp/PgDn / Ctrl+Tab (switch tabs) and Alt+1…9.
    if (e->type() == QEvent::ShortcutOverride) {
        auto *k = static_cast<QKeyEvent *>(e);
        const auto m = k->modifiers();
        const bool ctrlShift = (m & Qt::ControlModifier) && (m & Qt::ShiftModifier);
        const bool tabKeys = (m & Qt::ControlModifier)
            && (k->key() == Qt::Key_PageUp || k->key() == Qt::Key_PageDown || k->key() == Qt::Key_Tab || k->key() == Qt::Key_Backtab);
        const bool altDigit = (m & Qt::AltModifier) && k->key() >= Qt::Key_1 && k->key() <= Qt::Key_9;
        if ((!ctrlShift && !tabKeys && !altDigit) || isZoomKey(k)) {
            e->accept();
            return true;
        }
    }
    return QQuickPaintedItem::event(e);
}

void TerminalView::keyPressEvent(QKeyEvent *e)
{
    const auto m = e->modifiers();
    if (isZoomKey(e)) {
        if (e->key() == Qt::Key_0)
            setFontSize(12);
        else
            zoomBy(e->key() == Qt::Key_Minus || e->key() == Qt::Key_Underscore ? 1 / 1.1 : 1.1);
        return;
    }
    if ((m & Qt::ControlModifier) && (m & Qt::ShiftModifier)) {
        if (e->key() == Qt::Key_C) { copySelection(); return; }
        if (e->key() == Qt::Key_V) { paste(); return; }
    }
    if ((m & Qt::ShiftModifier) && (e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown) && !m_altScreen) {
        m_scrollOffset = std::clamp(m_scrollOffset + (e->key() == Qt::Key_PageUp ? m_rows / 2 : -m_rows / 2), 0, int(m_scrollback.size()));
        update();
        return;
    }
    if (m_scrollOffset) {   // typing goes back to the prompt
        m_scrollOffset = 0;
        update();
    }
    VTermKey key = VTERM_KEY_NONE;
    switch (e->key()) {
    case Qt::Key_Return: case Qt::Key_Enter: key = VTERM_KEY_ENTER; break;
    case Qt::Key_Tab: case Qt::Key_Backtab: key = VTERM_KEY_TAB; break;
    case Qt::Key_Backspace: key = VTERM_KEY_BACKSPACE; break;
    case Qt::Key_Escape: key = VTERM_KEY_ESCAPE; break;
    case Qt::Key_Up: key = VTERM_KEY_UP; break;
    case Qt::Key_Down: key = VTERM_KEY_DOWN; break;
    case Qt::Key_Left: key = VTERM_KEY_LEFT; break;
    case Qt::Key_Right: key = VTERM_KEY_RIGHT; break;
    case Qt::Key_Insert: key = VTERM_KEY_INS; break;
    case Qt::Key_Delete: key = VTERM_KEY_DEL; break;
    case Qt::Key_Home: key = VTERM_KEY_HOME; break;
    case Qt::Key_End: key = VTERM_KEY_END; break;
    case Qt::Key_PageUp: key = VTERM_KEY_PAGEUP; break;
    case Qt::Key_PageDown: key = VTERM_KEY_PAGEDOWN; break;
    default:
        if (e->key() >= Qt::Key_F1 && e->key() <= Qt::Key_F12)
            key = VTermKey(VTERM_KEY_FUNCTION(e->key() - Qt::Key_F1 + 1));
    }
    if (key != VTERM_KEY_NONE) {
        vterm_keyboard_key(m_vt, key, modifiers(m | (e->key() == Qt::Key_Backtab ? Qt::ShiftModifier : Qt::NoModifier)));
    } else if ((m & Qt::ControlModifier) && e->key() >= Qt::Key_A && e->key() <= Qt::Key_Z) {
        vterm_keyboard_unichar(m_vt, uint32_t('a' + (e->key() - Qt::Key_A)), modifiers(m, false));
    } else if ((m & Qt::ControlModifier) && e->key() < 0x80 && e->key() >= 0x20) {
        vterm_keyboard_unichar(m_vt, uint32_t(QChar(e->key()).toLower().unicode()), modifiers(m, false));
    } else if (!e->text().isEmpty()) {
        for (const uint c : e->text().toUcs4())
            vterm_keyboard_unichar(m_vt, c, modifiers(m, false));
    } else {
        QQuickPaintedItem::keyPressEvent(e);
        return;
    }
    // no repaint here: the program's echo damages what changes
}

void TerminalView::inputMethodEvent(QInputMethodEvent *e)
{
    sendText(e->commitString());
    e->accept();
}

VTermPos TerminalView::cellAt(QPointF p) const
{
    return VTermPos{std::clamp(int((p.y() - 3) / m_cellH), 0, m_rows - 1), std::clamp(int((p.x() - 4) / m_cellW), 0, m_cols - 1)};
}

void TerminalView::mousePressEvent(QMouseEvent *e)
{
    forceActiveFocus(Qt::MouseFocusReason);
    const VTermPos c = cellAt(e->position());
    if (m_mouseMode && !(e->modifiers() & Qt::ShiftModifier)) {   // the program wants the mouse (nvim, htop)
        vterm_mouse_move(m_vt, c.row, c.col, modifiers(e->modifiers()));
        vterm_mouse_button(m_vt, e->button() == Qt::LeftButton ? 1 : e->button() == Qt::MiddleButton ? 2 : 3, true, modifiers(e->modifiers()));
        return;
    }
    if (e->button() == Qt::MiddleButton) {   // primary-selection paste, like other X/Wayland terminals
        const QString text = QGuiApplication::clipboard()->text(QClipboard::Selection);
        if (!text.isEmpty()) { vterm_keyboard_start_paste(m_vt); sendText(text); vterm_keyboard_end_paste(m_vt); }
        return;
    }
    m_selecting = e->button() == Qt::LeftButton;
    m_selA = m_selB = c;
    update();
}

void TerminalView::mouseMoveEvent(QMouseEvent *e)
{
    const VTermPos c = cellAt(e->position());
    if (m_mouseMode && !(e->modifiers() & Qt::ShiftModifier)) {
        vterm_mouse_move(m_vt, c.row, c.col, modifiers(e->modifiers()));
        return;
    }
    if (m_selecting) {
        m_selB = c;
        update();
    }
}

void TerminalView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_mouseMode && !(e->modifiers() & Qt::ShiftModifier)) {
        vterm_mouse_button(m_vt, e->button() == Qt::LeftButton ? 1 : e->button() == Qt::MiddleButton ? 2 : 3, false, modifiers(e->modifiers()));
        return;
    }
    if (m_selecting) {
        m_selecting = false;
        if (m_selA.row == m_selB.row && m_selA.col == m_selB.col) {
            m_selA = m_selB = VTermPos{-1, -1};
        } else {
            const QString text = textBetween(m_selA, m_selB);
            QGuiApplication::clipboard()->setText(text, QClipboard::Selection);   // save_to_clipboard-style
        }
        update();
    }
}

QString TerminalView::textBetween(VTermPos a, VTermPos b) const
{
    if (!before(a, b))
        std::swap(a, b);
    QStringList lines;
    for (int row = a.row; row <= b.row; ++row) {
        QString line;
        const int from = row == a.row ? a.col : 0, to = row == b.row ? b.col : m_cols - 1;
        for (int col = from; col <= to; ++col) {
            VTermScreenCell cell{};
            vterm_screen_get_cell(m_screen, VTermPos{row, col}, &cell);
            line += cell.chars[0] ? QString::fromUcs4(reinterpret_cast<const char32_t *>(&cell.chars[0]), 1) : QStringLiteral(" ");
        }
        lines << (row == b.row ? line : line.trimmed());
    }
    return lines.join(QLatin1Char('\n'));
}

void TerminalView::copySelection()
{
    if (m_selA.row >= 0)
        QGuiApplication::clipboard()->setText(textBetween(m_selA, m_selB));
}

void TerminalView::wheelEvent(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {   // Ctrl+wheel: font size
        const qreal d = e->pixelDelta().y() ? e->pixelDelta().y() / 400.0 : e->angleDelta().y() / 1200.0;
        zoomBy(1 + d);
        e->accept();
        return;
    }
    // Touchpads send many small pixel deltas: add them up and scroll a line per cell height
    // (dividing each delta on its own rounded every one of them down to 0). Wheels: 3 lines a notch.
    int steps;
    if (!e->pixelDelta().isNull()) {
        m_wheelRest += e->pixelDelta().y();
        steps = int(m_wheelRest / m_cellH);
        m_wheelRest -= steps * m_cellH;
    } else {
        m_wheelRest += e->angleDelta().y() / 40.0;
        steps = int(m_wheelRest);
        m_wheelRest -= steps;
    }
    e->accept();
    if (!steps)
        return;
    if (m_mouseMode) {   // e.g. nvim with mouse=a
        const VTermPos c = cellAt(e->position());
        vterm_mouse_move(m_vt, c.row, c.col, VTERM_MOD_NONE);
        for (int i = 0; i < std::abs(steps); ++i)
            vterm_mouse_button(m_vt, steps > 0 ? 4 : 5, true, VTERM_MOD_NONE);
    } else if (m_altScreen) {   // full-screen programs without mouse: arrow keys
        for (int i = 0; i < std::abs(steps); ++i)
            vterm_keyboard_key(m_vt, steps > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
    } else {   // the shell: scroll back through the history
        m_scrollOffset = std::clamp(m_scrollOffset + steps, 0, int(m_scrollback.size()));
        update();
    }
    e->accept();
}

// ---- drag and drop -----------------------------------------------------------------------------

void TerminalView::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls() || e->mimeData()->hasText()) {
        e->setDropAction(Qt::CopyAction);   // nothing is moved: only the path is typed
        e->accept();
    } else {
        e->ignore();
    }
}

void TerminalView::dragMoveEvent(QDragMoveEvent *e)
{
    e->setDropAction(Qt::CopyAction);
    e->accept();
}

void TerminalView::dropEvent(QDropEvent *e)
{
    QStringList paths;
    for (const QUrl &u : e->mimeData()->urls())
        paths << (u.isLocalFile() ? u.toLocalFile() : u.toString());
    if (paths.isEmpty() && e->mimeData()->hasText())
        paths << e->mimeData()->text();
    insertPaths(paths);
    e->setDropAction(Qt::CopyAction);
    e->accept();
    forceActiveFocus();
}

static QString shellQuote(const QString &p)
{
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9_@%+=:,./~-]+$"));
    return plain.match(p).hasMatch() ? p : QLatin1Char('\'') + QString(p).replace(QLatin1Char('\''), QStringLiteral("'\\''")) + QLatin1Char('\'');
}

bool TerminalView::changeDirectory(const QString &path)
{
    if (!running() || !m_program.isEmpty() || path.isEmpty())
        return false;
    if (path == m_cwd)   // already there (e.g. the path bar is following the shell's own cd)
        return true;
    const pid_t fg = tcgetpgrp(m_pty->pty()->masterFd());
    if (fg <= 0 || fg != m_pty->processId())   // a program runs in the shell: leave it alone
        return false;
    // Ctrl+E Ctrl+U clears what's typed; the leading space keeps it out of the history.
    sendText(QStringLiteral("\x05\x15 cd -- ") + shellQuote(path) + QLatin1Char('\r'));
    m_cwd = path;
    return true;
}

void TerminalView::insertPaths(const QStringList &paths)
{
    QStringList quoted;
    for (const QString &p : paths)
        quoted << shellQuote(p);
    if (quoted.isEmpty())
        return;
    m_scrollOffset = 0;
    vterm_keyboard_start_paste(m_vt);
    sendText(quoted.join(QLatin1Char(' ')) + QLatin1Char(' '));
    vterm_keyboard_end_paste(m_vt);
    update();
}

void TerminalView::focusInEvent(QFocusEvent *e)
{
    m_focused = true;
    updateCells(m_cursor.row, m_cursor.col, m_cursor.row + 1, m_cursor.col + 1);   // underline cursor
    QQuickPaintedItem::focusInEvent(e);
}

void TerminalView::focusOutEvent(QFocusEvent *e)
{
    m_focused = false;
    updateCells(m_cursor.row, m_cursor.col, m_cursor.row + 1, m_cursor.col + 1);   // hollow box
    QQuickPaintedItem::focusOutEvent(e);
}
