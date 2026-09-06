#ifndef TUI_COMMAND_H
#define TUI_COMMAND_H

#include <QString>
#include <QStringList>

enum class TuiCommandType
{
    Invalid,
    Help,
    Status,
    Players,
    Hand,
    Equipment,
    Piles,
    Skills,
    Log,
    Chat,
    Trust,
    AddRobot,
    Surrender,
    Reconnect,
    Quit,
    Cancel,
    // A local view command (docs/tui-board-ui.md §3.6): board mode's presenter
    // pages to it. Ignored outright in classic mode, where there is no page
    // to turn to; it must never reach TuiApplicationController's branches
    // that send an intent to the session.
    Board
};

enum class TuiTrustMode
{
    Toggle,
    Enable,
    Disable
};

struct TuiCommandIntent
{
    TuiCommandType type = TuiCommandType::Invalid;
    QString text;
    TuiTrustMode trustMode = TuiTrustMode::Toggle;
    bool fillRemaining = false;
    int count = 0;
    // TuiCommandType::Board only: the page the player typed, 1-based (as
    // shown by the room title's own "‹n/N›"). The controller converts to the
    // 0-based page TuiBoardPresenter::setPage() takes.
    int page = 0;
};

class TuiCommandParser
{
public:
    static bool parse(const QString &line, TuiCommandIntent *intent,
                      QString *error = nullptr);
};

struct TuiCompletion
{
    QString line;
    QStringList matches;
};

QStringList tuiCommandNames();
TuiCompletion completeTuiLine(const QString &line, const QStringList &extraTokens = {});

#endif
