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

    std::printf("[AUTOTEST] TUI_LINE_EDITOR_RESULT status=%s\n",
        failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
