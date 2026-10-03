#ifndef TUI_LINE_EDITOR_H
#define TUI_LINE_EDITOR_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

// A keystroke, decoded from raw bytes. `text` only carries meaning for
// `Char`: one decoded Unicode character (never a whole pasted string --
// TuiKeyDecoder emits one event per code point, exactly like typing).
enum class TuiKey {
    None,
    Char,
    Left,
    Right,
    Home,
    End,
    Backspace,
    Delete,
    Enter,
    Tab,
    Escape,
    Up,
    Down,
    PageUp,
    PageDown,
    Interrupt,
    // Readline-style kill commands supported by the key decoder.
    KillToLineStart,  // Ctrl+U: erase from line start to the cursor
    KillToLineEnd,    // Ctrl+K: erase from the cursor to line end
    KillPreviousWord, // Ctrl+W: erase the word immediately before the cursor
};

struct TuiKeyEvent {
    TuiKey key = TuiKey::None;
    QString text;
};

// Turns a stream of raw terminal bytes into TuiKeyEvents. A terminal is free
// to split any multi-byte sequence across reads -- an escape sequence or a
// UTF-8 code point can arrive with the socket notifier firing in between --
// and it routinely does under real scheduling latency. feed() is therefore a
// state machine whose buffer survives across calls: a half-delivered
// sequence just waits for the rest instead of ever being compared as a
// whole string.
class TuiKeyDecoder
{
public:
    QVector<TuiKeyEvent> feed(const QByteArray &bytes);

    // Resolve a lone Escape after the caller's short timer; a pending sequence is not Escape.
    QVector<TuiKeyEvent> resolvePendingEscape();

private:
    enum class State {
        Ground,
        Escape,
        Csi,
        Utf8,
    };

    void processByte(unsigned char byte, QVector<TuiKeyEvent> &events);
    void finishCsi(unsigned char finalByte, QVector<TuiKeyEvent> &events);

    State m_state = State::Ground;
    // CSI parameter bytes collected between "ESC [" and the final byte
    // (e.g. "5" out of "ESC [ 5 ~"). Cleared on entry to Csi and on exit.
    QByteArray m_csiParams;
    // Bytes of a UTF-8 sequence collected so far, including the lead byte.
    QByteArray m_utf8Buffer;
    int m_utf8Expected = 0;
};

// Reassembles TuiKeyEvents into a line of text. This is the only place that
// turns keystrokes into text: everything below it (grammar, ClientCore, the
// reply encoder) still only ever sees a finished line via the same
// lineReady-shaped exit classic mode always used, so it never learns a raw
// key-driven UI exists.
class TuiLineEditor
{
public:
    void setCompleter(std::function<QString(const QString &, QStringList *)> completer);

    // Applies one key event. Returns whether the screen needs to redraw.
    // When the key finished a line (Enter), `*submitted` is set to it and
    // the buffer is cleared; otherwise `*submitted` is cleared. `submitted`
    // may be null.
    bool handle(const TuiKeyEvent &event, QString *submitted);

    QString text() const { return m_text; }
    // Cursor position in display columns (per tuiDisplayWidth), not
    // characters -- a wide character moves it by two, matching what the
    // renderer actually draws.
    int cursorColumn() const;

    void reset();

private:
    void insertText(const QString &text);

    QString m_text;
    int m_cursor = 0; // character index into m_text
    QStringList m_history;
    int m_historyIndex = 0; // == m_history.size() means "not browsing history"
    std::function<QString(const QString &, QStringList *)> m_completer;
};

#endif
