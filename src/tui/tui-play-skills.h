#ifndef TUI_PLAY_SKILLS_H
#define TUI_PLAY_SKILLS_H

#include "card.h"
#include "interaction-model.h"
#include "structs.h"

#include <QList>
#include <QString>

class ClientGameState;

// The skill a server prompt names: "@@tuxi", "@rende", "@@guhuo3", "@@tuxi!".
// Empty when the pattern asks for cards rather than one skill's card. Same
// shape RoomScene matches before it enters a skill's pending mode.
QString tuiPatternSkillName(const QString &pattern, int *instanceId = nullptr);

// The reason a skill would be activated under for this prompt, or
// CARD_USE_REASON_UNKNOWN when the prompt offers no skills at all.
// handlingMethod is CardSelectionState's raw Card::HandlingMethod, -1 when the
// server did not say.
CardUseStruct::CardUseReason tuiSkillPromptReason(InteractionType type, int handlingMethod,
                                                  const QString &pattern);

// ViewAsSkill / SkillCard list for a prompt, numbered after the cards. A
// pattern that names one skill leaves only that skill. Whether a prompt gets
// a list at all is tuiSkillPromptReason()'s answer, not this one's.
void tuiFillSkillCandidates(const ClientGameState &state, const QString &pattern,
                            CardInteractionPayload *payload);

// Empty when the skill can be activated here, otherwise a short reason. Only
// ever a mark on the listing: a wrong hint must not become a wall.
QString tuiSkillActivationHint(const QString &skillName, int instanceId,
                               CardUseStruct::CardUseReason reason, const QString &pattern);

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
