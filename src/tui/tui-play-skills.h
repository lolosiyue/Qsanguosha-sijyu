#ifndef TUI_PLAY_SKILLS_H
#define TUI_PLAY_SKILLS_H

#include "interaction-model.h"

#include <QList>
#include <QString>

class Card;
class ClientGameState;

// Play-phase ViewAsSkill / SkillCard list, numbered after the hand.
void tuiFillPlaySkillCandidates(const ClientGameState &state, CardInteractionPayload *payload);

// viewAs / V2 createCard → Card::toString() for the wire.
//
// builtCard, when asked for, also hands back the card the text was made from.
// It stays valid for the rest of the current event handler and no longer: the
// card is already queued for deletion. Take it rather than parsing the wire
// string back -- Card::Parse() calls the full Card::deleteLater(), which drains
// the global card lifetime manager and reaps unrelated pending engine cards.
QString tuiResolveSkillCardWireText(const QString &selfName, const QString &skillName,
                                    int instanceId, const QList<int> &subcardIds,
                                    QString *error, const Card **builtCard = nullptr);

#endif
