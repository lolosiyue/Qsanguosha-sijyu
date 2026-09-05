#include "tui-text-width.h"

#include <QCoreApplication>
#include <QString>

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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    check(tuiDisplayWidth(QStringLiteral("abc")) == 3, "ASCII is one column each");
    check(tuiDisplayWidth(QString::fromUtf8("曹操")) == 4, "Han characters are two columns each");
    check(tuiDisplayWidth(QString::fromUtf8("时语sgs1")) == 8, "mixed text adds up");
    check(tuiDisplayWidth(QString::fromUtf8("【杀】")) == 6,
          "CJK brackets are full width, which is why they must not be counted as one");
    check(tuiDisplayWidth(QString::fromUtf8("♥♡")) == 2,
          "the heart glyphs the board draws hp with are narrow");
    check(tuiDisplayWidth(QString::fromUtf8("─│┌┤")) == 4, "box drawing is narrow");

    // A surrogate pair is one code point, not two.
    check(tuiDisplayWidth(QString::fromUtf8("\xF0\x9F\x80\x84")) == 2,
          "a non-BMP code point counts once");

    // The rule that keeps the grid from tearing.
    const QString name = QString::fromUtf8("张飞张飞");
    check(tuiDisplayWidth(tuiElide(name, 5)) <= 5, "elide never exceeds the budget");
    check(tuiElide(name, 5) == QString::fromUtf8("张飞…"),
          "elide drops a whole wide character rather than splitting one");
    check(tuiElide(name, 8) == name, "text that fits is returned untouched");
    check(tuiElide(name, 1) == QString::fromUtf8("…"), "a one column budget still yields the mark");
    check(tuiElide(name, 0).isEmpty(), "a zero budget yields nothing");

    check(tuiDisplayWidth(tuiPadTo(QString::fromUtf8("曹操"), 7)) == 7,
          "padding counts columns, not characters");
    check(tuiPadTo(QString::fromUtf8("曹操"), 7) == QString::fromUtf8("曹操   "),
          "padding is trailing spaces");
    check(tuiDisplayWidth(tuiPadTo(name, 5)) == 5, "padding an over-long string elides it first");

    std::printf("[AUTOTEST] TUI_TEXT_WIDTH_RESULT status=%s\n",
        failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
