// fm: a terminal for tabs. libvterm (the engine Neovim's :terminal uses) on a KPty, painted with
// the user's Alacritty look (Noto Sans Mono 12, Nord colours).
#pragma once

#include <QFont>
#include <QHash>
#include <QRawFont>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QStringList>
#include <array>
#include <deque>
#include <vector>

extern "C" {
#include <vterm.h>
}

class KPtyProcess;
class QTimer;

class TerminalView : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QString program MEMBER m_program NOTIFY programChanged)          // "" = the login shell
    Q_PROPERTY(QStringList arguments MEMBER m_arguments NOTIFY programChanged)
    Q_PROPERTY(QString workingDirectory MEMBER m_cwd NOTIFY programChanged)
    // The shell's folder now (after `cd`), so the side's path bar can follow it.
    Q_PROPERTY(QString currentDirectory READ currentDirectory NOTIFY currentDirectoryChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)                    // set by the program (OSC 0/2)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int columns READ columns NOTIFY sizeChanged)
    Q_PROPERTY(int rows READ rows NOTIFY sizeChanged)
    Q_PROPERTY(qreal fontPointSize MEMBER m_pointSize NOTIFY fontChanged)
    Q_PROPERTY(int scrollOffset READ scrollOffset)   // lines scrolled back into the history (tests)

public:
    explicit TerminalView(QQuickItem *parent = nullptr);
    ~TerminalView() override;

    void paint(QPainter *painter) override;

    QString title() const { return m_title; }
    QString currentDirectory() const { return m_cwd; }
    bool running() const;
    int columns() const { return m_cols; }
    int rows() const { return m_rows; }
    int scrollOffset() const { return m_scrollOffset; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void sendText(const QString &text);     // typed / pasted text
    Q_INVOKABLE void paste();                           // clipboard, as a bracketed paste
    Q_INVOKABLE void copySelection();
    Q_INVOKABLE QString screenText() const;             // visible text (tests)
    // Font size: touchpad pinch, Ctrl+wheel, Ctrl++ / Ctrl+- / Ctrl+0 (as in Alacritty).
    // New terminals start at the last size used.
    Q_INVOKABLE void zoomBy(qreal factor);
    Q_INVOKABLE void setFontSize(qreal points);
    // The largest size up to 12 pt at which the visible output has no wrapped lines (the
    // thumbwheel's home button). Full-screen programs (nvim, tmux) get 12 pt.
    Q_INVOKABLE void fitWidth();
    // Dropped files and folders: their paths, shell-quoted, typed at the cursor (as in Alacritty).
    Q_INVOKABLE void insertPaths(const QStringList &paths);
    // The folder view navigated: cd the shell there — only if it's the shell at its prompt
    // (nothing else in the foreground, e.g. not nvim or tmux). Returns whether it did.
    Q_INVOKABLE bool changeDirectory(const QString &path);

    // libvterm callbacks
    int onDamage(VTermRect rect);
    int onMoveCursor(VTermPos pos, VTermPos old, int visible);
    int onSetTermProp(VTermProp prop, VTermValue *val);
    int onPushLine(int cols, const VTermScreenCell *cells);
    int onPopLine(int cols, VTermScreenCell *cells);
    void onOutput(const char *s, size_t len);

Q_SIGNALS:
    void programChanged();
    void titleChanged();
    void currentDirectoryChanged();
    void runningChanged();
    void sizeChanged();
    void fontChanged();
    void finished(int exitCode);

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    bool event(QEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *e) override;
    void itemChange(ItemChange change, const ItemChangeData &data) override;
    void keyPressEvent(QKeyEvent *e) override;
    void inputMethodEvent(QInputMethodEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;

private:
    bool handleGesture(QEvent *e);
    void updateMetrics();
    void resizeTerminal();
    bool reflowLosesCursor(int newRows, int newCols) const;
    QRgb toRgb(const VTermColor &c, bool fg) const;
    quint32 glyphFor(int style, char32_t c) const;
    bool rowCells(int row, VTermScreenCell *out) const;
    VTermPos cellAt(QPointF p) const;
    QString textBetween(VTermPos a, VTermPos b) const;

    VTerm *m_vt = nullptr;
    VTermScreen *m_screen = nullptr;
    QPointer<KPtyProcess> m_pty;
    QPointer<QQuickWindow> m_watchedWindow;
    bool m_dragOver = false;                   // a drag is over this terminal (taken at the window)
    QString m_program, m_cwd, m_title;
    QStringList m_arguments;
    QFont m_font, m_boldFont, m_italicFont;
    QRawFont m_raw[3];                         // regular, bold, italic: text is drawn as glyph runs
    mutable std::array<quint32, 128> m_ascii[3];          // glyph cache (-1: not looked up yet)
    mutable QHash<char32_t, quint32> m_glyphs[3];
    std::vector<VTermScreenCell> m_rowBuf;     // one row's cells while painting
    struct TextRun                             // the glyphs of one colour and style
    {
        QRgb fg;
        int style;
        QList<quint32> glyphs;
        QList<QPointF> positions;
    };
    std::vector<TextRun> m_runs;
    static inline qreal s_lastPointSize = 12;
    qreal m_pointSize = s_lastPointSize;
    QTimer *m_sizeHint = nullptr;
    QTimer *m_cwdCheck = nullptr;              // reads the shell's cwd shortly after output
    class QVariantAnimation *m_sizeAnim = nullptr;              // shows "14 pt" for a moment after a change
    qreal m_cellW = 8, m_cellH = 16, m_ascent = 12;
    int m_cols = 80, m_rows = 24;
    VTermPos m_cursor{0, 0};
    bool m_cursorVisible = true, m_focused = false, m_altScreen = false;
    int m_mouseMode = 0;                        // VTERM_PROP_MOUSE_*
    // History lines, compact: 20 bytes a cell instead of VTermScreenCell's 40, trailing blank cells
    // left out (they come back as blanks); the rare combining characters on the side.
    struct SbCell
    {
        uint32_t ch;
        VTermColor fg, bg;
        VTermScreenCellAttrs attrs;
        uint8_t width;
    };
    struct SbLine
    {
        std::vector<SbCell> cells;
        std::vector<uint32_t> more;   // per cell with several characters: col, then the rest
    };
    void unpackLine(const SbLine &line, VTermScreenCell *out, int cols) const;
    VTermScreenCell m_blank{};                 // an empty cell in the default colours
    std::deque<SbLine> m_scrollback;
    int m_scrollOffset = 0;
    qreal m_wheelRest = 0;                     // wheel / touchpad movement not yet a whole line                     // lines scrolled back into the history
    bool m_selecting = false;
    VTermPos m_selA{-1, -1}, m_selB{-1, -1};
};
