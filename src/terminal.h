// fm: a terminal for tabs. libvterm (the engine Neovim's :terminal uses) on a KPty, painted with
// the user's Alacritty look (Noto Sans Mono 12, Nord colours).
#pragma once

#include <QFont>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QStringList>
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
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)                    // set by the program (OSC 0/2)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int columns READ columns NOTIFY sizeChanged)
    Q_PROPERTY(int rows READ rows NOTIFY sizeChanged)
    Q_PROPERTY(qreal fontPointSize MEMBER m_pointSize NOTIFY fontChanged)

public:
    explicit TerminalView(QQuickItem *parent = nullptr);
    ~TerminalView() override;

    void paint(QPainter *painter) override;

    QString title() const { return m_title; }
    bool running() const;
    int columns() const { return m_cols; }
    int rows() const { return m_rows; }

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
    QColor toColor(const VTermColor &c, bool fg) const;
    VTermPos cellAt(QPointF p) const;
    QString textBetween(VTermPos a, VTermPos b) const;

    VTerm *m_vt = nullptr;
    VTermScreen *m_screen = nullptr;
    QPointer<KPtyProcess> m_pty;
    QPointer<QQuickWindow> m_watchedWindow;
    bool m_dragOver = false;                   // a drag is over this terminal (taken at the window)
    QString m_program, m_cwd, m_title;
    QStringList m_arguments;
    QFont m_font;
    static inline qreal s_lastPointSize = 12;
    qreal m_pointSize = s_lastPointSize;
    QTimer *m_sizeHint = nullptr;
    class QVariantAnimation *m_sizeAnim = nullptr;              // shows "14 pt" for a moment after a change
    qreal m_cellW = 8, m_cellH = 16, m_ascent = 12;
    int m_cols = 80, m_rows = 24;
    VTermPos m_cursor{0, 0};
    bool m_cursorVisible = true, m_focused = false, m_altScreen = false;
    int m_mouseMode = 0;                        // VTERM_PROP_MOUSE_*
    std::deque<std::vector<VTermScreenCell>> m_scrollback;
    int m_scrollOffset = 0;                     // lines scrolled back into the history
    bool m_selecting = false;
    VTermPos m_selA{-1, -1}, m_selB{-1, -1};
};
