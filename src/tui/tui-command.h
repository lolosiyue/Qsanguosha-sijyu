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
    Cancel
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
