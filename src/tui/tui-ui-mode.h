#ifndef TUI_UI_MODE_H
#define TUI_UI_MODE_H

#include <QString>

// Which of the two UI presenters this run installs.
enum class TuiUiMode { Classic, Board };

// Every fact tuiResolveUiMode() needs, gathered by tui-main.cpp so the
// decision stays a pure function: isatty() and QSettings only ever appear in
// the code that fills this struct in, never inside tui-ui-mode.cpp itself.
struct TuiUiModeInputs
{
    // "" (no --ui given), "classic" or "board" -- anything else is a usage
    // error, independent of every other field below.
    QString flag;
    bool hasScript = false;
    bool stdoutIsTty = false;
    bool stdinIsTty = false;
    // --plain, --no-color and NO_COLOR share the same mode-selection rule.
    // treats all three as one forcing condition and nothing downstream needs
    // to tell them apart, so the caller folds them into this one bool.
    bool plain = false;
    // tuiSavedUiMode()'s return value: "classic", "board", or "" when
    // nothing has been remembered yet.
    QString savedChoice;
};

struct TuiUiModeDecision
{
    TuiUiMode mode = TuiUiMode::Classic;
    // True only when no mode is forced and no choice has been saved; tui-main.cpp prompts before connecting.
    // row): tui-main.cpp must ask before connecting, and this is the only
    // case where it should.
    bool askUser = false;
    // True for an explicit board/forcing conflict or an unknown --ui value.
    // together with an explicit --ui board) and for an unrecognised --ui
    // value. tui-main.cpp turns either into exit code 2, printing
    // conflictReason.
    bool conflict = false;
    QString conflictReason;
};

// Pure implementation of the mode-selection table in docs/tui-board-ui.md section 6.1.
// wins. Pure: the same inputs produce the same decision every time, with no
// filesystem or terminal access of its own.
TuiUiModeDecision tuiResolveUiMode(const TuiUiModeInputs &inputs);

// The mode remembered from a previous run's startup question -- "classic",
// "board", or "" when nothing is saved -- and how to save one. Backed by
// QSettings under QStandardPaths::AppConfigLocation; this is the first use
// of QSettings anywhere in the TUI.
QString tuiSavedUiMode();
// Returns false when QSettings could not actually persist the choice (e.g.
// an unwritable config directory) -- the caller should tell the player
// rather than silently claiming the choice was remembered when the next run
// will just ask again.
bool tuiSaveUiMode(TuiUiMode mode);

#endif
