// Every row of docs/tui-board-ui.md §6.1's decision table, evaluated top to
// bottom, gets a check here -- including the deliberate error row (a
// forcing condition together with an explicit --ui board), which is the one
// row a later reader might be tempted to "fix" into a silent downgrade.
#include "tui-ui-mode.h"

#include <QCoreApplication>
#include <QString>
#include <QTemporaryDir>

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

    // Isolates tuiSaveUiMode()/tuiSavedUiMode()'s QSettings file under a
    // throwaway directory instead of the real user config location --
    // otherwise running this suite would leave (or read) a stray
    // ui-mode.ini in whatever account happens to run the tests.
    QTemporaryDir configHome;
    check(configHome.isValid(), "a temp directory for QStandardPaths is created");
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());

    TuiUiModeInputs inputs;
    inputs.stdoutIsTty = true;
    inputs.stdinIsTty = true;

    // Row 1: a script always wins, whatever else was asked for.
    TuiUiModeInputs scripted = inputs;
    scripted.hasScript = true;
    check(tuiResolveUiMode(scripted).mode == TuiUiMode::Classic, "a script forces classic");
    check(!tuiResolveUiMode(scripted).askUser, "and never asks");

    // --ui classic under the same forcing condition is not a conflict --
    // only asking for board is.
    TuiUiModeInputs scriptedClassic = scripted;
    scriptedClassic.flag = QStringLiteral("classic");
    check(!tuiResolveUiMode(scriptedClassic).conflict,
          "--ui classic alongside --script is not a conflict");

    // Row 2: redirected stdout or stdin forces classic the same way.
    TuiUiModeInputs pipedOut = inputs;
    pipedOut.stdoutIsTty = false;
    check(tuiResolveUiMode(pipedOut).mode == TuiUiMode::Classic,
          "redirected output forces classic");
    check(!tuiResolveUiMode(pipedOut).askUser, "and never asks");

    TuiUiModeInputs pipedIn = inputs;
    pipedIn.stdinIsTty = false;
    check(tuiResolveUiMode(pipedIn).mode == TuiUiMode::Classic,
          "redirected input forces classic");

    // Row 3: --plain (standing in for --plain/--no-color/NO_COLOR, which
    // tui-main.cpp folds into this one bool before calling in) forces
    // classic too.
    TuiUiModeInputs plain = inputs;
    plain.plain = true;
    check(tuiResolveUiMode(plain).mode == TuiUiMode::Classic, "--plain forces classic");
    check(!tuiResolveUiMode(plain).askUser, "and never asks");

    // Row 4, the deliberate one: asking for board under a forcing condition
    // is an error, not a silent downgrade. Checked for all three forcing
    // conditions, each naming itself in the message.
    TuiUiModeInputs conflictingPlain = plain;
    conflictingPlain.flag = QStringLiteral("board");
    const TuiUiModeDecision plainConflict = tuiResolveUiMode(conflictingPlain);
    check(plainConflict.conflict, "--ui board with --plain is a conflict");
    check(!plainConflict.askUser, "a conflict is never also a question");
    check(plainConflict.conflictReason.contains(QStringLiteral("--plain")),
          "and the message names the flag that conflicts");

    TuiUiModeInputs conflictingScript = scripted;
    conflictingScript.flag = QStringLiteral("board");
    check(tuiResolveUiMode(conflictingScript).conflictReason.contains(QStringLiteral("--script")),
          "--ui board with --script names --script in the conflict message");

    TuiUiModeInputs conflictingPipe = pipedOut;
    conflictingPipe.flag = QStringLiteral("board");
    check(tuiResolveUiMode(conflictingPipe).conflict,
          "--ui board with redirected output is also a conflict");

    // Row 5: an explicit choice with nothing forcing it is followed exactly.
    TuiUiModeInputs explicitBoard = inputs;
    explicitBoard.flag = QStringLiteral("board");
    check(tuiResolveUiMode(explicitBoard).mode == TuiUiMode::Board, "--ui board is honoured");
    check(!tuiResolveUiMode(explicitBoard).askUser, "an explicit flag never asks");

    TuiUiModeInputs explicitClassic = inputs;
    explicitClassic.flag = QStringLiteral("classic");
    check(tuiResolveUiMode(explicitClassic).mode == TuiUiMode::Classic,
          "--ui classic is honoured");
    check(!tuiResolveUiMode(explicitClassic).askUser, "and never asks either");

    // Row 6: no --ui, but a saved choice from a previous run's question.
    TuiUiModeInputs remembered = inputs;
    remembered.savedChoice = QStringLiteral("board");
    check(tuiResolveUiMode(remembered).mode == TuiUiMode::Board, "a saved choice is used");
    check(!tuiResolveUiMode(remembered).askUser, "and is not asked again");

    TuiUiModeInputs rememberedClassic = inputs;
    rememberedClassic.savedChoice = QStringLiteral("classic");
    check(tuiResolveUiMode(rememberedClassic).mode == TuiUiMode::Classic,
          "a saved classic choice is used too");

    // Row 7: nothing forced, nothing saved -- ask once.
    check(tuiResolveUiMode(inputs).askUser,
          "a bare tty run with no saved choice asks once");
    check(!tuiResolveUiMode(inputs).conflict, "asking is not itself a conflict");

    // Not one of §6.1's own rows: an unrecognised --ui value is always a
    // usage error, regardless of the other fields.
    TuiUiModeInputs badFlag = inputs;
    badFlag.flag = QStringLiteral("fancy");
    check(tuiResolveUiMode(badFlag).conflict, "an unknown --ui value is a usage error");
    check(!tuiResolveUiMode(badFlag).askUser, "and does not also ask");

    // tuiSavedUiMode()/tuiSaveUiMode(): the QSettings round trip tui-main.cpp
    // relies on to remember an answer across runs.
    check(tuiSavedUiMode().isEmpty(), "nothing is remembered before the first save");
    tuiSaveUiMode(TuiUiMode::Board);
    check(tuiSavedUiMode() == QStringLiteral("board"), "a saved board choice reads back");
    tuiSaveUiMode(TuiUiMode::Classic);
    check(tuiSavedUiMode() == QStringLiteral("classic"),
          "saving again overwrites the previous choice");

    std::printf("[AUTOTEST] TUI_UI_MODE_RESULT status=%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
