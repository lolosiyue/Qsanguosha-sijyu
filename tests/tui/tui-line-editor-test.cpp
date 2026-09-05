#include "tui-line-editor.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        std::printf("[FAIL] %s\n", what);
    }
}

void type(TuiLineEditor &editor, const QString &text)
{
    for (const QChar &character : text) {
        QString submitted;
        editor.handle(TuiKeyEvent{TuiKey::Char, QString(character)}, &submitted);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    TuiKeyDecoder decoder;

    // The single property that matters most: a terminal is free to split an
    // escape sequence across reads, and it routinely does.
    QVector<TuiKeyEvent> events = decoder.feed(QByteArray("\x1b["));
    check(events.isEmpty(), "half an escape sequence yields no key yet");
    events = decoder.feed(QByteArray("D"));
    check(events.size() == 1 && events.at(0).key == TuiKey::Left,
          "the rest of the sequence completes the left arrow");

    events = decoder.feed(QByteArray("ab"));
    check(events.size() == 2 && events.at(0).text == QStringLiteral("a"),
          "plain bytes decode as characters");

    events = decoder.feed(QByteArray("\x1b[5~"));
    check(events.size() == 1 && events.at(0).key == TuiKey::PageUp, "PgUp decodes");

    events = decoder.feed(QByteArray("\x03"));
    check(events.size() == 1 && events.at(0).key == TuiKey::Interrupt,
          "Ctrl+C decodes as an interrupt rather than a control character");

    // UTF-8 arrives in pieces too.
    events = decoder.feed(QByteArray("\xe6\x97"));
    check(events.isEmpty(), "half a UTF-8 code point yields no key yet");
    events = decoder.feed(QByteArray("\xb6"));
    check(events.size() == 1 && events.at(0).text == QString::fromUtf8("时"),
          "the rest of the code point completes the character");

    // Item 4: every entry in the CSI table round-tripped through real bytes,
    // not just hand-built TuiKeyEvents -- a swapped mapping in finishCsi
    // would otherwise pass unnoticed.
    events = decoder.feed(QByteArray("\x1b[H"));
    check(events.size() == 1 && events.at(0).key == TuiKey::Home, "ESC[H decodes as Home");
    events = decoder.feed(QByteArray("\x1b[F"));
    check(events.size() == 1 && events.at(0).key == TuiKey::End, "ESC[F decodes as End");
    events = decoder.feed(QByteArray("\x1b[1~"));
    check(events.size() == 1 && events.at(0).key == TuiKey::Home, "ESC[1~ decodes as Home");
    events = decoder.feed(QByteArray("\x1b[4~"));
    check(events.size() == 1 && events.at(0).key == TuiKey::End, "ESC[4~ decodes as End");
    events = decoder.feed(QByteArray("\x1b[A"));
    check(events.size() == 1 && events.at(0).key == TuiKey::Up, "ESC[A decodes as Up");
    events = decoder.feed(QByteArray("\x1b[B"));
    check(events.size() == 1 && events.at(0).key == TuiKey::Down, "ESC[B decodes as Down");
    events = decoder.feed(QByteArray("\x1b[3~"));
    check(events.size() == 1 && events.at(0).key == TuiKey::Delete, "ESC[3~ decodes as Delete");
    events = decoder.feed(QByteArray("\x1b[6~"));
    check(events.size() == 1 && events.at(0).key == TuiKey::PageDown, "ESC[6~ decodes as PageDown");

    // Item 1 (Critical): an unterminated CSI sequence must not wedge the
    // decoder forever. Overflow the parameter cap with no final byte.
    TuiKeyDecoder wedge;
    events = wedge.feed(QByteArray("\x1b[") + QByteArray(40, '1'));
    check(!events.isEmpty(), "an over-long CSI is abandoned instead of swallowing everything after it");
    events = wedge.feed(QByteArray("a"));
    check(events.size() == 1 && events.at(0).key == TuiKey::Char
              && events.at(0).text == QStringLiteral("a"),
          "input after an abandoned CSI decodes normally");

    // A fresh ESC arriving mid-CSI restarts the sequence instead of being
    // swallowed as an illegal parameter byte.
    TuiKeyDecoder restart;
    events = restart.feed(QByteArray("\x1b[12"));
    check(events.isEmpty(), "a CSI still accumulating parameters yields no key yet");
    events = restart.feed(QByteArray("\x1b[D"));
    check(events.size() == 1 && events.at(0).key == TuiKey::Left,
          "a fresh ESC mid-CSI abandons the stale sequence and starts a clean one");

    // The exact regression a reviewer reproduced: ESC[123, then 456, then a
    // fresh lone ESC, then 'A'. Before the fix this parked forever in Csi
    // and 'A' flushed the accumulated "123456" as a spurious Up. It must
    // instead come out as a clean Escape followed by a plain 'A' character
    // -- nothing from the abandoned digits leaking out as a key at all.
    TuiKeyDecoder regression;
    events = regression.feed(QByteArray("\x1b[123"));
    check(events.isEmpty(), "still parked mid-CSI, no garbage emitted while waiting");
    events = regression.feed(QByteArray("456"));
    check(events.isEmpty(), "more digits, still parked, still no garbage emitted");
    events = regression.feed(QByteArray("\x1b"));
    check(events.isEmpty(), "the fresh ESC abandons the stale CSI and is itself parked, not decoded yet");
    events = regression.feed(QByteArray("A"));
    check(events.size() == 2 && events.at(0).key == TuiKey::Escape
              && events.at(1).key == TuiKey::Char && events.at(1).text == QStringLiteral("A"),
          "resolves as Escape followed by a plain 'A' -- never a spurious Up from the abandoned digits");

    // Item 5: resolvePendingEscape, including its interaction with the
    // Critical fix -- a lone ESC after an abandoned CSI must still resolve.
    events = wedge.feed(QByteArray("\x1b"));
    check(events.isEmpty(), "a lone ESC is parked, not decoded on the spot");
    events = wedge.resolvePendingEscape();
    check(events.size() == 1 && events.at(0).key == TuiKey::Escape,
          "resolvePendingEscape resolves a parked lone ESC into an Escape keypress");
    events = wedge.resolvePendingEscape();
    check(events.isEmpty(), "resolvePendingEscape is a no-op once nothing is pending");

    TuiKeyDecoder freshDecoder;
    events = freshDecoder.resolvePendingEscape();
    check(events.isEmpty(), "resolvePendingEscape does nothing with no ESC pending at all");
    events = freshDecoder.feed(QByteArray("\x1b[D"));
    check(events.size() == 1 && events.at(0).key == TuiKey::Left,
          "a completed sequence needs no resolution");

    TuiLineEditor editor;
    type(editor, QString::fromUtf8("时语1"));
    check(editor.text() == QString::fromUtf8("时语1"), "typing accumulates a line");
    check(editor.cursorColumn() == 5, "the cursor is measured in columns, not characters");

    QString submitted;
    editor.handle(TuiKeyEvent{TuiKey::Left}, &submitted);
    check(editor.cursorColumn() == 4, "left moves one character, which is two columns here");
    editor.handle(TuiKeyEvent{TuiKey::Backspace}, &submitted);
    check(editor.text() == QString::fromUtf8("时1"),
          "backspace removes a whole wide character");

    editor.handle(TuiKeyEvent{TuiKey::Home}, &submitted);
    check(editor.cursorColumn() == 0, "home goes to the start");
    editor.handle(TuiKeyEvent{TuiKey::End}, &submitted);
    check(editor.cursorColumn() == 3, "end goes to the end");

    editor.handle(TuiKeyEvent{TuiKey::Enter}, &submitted);
    check(submitted == QString::fromUtf8("时1"), "enter submits the line");
    check(editor.text().isEmpty(), "and clears the buffer");

    editor.handle(TuiKeyEvent{TuiKey::Up}, &submitted);
    check(editor.text() == QString::fromUtf8("时1"), "up recalls the previous line");

    // Deliberate: a stray key must never produce a wire effect. Cancelling an
    // interaction stays an explicit /cancel.
    editor.handle(TuiKeyEvent{TuiKey::Escape}, &submitted);
    check(editor.text().isEmpty() && submitted.isEmpty(),
          "escape clears the line and submits nothing");

    editor.setCompleter([](const QString &prefix, QStringList *matches) {
        if (QStringLiteral("/status").startsWith(prefix)) {
            *matches << QStringLiteral("/status");
            return QStringLiteral("/status");
        }
        return prefix;
    });
    type(editor, QStringLiteral("/sta"));
    editor.handle(TuiKeyEvent{TuiKey::Tab}, &submitted);
    check(editor.text() == QStringLiteral("/status"), "tab completes through the injected completer");

    editor.reset();
    for (int i = 0; i < 17000; ++i)
        type(editor, QStringLiteral("x"));
    check(editor.text().size() == 16384, "the line stops growing at the existing 16384 limit");

    // Item 4: handle(Right) and handle(Delete) are otherwise never called
    // above. A fresh editor keeps this independent of the state built up by
    // the assertions before it.
    TuiLineEditor motionEditor;
    type(motionEditor, QString::fromUtf8("时语1"));
    motionEditor.handle(TuiKeyEvent{TuiKey::Home}, &submitted);
    motionEditor.handle(TuiKeyEvent{TuiKey::Right}, &submitted);
    check(motionEditor.cursorColumn() == 2, "right moves one character forward, two columns here");
    motionEditor.handle(TuiKeyEvent{TuiKey::Delete}, &submitted);
    check(motionEditor.text() == QString::fromUtf8("时1"),
          "delete removes the whole wide character ahead of the cursor");
    motionEditor.handle(TuiKeyEvent{TuiKey::End}, &submitted);
    motionEditor.handle(TuiKeyEvent{TuiKey::Delete}, &submitted);
    check(motionEditor.text() == QString::fromUtf8("时1"), "delete at the end of the line is a no-op");

    // Item 3 (Important): cursor motion and deletion must move by whole code
    // points, not UTF-16 code units, so a surrogate pair (an astral
    // character such as U+1F600, outside the BMP) is never split. A real
    // decoded Char event carries the whole pair as one event's text -- the
    // type() helper above iterates QChar-by-QChar and would tear it apart
    // itself, so this goes through handle() directly instead.
    TuiLineEditor astralEditor;
    QString astralSubmitted;
    astralEditor.handle(TuiKeyEvent{TuiKey::Char, QString::fromUtf8("\xF0\x9F\x98\x80")},
        &astralSubmitted); // U+1F600, a surrogate pair in UTF-16
    check(astralEditor.text().size() == 2, "the astral character is inserted as one surrogate pair");
    astralEditor.handle(TuiKeyEvent{TuiKey::Left}, &astralSubmitted);
    check(astralEditor.cursorColumn() == 0,
          "left moves over the whole surrogate pair rather than landing inside it");
    astralEditor.handle(TuiKeyEvent{TuiKey::Right}, &astralSubmitted);
    astralEditor.handle(TuiKeyEvent{TuiKey::Backspace}, &astralSubmitted);
    check(astralEditor.text().isEmpty(),
          "backspace removes the whole surrogate pair, leaving no unpaired half behind");

    // Item 2 (Spec gap): Ctrl+U / Ctrl+K / Ctrl+W must actually edit the
    // line, not just decode to something.
    TuiLineEditor killEditor;
    QString killSubmitted;
    type(killEditor, QStringLiteral("hello world"));
    killEditor.handle(TuiKeyEvent{TuiKey::Left}, &killSubmitted); // cursor lands inside "world"
    killEditor.handle(TuiKeyEvent{TuiKey::KillToLineStart}, &killSubmitted);
    check(killEditor.text() == QStringLiteral("d"), "Ctrl+U erases from line start to the cursor");

    killEditor.reset();
    type(killEditor, QStringLiteral("hello world"));
    killEditor.handle(TuiKeyEvent{TuiKey::Home}, &killSubmitted);
    killEditor.handle(TuiKeyEvent{TuiKey::KillToLineEnd}, &killSubmitted);
    check(killEditor.text().isEmpty(), "Ctrl+K erases from the cursor to line end");

    killEditor.reset();
    type(killEditor, QStringLiteral("hello world"));
    killEditor.handle(TuiKeyEvent{TuiKey::KillPreviousWord}, &killSubmitted);
    check(killEditor.text() == QStringLiteral("hello "), "Ctrl+W erases the word before the cursor");

    // Ctrl+W with wide characters immediately before the cursor: the whole
    // run of non-space characters -- 时语 included -- comes out as one word.
    killEditor.reset();
    type(killEditor, QString::fromUtf8("时语foo"));
    killEditor.handle(TuiKeyEvent{TuiKey::KillPreviousWord}, &killSubmitted);
    check(killEditor.text().isEmpty(),
          "Ctrl+W erases a word that includes wide characters ahead of the cursor");

    std::printf("[AUTOTEST] TUI_LINE_EDITOR_RESULT status=%s\n",
        failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
