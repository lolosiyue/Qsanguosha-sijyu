#include "tui-stream-presenter.h"

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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    QStringList out;
    QStringList err;
    TuiStreamPresenter presenter([&out](const QString &text) { out << text; },
                                 [&err](const QString &text) { err << text; });

    presenter.writeOutput(QStringLiteral("hello"));
    check(out.size() == 1 && out.at(0) == QStringLiteral("hello\n"),
          "stream presenter writes the text with a trailing newline and nothing else");
    check(err.isEmpty(), "output does not leak into the error sink");

    presenter.writeError(QStringLiteral("bad"));
    check(err.size() == 1 && err.at(0) == QStringLiteral("TUI_ERROR bad\n"),
          "errors keep the TUI_ERROR prefix the automation greps for");

    // The presenter must not sanitize: the controller already did, and doing it
    // twice would let a board presenter and a stream presenter disagree about
    // what reached --log-file.
    presenter.writeOutput(QStringLiteral("a\x1b[31mb"));
    check(out.size() == 2 && out.at(1) == QStringLiteral("a\x1b[31mb\n"),
          "the presenter passes text through untouched");

    presenter.shutdown();
    check(out.size() == 2 && err.size() == 1,
          "shutdown on the stream presenter writes nothing");

    std::printf("[AUTOTEST] TUI_PRESENTER_RESULT status=%s\n",
        failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
