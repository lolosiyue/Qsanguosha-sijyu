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
    bool known = false;      // the engine had an opinion at all
    bool targetFixed = false; // the card picks its own targets
    QStringList targets;      // object names that pass as a first target
    // How many times each of them may be named. Above one is the answer
    // Collateral and GreatYeyanCard give, and the only way the player can
    // tell that naming somebody twice is allowed.
    QHash<QString, int> maxVotes;
};

// Everything a presenter needs the engine for. Each one is optional; an absent
// resolver means the raw value is shown. Both UI modes share one set: they ask
// the engine the same questions and differ only in how they draw the answers.
struct TuiResolvers
{
    std::function<QString(int)> card;
    std::function<QString(const QString &)> name;
    // Object name -> the screen name the player picked. A prompt carries only
    // object names, and renderInteraction() has no ClientGameState to look them
    // up in, so the controller hands its own resolver over.
    std::function<QString(const QString &)> player;
    // General name -> its kingdom. The server never broadcasts the kingdom
    // property, so it is read off the general the way Player::getKingdom() does.
    std::function<QString(const QString &)> kingdom;
    // What the engine says about a candidate in the request being answered --
    // "不可用", "不符" and so on, already worded. Empty means nothing to add.
    // The renderer never acts on it: an advisory that turns out wrong must not
    // be able to hide a legal answer.
    std::function<QString(int)> cardHint;
    std::function<QString(const QString &)> playerHint;
    std::function<TuiCardTargets(int)> cardTargets;
    // /hand is read outside any request, so it asks the play-phase
    // question unconditionally rather than following the active prompt.
    std::function<QString(int)> handHint;
    // Same advisory, for an offered skill: name and activation instance in.
    std::function<QString(const QString &, int)> skillHint;
};

#endif
