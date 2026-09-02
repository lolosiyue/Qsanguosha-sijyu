#ifndef TUI_PLAY_SKILLS_H
#define TUI_PLAY_SKILLS_H

#include "interaction-model.h"

#include <QList>
#include <QString>

class ClientGameState;

// Play-phase ViewAsSkill / SkillCard list, numbered after the hand.
void tuiFillPlaySkillCandidates(const ClientGameState &state, CardInteractionPayload *payload);

// viewAs / V2 createCard → Card::toString() for the wire.
QString tuiResolveSkillCardWireText(const QString &selfName, const QString &skillName,
                                    int instanceId, const QList<int> &subcardIds,
                                    QString *error);

#endif
