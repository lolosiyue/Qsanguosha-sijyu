#ifndef TUI_RESOLVERS_H
#define TUI_RESOLVERS_H

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

// Which players the engine would let a card be aimed at, asked before the
// player has picked anything -- Card::targetFilter() with an empty selection,
// which is how the desktop lights up its photos. Only the first target is
// knowable this way: on a multi-target card the rest depend on what came
// before, so the menu shows the opening move and the full list is checked when
// the answer is submitted.
struct TuiCardTargets
{
    bool known = false;
    bool targetFixed = false;
    QStringList targets;
    // How many times each of them may be named. Above one is the answer
    // Collateral and GreatYeyanCard give.
    QHash<QString, int> maxVotes;
};

// Everything a presenter needs the engine for. Each one is optional; an absent
// resolver means the raw value is shown. Both UI modes share one set: they ask
// the engine the same questions and differ only in how they draw the answers.
struct TuiResolvers
{
    std::function<QString(int)> card;
    std::function<QString(const QString &)> name;
    std::function<QString(const QString &)> player;
    std::function<QString(const QString &)> kingdom;
    std::function<QString(int)> cardHint;
    std::function<QString(const QString &)> playerHint;
    std::function<TuiCardTargets(int)> cardTargets;
    std::function<QString(int)> handHint;
    std::function<QString(const QString &, int)> skillHint;
};

#endif
