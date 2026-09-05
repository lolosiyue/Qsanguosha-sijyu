#ifndef TUI_TARGET_ADVICE_H
#define TUI_TARGET_ADVICE_H

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

class Card;
class Player;

// Object name -> the table's copy of that player, nullptr when the name is not
// one of ours. Everything below refuses to guess: an unknown name means no
// opinion at all, never a rejection.
using TuiPlayerLookup = std::function<const Player *(const QString &)>;
// Object name / card name -> what the player should read.
using TuiNameText = std::function<QString(const QString &)>;

// One step of "where can this card be aimed next", asked the way RoomScene asks
// it before every click: Card::targetFilter() against the targets already
// chosen.
//
// The four-argument overload is the one to ask, and only its maxVotes counts.
// Collateral::targetFilter() returns false unconditionally and speaks purely
// through maxVotes, and RoomScene::updateTargetsEnablity() reads it the same
// way -- it never looks at the bool. Cards that override only the four-argument
// form (Collateral, GreatYeyanCard, GreatJianjieYeyanCard) fall back to Card's
// own default through the three-argument one, which answers "one target, anyone
// but me" for every one of them.
struct TuiTargetStep
{
    bool known = false;   // there was a card and a Self to ask about
    bool fixed = false;   // the card picks its own targets; nothing to ask
    bool feasible = false; // targetsFeasible() for the chosen list as it stands
    QStringList candidates; // object names that can be the next target
    // Total votes each candidate may hold, already-chosen ones included. Above
    // one means the same player can be named again.
    QHash<QString, int> maxVotes;
};

// pool is the prompt's own target list: a player the server withheld never
// becomes a candidate just because the engine would allow it.
TuiTargetStep tuiTargetStep(const Card *card, const QStringList &chosen,
                            const QStringList &pool, const TuiPlayerLookup &lookup,
                            const Player *self);

// The verdict on a finished answer, worded for the player. Empty means send it.
//
// A target-fixed card is accepted without asking targetFilter() about anything:
// its targets are the server's (the dying player of an ask-for-peach, the
// target of a nullification), and a view-as skill's card commonly answers false
// for them. The desktop submits on targetFixed() || targetsFeasible() for the
// same reason.
//
// incomplete, when asked for, separates "this is wrong" from "this is not
// finished": it comes back true when every target named was legal in turn and
// only targetsFeasible() objected. A caller still collecting targets treats
// that as a reason to keep asking rather than a reason to refuse.
QString tuiValidateTargets(const Card *card, const QStringList &targets,
                           const TuiPlayerLookup &lookup, const Player *self,
                           const TuiNameText &cardName, const TuiNameText &playerName,
                           bool *incomplete = nullptr);

#endif
