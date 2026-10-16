#ifndef CARD_USE_SERVICE_H
#define CARD_USE_SERVICE_H

#include "room.h"
#include "skill.h"
#include "structs.h"

#include <QList>
#include <QString>
#include <QVariant>

class Card;
class ServerPlayer;
class ViewAsSkillV2;

int getConvertedPhysicalCardId(const Card *card);

class CardUseService
{
public:
    explicit CardUseService(Room &room);

    void slashEffect(const SlashEffectStruct&effect);
    void slashResult(const SlashEffectStruct&effect, const Card*jink);
    bool useNullified(const Card*use_card);
    const Card*isCanceled(const CardEffectStruct&effect);
    CardUseStruct getUseStruct(const Card*card);
    void prepareTargetModSkillReveal(CardUseStruct &use) const;
    void planTargetModSkillReveal(CardUseStruct &use) const;
    bool showRequiredTargetModSkillsV2(const CardUseStruct &use);
    bool areCardTargetsLegal(const CardUseStruct &use) const;
    const Card *resolveActiveSkillRequest(ServerPlayer *player, const ViewAsSkillV2 *skill,
                                                 const ActiveSkillRequest &request) const;
    void notifyCardProvenance(const QString &kind, ServerPlayer *initiator, const Card *card,
                                    const SkillInstanceRef &sourceRef, const SkillInstanceRef &activationRef);
    bool useCard(const CardUseStruct&use, bool add_history);
    bool useCard(CardUseStruct&use, bool add_history);
    bool useCardFromSkillEffect(const CardUseStruct &use, const SkillContext &accepted, bool add_history);
    bool useCardFromSkillEffect(CardUseStruct &use, const SkillContext &accepted, bool add_history);
    bool physicalCardEffectContext(const Card *card, SkillContext &context) const;
    bool setPhysicalCardEffectSource(int cardId, const SkillContext &accepted);
    bool restorePhysicalCardEffectSource(int cardId, const QVariantMap &receipt);
    Room::SkillCardMaterialLocations skillCardMaterialLocations(const Card *card) const;
    bool skillEffectCardMaterialsValid(const Card *card,
        const Room::SkillCardMaterialLocations *admittedLocations = nullptr) const;
    bool resolveCardUse(CardUseStruct &use, bool add_history, const SkillContext *acceptedEffect);
    bool cardEffect(const Card*card, ServerPlayer*from, ServerPlayer*to, bool multiple);
    bool cardEffect(CardEffectStruct&effect);
    bool isJinkEffected(ServerPlayer*user, const Card*jink);
    QList<ServerPlayer *> getUseExtraTargets(const CardUseStruct &use,
                                              bool distanceLimit);
    void cancelTarget(CardUseStruct &use, ServerPlayer *player);

private:
    Room &m_room;
};

#endif
