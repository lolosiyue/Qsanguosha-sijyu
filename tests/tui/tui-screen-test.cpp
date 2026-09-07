#include "tui-screen.h"

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

QString lineAt(const TuiScreen &screen, int row)
{
    return screen.toPlainText().split(QLatin1Char('\n')).value(row);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    TuiScreen screen;
    screen.resize(5, 20);
    check(screen.rows() == 5 && screen.cols() == 20, "the screen takes the size it was given");

    screen.putText(1, 2, QString::fromUtf8("曹操"));
    check(lineAt(screen, 1) == QString::fromUtf8("  曹操"),
          "a wide string starts where it was put and occupies four columns");

    // Clipping, not wrapping: a torn row is worse than a cut word.
    screen.putText(2, 18, QString::fromUtf8("张飞"));
    check(tuiDisplayWidth(lineAt(screen, 2)) <= 20,
          "text that would run past the right edge is clipped to the screen");

    screen.putText(3, 0, QString::fromUtf8("时语时语时语"), TuiAttr::Normal, 5);
    check(lineAt(screen, 3) == QString::fromUtf8("时语…"),
          "maxWidth elides through the same rule tuiElide uses");

    screen.drawBox(TuiRect{0, 0, 5, 20}, QString::fromUtf8("房间"));
    check(lineAt(screen, 0).startsWith(QString::fromUtf8("┌")),
          "a box draws its own corners");
    check(lineAt(screen, 0).contains(QString::fromUtf8("房间")),
          "a box carries its title on the top edge");
    check(lineAt(screen, 4).startsWith(QString::fromUtf8("└")), "and closes at the bottom");

    // The property that makes the board usable over a slow link.
    const QString first = screen.flush();
    check(!first.isEmpty(), "the first flush after a resize emits the whole screen");
    const QString second = screen.flush();
    check(second.isEmpty(), "flushing an unchanged screen emits nothing at all");

    screen.putText(1, 2, QString::fromUtf8("孙权"));
    const QString third = screen.flush();
    check(!third.isEmpty() && third.length() < first.length() / 2,
          "changing one cell emits far less than a full repaint");
    check(!third.contains(QString::fromUtf8("房间")),
          "an unchanged box title is not re-sent");

    screen.clear();
    check(lineAt(screen, 1).isEmpty(), "clear empties every cell");

    // C1 (2026-09 review): the full-repaint path used to append "\r\n" after
    // EVERY row, including the last one. On a terminal whose height exactly
    // equals the screen's row count that trailing separator scrolls the
    // alternate screen up by a line -- the top border is lost, and every
    // absolute-position escape the diff path emits afterwards addresses rows
    // against a screen that has silently shifted underneath it. This is the
    // one part of the whole suite that examines flush()'s actual byte
    // stream with geometry intact rather than toPlainText(), which strips
    // positioning and would never have caught this.
    {
        TuiScreen repaintScreen;
        constexpr int rows = 6;
        constexpr int cols = 10;
        repaintScreen.resize(rows, cols);
        for (int row = 0; row < rows; ++row)
            repaintScreen.putText(row, 0, QStringLiteral("row%1").arg(row));
        const QString frame = repaintScreen.flush();

        const int separatorCount = frame.count(QStringLiteral("\r\n"));
        check(separatorCount == rows - 1,
              "a full repaint emits exactly one row separator between each "
              "pair of rows (rows - 1 total), never trailing after the last");
        check(!frame.endsWith(QStringLiteral("\r\n")),
              "the full repaint's byte stream does not end with a row separator");
        // Belt and braces: the separator must not appear anywhere after the
        // last row's own content starts, i.e. splitting on it must produce
        // exactly `rows` chunks (one per row, nothing empty trailing).
        const QStringList chunks = frame.split(QStringLiteral("\r\n"));
        check(chunks.size() == rows,
              "splitting the full-repaint stream on the row separator yields "
              "exactly one chunk per row");
        check(!chunks.isEmpty() && !chunks.last().isEmpty(),
              "the last chunk (the bottom row) is real row content, not an "
              "empty trailing element left by a separator after it");
    }

    std::printf("[AUTOTEST] TUI_SCREEN_RESULT status=%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
