// Every row of docs/tui-board-ui.md §6.1's decision table, evaluated top to
// bottom, gets a check here -- including the deliberate error row (a
// forcing condition together with an explicit --ui board), which is the one
// row a later reader might be tempted to "fix" into a silent downgrade.
#include "tui-ui-mode.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QStandardPaths>
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

    // Rows 1-3 all resolve to Classic on the automatic path, so their
    // relative order is invisible there -- it only shows up in which one
    // names itself when two apply at once and --ui board turns it into an
    // error. --script (row 1) must win over --plain (row 3).
    TuiUiModeInputs doubleForcing = inputs;
    doubleForcing.hasScript = true;
    doubleForcing.plain = true;
    doubleForcing.flag = QStringLiteral("board");
    const TuiUiModeDecision doubleForcingConflict = tuiResolveUiMode(doubleForcing);
    check(doubleForcingConflict.conflict,
          "two forcing conditions at once is still a conflict");
    check(doubleForcingConflict.conflictReason.contains(QStringLiteral("--script")),
          "the table's row order names --script (row 1) ahead of --plain (row 3)");
    check(!doubleForcingConflict.conflictReason.contains(QStringLiteral("--plain")),
          "and does not also name the lower-priority condition");

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

    // Row 5 must be checked *ahead of* row 6: an explicit flag has to beat
    // a saved choice even when both are set on the same call, not merely
    // when the other field happens to be empty (the two blocks above never
    // set both at once, so a swapped if/else order would still pass them).
    TuiUiModeInputs explicitOverridesSavedBoard = inputs;
    explicitOverridesSavedBoard.flag = QStringLiteral("classic");
    explicitOverridesSavedBoard.savedChoice = QStringLiteral("board");
    const TuiUiModeDecision explicitOverridesSavedBoardDecision =
        tuiResolveUiMode(explicitOverridesSavedBoard);
    check(explicitOverridesSavedBoardDecision.mode == TuiUiMode::Classic,
          "an explicit --ui classic overrides a saved board choice");
    check(!explicitOverridesSavedBoardDecision.askUser,
          "an explicit flag never asks, even with a saved choice present");

    TuiUiModeInputs explicitOverridesSavedClassic = inputs;
    explicitOverridesSavedClassic.flag = QStringLiteral("board");
    explicitOverridesSavedClassic.savedChoice = QStringLiteral("classic");
    const TuiUiModeDecision explicitOverridesSavedClassicDecision =
        tuiResolveUiMode(explicitOverridesSavedClassic);
    check(explicitOverridesSavedClassicDecision.mode == TuiUiMode::Board,
          "an explicit --ui board overrides a saved classic choice");
    check(!explicitOverridesSavedClassicDecision.askUser,
          "and never asks either");

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

    // tuiSavedUiMode() must not crash or propagate garbage when ui-mode.ini
    // exists but is not what it wrote: it should read back the same as "no
    // saved choice" (row 7 asks instead of trusting nonsense), not throw a
    // Qt warning through to the player or hand back the raw bytes. This
    // writes straight into the same XDG_CONFIG_HOME temp directory set up
    // above -- tuiUiModeSettingsPath() is private to tui-ui-mode.cpp, but it
    // is documented (tui-ui-mode.h) to sit at
    // QStandardPaths::AppConfigLocation + "/ui-mode.ini", so the test can
    // reach the same file without tui-ui-mode.cpp exposing a path override.
    {
        const QString settingsPath =
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/ui-mode.ini");
        check(QDir().mkpath(QFileInfo(settingsPath).absolutePath()),
              "the temp config directory can be created");
        QFile garbage(settingsPath);
        check(garbage.open(QIODevice::WriteOnly | QIODevice::Truncate),
              "a garbage ui-mode.ini can be written for the test");
        // Not just unparseable syntax -- also a recognised key with a value
        // that is neither "classic" nor "board", and a NUL byte partway
        // through a line, to touch both tuiSavedUiMode()'s own value check
        // and QSettings' own INI parser.
        static const char garbageBytes[] =
            "[General]\nui_mode=neither-classic-nor-board\n{{{not valid ini at all\x01\x02";
        garbage.write(garbageBytes, sizeof(garbageBytes) - 1);
        garbage.close();
        check(tuiSavedUiMode().isEmpty(),
              "a corrupt ui-mode.ini is treated as no saved choice, not a crash");
    }

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
