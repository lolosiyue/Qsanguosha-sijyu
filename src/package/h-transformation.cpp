/********************************************************************
    Copyright (c) 2013-2015 - Mogara

    This file is part of QSanguosha-Hegemony.

    This game is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 3.0
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    See the LICENSE file for more details.

    Mogara
    *********************************************************************/

#include "h-transformation.h"
#include "h-formation.h"
#include "skill.h"
#include "client.h"
#include "engine.h"
#include "structs.h"
#include "gamerule.h"
#include "settings.h"
#include "roomthread.h"
#include "json.h"
#include "room.h"
#include "serverplayer.h"
#include "general.h"
#include "h-standard-tricks.h"
#include "h-strategic-advantage.h"
#include "util.h"
#include "skill-declaration.h"
#include <QScopeGuard>

//xunyou
class HZhiyu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HZhiyu() : TriggerSkillV2("heg_zhiyu")
    {
        events << Damaged;
    }

    virtual bool canPreshow() const
    {
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *xunyu, QVariant &) const override
    {
        if ((xunyu && xunyu->isAlive() && xunyu->hasSkill(objectName())))
            return TriggerList{{xunyu, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *xunyu = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (xunyu->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), xunyu);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *xunyu, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        Room *room = xunyu->getRoom();
        xunyu->drawCards(1, objectName());
        if (xunyu->isDead() || xunyu->isKongcheng()) return false;
        room->showAllCards(xunyu);
        bool same = true;
        bool isRed = xunyu->getHandcards().first()->isRed();
        foreach (const Card *card, xunyu->getHandcards()) {
            if (card->isRed() != isRed) {
                same = false;
                break;
            }
        }
        if (same && damage.from && damage.from->isAlive())
            room->askForDiscard(damage.from, objectName(), 1, 1);
        return false;
    }
};

HQiceCard::HQiceCard()
{
    setSkillName("heg_qice");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool HQiceCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Card *mutable_card = Sanguosha->cloneCard(getUserString());
    if (mutable_card) {
        mutable_card->addSubcards(subcards);
        mutable_card->setCanRecast(false);
        mutable_card->deleteLater();
    }
    if (!mutable_card) return false;
    if (targets.length() >= subcards.length() && !mutable_card->isKindOf("Collateral")) return false;

    if (mutable_card->isKindOf("HAllianceFeast")) {
        if (to_select->getRole() == "careerist") {
            if (subcards.length() < 2)
                return false;
        } else {
            QList<const Player *> _targets;
            foreach (const Player *p, Self->getAliveSiblings())
                if (p->isFriendWith(to_select) && !Self->isProhibited(p, mutable_card))
                    _targets << p;
            if (_targets.length() > subcards.length() - 1) return false;
        }
    }

    if (mutable_card->isKindOf("HFightTogether")) {

        QList<const Player *> _targets, all_players = Self->getAliveSiblings();
        all_players << Self;

        const QStringList bigKingdoms = Self->getBigKingdoms(mutable_card->objectName(), MaxCardsType::Normal);
        const bool selectedBig = to_select->hasShownOneGeneral() && bigKingdoms.contains(to_select->getSeemingKingdom());
        foreach (const Player *p, all_players) {
            if ((p->hasShownOneGeneral() && bigKingdoms.contains(p->getSeemingKingdom())) == selectedBig) {
                if (!Self->isProhibited(p, mutable_card))
                    _targets << p;
            }
        }
        if (_targets.length() > subcards.length()) return false;

    }

    return mutable_card && mutable_card->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, mutable_card, targets);
}

bool HQiceCard::targetFixed() const
{
    Card *mutable_card = Sanguosha->cloneCard(getUserString());
    if (mutable_card) {
        mutable_card->addSubcards(subcards);
        mutable_card->setCanRecast(false);
        mutable_card->deleteLater();
    }
    return mutable_card && mutable_card->targetFixed();
}

bool HQiceCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    Card *mutable_card = Sanguosha->cloneCard(getUserString());
    if (mutable_card) {
        mutable_card->addSubcards(subcards);
        mutable_card->setCanRecast(false);
        mutable_card->deleteLater();
    }
    if (!mutable_card) return false;
    if (mutable_card->isKindOf("Collateral")) {
        if (targets.length()/2 > subcards.length()) return false;
    } else {
        if (targets.length() > subcards.length()) return false;
    }
    return mutable_card && mutable_card->targetsFeasible(targets, Self);
}

void HQiceCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *source = card_use.from;
    QString c = getUserString();

    Card *use_card = Sanguosha->cloneCard(c);
    if (!use_card) return;
    use_card->setSkillName("heg_qice");
    use_card->addSubcards(subcards);
    use_card->setCanRecast(false);
    use_card->setShowSkill("heg_qice");

    if (use_card->isAvailable(source)) {

        CardUseStruct actualUse = card_use;
        actualUse.card = use_card;
        room->useCard(actualUse);

        if (source->getMark("qicetransformUsed") ==0 && source->canTransform()) {
            if (room->askForChoice(source, "transform_qice", "yes+no", QVariant(), QString(), "@transform-ask:::heg_qice") == "yes") {
                room->broadcastSkillInvoke("transform", source->isMale());
                room->addPlayerMark(source, "qicetransformUsed");
                room->transformDeputyGeneral(source);
            }
        }
    }
}

class HQice : public ViewAsSkillV2
{
public:
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->isKongcheng() && !player->hasUsed("HQiceCard");
    }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty() && declarationAllowed(request.initiator, request.userString);
    }

    HQice() : ViewAsSkillV2("heg_qice")
    {

    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        QString c = request.userString;
        if (c != "") {
            HQiceCard *card = new HQiceCard;
            card->addSubcards(request.initiator->getHandcards());
            card->setUserString(c);
            return card;
        } else
            return NULL;
    }


    bool declarationAllowed(const Player *Self, const QString &button_name) const
    {
        if (!Self || button_name.isEmpty()) return false;

        Card *card = Sanguosha->cloneCard(button_name, Card::NoSuit, 0);

        if (card == NULL) return false;
        card->deleteLater();
        if (!card->isNDTrick()) return false;

        card->addSubcards(Self->getHandcards());
        card->setSkillName("heg_qice");


        if (card->targetFixed()) {
            int x = 0;
            QList<const Player *> all, siblings = Self->getAliveSiblings();
            siblings.prepend(Self);
            foreach (const Player *p, siblings) {
                if (!Self->isProhibited(p, card))
                    all << p;
            }

            if (card->isKindOf("HAwaitExhausted")) {
                foreach (const Player *p, all) {
                    if (Self->isFriendWith(p))
                        x++;
                }
            } else if (card->isKindOf("HBurningCamps")) {
                QList<const Player *> players = Self->getNextAlive()->getFormation();
                foreach (const Player *p, players) {
                    if (all.contains(p))
                        x++;
                }
            }  else if (card->isKindOf("HImperialOrder")) {
                foreach (const Player *p, all) {
                    if (!p->hasShownOneGeneral())
                        x++;
                }
            } else if (card->getSubtype() == "aoe") {
                x= all.length();
                if (all.contains(Self)) x--;
            } else if (card->getSubtype() == "global_effect") {
                x = all.length();
            }

            if (x > Self->getHandcardNum()) return false;

        }

        return !Self->isCardLimited(card, Card::MethodUse) && card->isAvailable(Self);
    }
    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), false); }
    SkillDeclarationReason declarationReason(const Player *self, const QString &value, const Card *) const override
    {
        return declarationAllowed(self, value) ? SkillDeclarationReason::None : SkillDeclarationReason::CardUnavailable;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HQiceCard"; }

};

//bianhuanhou

class HWanwei : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HWanwei() : TriggerSkillV2("heg_wanwei")
    {
        events << BeforeCardsMoveBatch;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName()))) {
            int x = 0;
            QVariantList move_datas = data.toList();
            foreach (QVariant move_data, move_datas) {
                CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
                if (move.from == player && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
                        && ((move.reason.m_reason == CardMoveReason::S_REASON_DISMANTLE && move.reason.m_playerId != move.reason.m_targetId)
                        || (move.to && move.to != player && move.to_place == Player::PlaceHand
                        && move.reason.m_reason != CardMoveReason::S_REASON_GIVE))) {
                    for (int i = 0; i < move.card_ids.length(); ++i) {
                        if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip) {
                            x++;
                        }
                    }
                }
            }

            if (x > 0 && x < player->getCardCount(true))
                return TriggerList{{player, {objectName()}}};

        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        QVariantList move_datas = data.toList();
        QVariantList new_datas;

        QList<int> selected;

        foreach (QVariant move_data, move_datas) {
            CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
            if (move.from != player) {
                new_datas << move_data;
                continue;
            }
            ServerPlayer *target = NULL;
            QString prompt;

            if (move.reason.m_reason == CardMoveReason::S_REASON_DISMANTLE && move.reason.m_playerId != move.reason.m_targetId) {
                target = room->findPlayerByObjectName(move.reason.m_playerId, true);
                prompt = "@heg_wanwei-dismantle:";
            }

            if (move.reason.m_reason != CardMoveReason::S_REASON_GIVE && move.to && move.to != player && move.to_place == Player::PlaceHand) {
                target = (ServerPlayer *)move.to;
                prompt = "@heg_wanwei-extraction:";
            }
            if (target == NULL) {
                new_datas << move_data;
                continue;
            }
            prompt = prompt + target->objectName() + "::";

            QList<int> card_ids;
            for (int i = 0; i < move.card_ids.length(); ++i) {
                if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip) {
                    card_ids << i;
                }
            }

            if (card_ids.isEmpty()) {
                new_datas << move_data;
                continue;
            }

            int x = card_ids.length();

            prompt = prompt + QString::number(x);

            QStringList pattern;
            foreach (int id, selected) {
                pattern << QString("^%1").arg(id);
            }

            QList<int> ints = room->askForExchangeCards(player, "_wanwei", x, x, prompt, QString(), pattern.join("|"));

            if (ints.length() < x) {
                ints.clear();
                foreach (const Card *card, player->getCards("he")) {
                    if (ints.length() == x) break;
                    int id = card->getEffectiveId();
                    if (!selected.contains(id))
                        ints << id;
                }
            }
            selected << ints;

            for (int i = 0; i < ints.length(); ++i) {
                move.card_ids.replace(card_ids.at(i), ints.at(i));
                move.from_places.replace(card_ids.at(i), room->getCardPlace(ints.at(i)));
            }

            new_datas << QVariant::fromValue(move);
        }
        data = QVariant::fromValue(new_datas);

        return false;
    }
};

class HYuejian : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HYuejian() : TriggerSkillV2("heg_yuejian")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    virtual bool canPreshow() const
    {
        return true;
    }

    virtual TriggerList triggerable(TriggerEvent , Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() == Player::Discard) {
            QList<ServerPlayer *> huanghous = room->findPlayersBySkillName(objectName());
            TriggerList skill_list;
            foreach (ServerPlayer *huanghou, huanghous) {
                if (huanghou->isFriendWith(player)) {
                    bool can_invoke = true;
                    QStringList assignee_list = player->property("usecard_targets").toString().split("+");
                    foreach (ServerPlayer *to, room->getAllPlayers(true)) {
                        if (assignee_list.contains(to->objectName()) && !huanghou->isFriendWith(to)) {
                            can_invoke = false;
                            break;
                        }
                    }

                    if (can_invoke)
                        skill_list.insert(huanghou, QStringList(objectName()));
                }
            }
            return skill_list;
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room* room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *huanghou = ctx.owner;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        bool invoke = false;
        if (huanghou->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(huanghou, objectName());
        } else
            invoke = huanghou->askForSkillInvoke(this, QVariant::fromValue(player));

        if (invoke) {
            room->broadcastSkillInvoke(objectName(), huanghou);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        room->setPlayerFlag(player, "jianyue_keep");
        return false;
    }
};

class HYuejianMaxCards : public MaxCardsSkillV2
{
public:
    HYuejianMaxCards() : MaxCardsSkillV2("#heg_yuejian-maxcard")
    {
        setHolderSelector(CorrectSkill_System);
    }

    virtual int getFixed(const Player *target) const
    {
        if (target->hasFlag("jianyue_keep"))
            return target->getMaxHp();
        return -1;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &) const override { return CorrectSkillResult::noEffect(); }
    CorrectSkillResult getFixedValue(const CorrectSkillContext &c) const override
    {
        const int n = c.primary ? getFixed(c.primary) : -1;
        return n < 0 ? CorrectSkillResult::noEffect() : CorrectSkillResult::useAmount(n);
    }
};

//liguo
HXiongsuanCard::HXiongsuanCard()
{
    setSkillName("heg_xiongsuan");
    mute = true;
}

bool HXiongsuanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->isFriendWith(to_select);
}

void HXiongsuanCard::onUse(Room *room, CardUseStruct &card_use) const
{
    room->setPlayerMark(card_use.from, "@fierce", 0);
    room->broadcastSkillInvoke("heg_xiongsuan", card_use.from);
    room->doSuperLightbox("heg_lijueguosi", "heg_xiongsuan");

    Card::onUse(room, card_use);
}

void HXiongsuanCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    room->damage(DamageStruct("heg_xiongsuan", effect.from, effect.to));
    if (effect.from->isAlive()) {
        effect.from->drawCards(3);
        if (effect.to->isAlive()) {
            QStringList limited_skills;
            QList<const Skill *> skills = effect.to->getVisibleSkillList();
            foreach (const Skill *skill, skills) {
                if (skill->getFrequency() == Skill::Limited && !skill->getLimitMark().isEmpty() && effect.to->getMark(skill->getLimitMark()) == 0)
                    limited_skills.append(skill->objectName());
            }
            if (!limited_skills.isEmpty()) {
                QString skill_name = room->askForChoice(effect.from, "heg_xiongsuan", limited_skills.join("+"), QVariant(), QString(), "@heg_xiongsuan-reset::"+effect.to->objectName());
                effect.to->setTag("XiongsuanSkill", skill_name);
            }
        }
    }
}

class HXiongsuan : public ViewAsSkillV2
{
public:
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return player->getMark("@fierce") >= 1;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !Sanguosha || !candidate || candidate->getEffectiveId() < 0 || !request.selectedCardIds.isEmpty() || candidate->hasFlag("using")) return false;
        return !request.initiator->isJilei(candidate) && Sanguosha->matchExpPattern(".|.|.|hand", request.initiator, candidate);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.size() != 1) return false;
        // Replay selection to reject duplicate IDs and recheck the ordered constraints.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HXiongsuanCard"; }

    HXiongsuan() : ViewAsSkillV2("heg_xiongsuan", 1)
    {
        frequency = Limited;
        limit_mark = "@fierce";
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        HXiongsuanCard *card = new HXiongsuanCard;
        card->addSubcard(originalCard);
        card->setShowSkill(objectName());
        return card;
    }

};

class HXiongsuanReset : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HXiongsuanReset() : TriggerSkillV2("#heg_xiongsuan-reset")
    {
        events << EventPhaseChanging << EventPhaseStart;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::NotActive) {
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                p->removeTag("XiongsuanSkill");
            }
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive && player->isAlive()) {
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                QString skill_name = p->getTag("XiongsuanSkill").toString();
                const Skill *skill = Sanguosha->getSkill(skill_name);
                if (skill && !skill->getLimitMark().isEmpty() && p->getMark(skill->getLimitMark()) == 0)
                    return TriggerList{{player, {objectName()}}};
            }
        }
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            QString skill_name = p->getTag("XiongsuanSkill").toString();
            const Skill *skill = Sanguosha->getSkill(skill_name);
            if (skill && !skill->getLimitMark().isEmpty() && p->getMark(skill->getLimitMark()) == 0) {
                LogMessage log;
                log.type = "#XiongsuanReset";
                log.from = p;
                log.arg = skill_name;
                room->sendLog(log);
                room->setPlayerMark(p, skill->getLimitMark(), 1);
            }
        }
        return false;
    }
};



namespace {
bool isHuashenBundleGeneral(const QString &name)
{
    const General *general = Sanguosha->getGeneral(name);
    static const QSet<QString> packages = { "heg_standard", "heg_formation", "heg_momentum",
        "heg_transformation", "heg_power", "heg_manoeuvre", "heg_newsgs", "heg_mol", "heg_overseas", "heg_lord_ex" };
    return general && name.startsWith("heg_") && !name.startsWith("heg_lord_")
        && packages.contains(general->getPackage());
}
}

void HRefreshHuashenProjection(ServerPlayer *player)
{
    if (!player || player->getTag("heg_huashen_refreshing").toBool()) return;
    player->setTag("heg_huashen_refreshing", true);
    const auto refreshGuard = qScopeGuard([player] { player->removeTag("heg_huashen_refreshing"); });
    Room *room = player->getRoom();
    const QStringList souls = player->property("Huashens").toString().split("+", Qt::SkipEmptyParts);
    QMap<QString, QStringList> providers;
    for (const QString &name : souls) {
        const General *general = Sanguosha->getGeneral(name);
        if (!general || !isHuashenBundleGeneral(name)) continue;
        for (const Skill *skill : general->getVisibleSkillList()) {
            if (!skill || (skill->getFrequency(player) != Skill::Frequent && skill->getFrequency(player) != Skill::NotFrequent)
                || skill->relateToPlace(true) || skill->relateToPlace(false)
                || dynamic_cast<const HArraySummon *>(ViewAsSkill::parseViewAsSkill(skill))
                || skill->objectName() == "heg_huashen") continue;
            bool ownSource = false;
            for (const SkillInstance &owned : player->getSkillInstances()) {
                if (owned.skillName != skill->objectName()) continue;
                const SkillInstance *root = &owned;
                QSet<QString> visited;
                while (root->source == SourceAttached && root->parentRef.isValid()
                    && root->parentRef.ownerObjectName == player->objectName()) {
                    const QString key = root->parentRef.key.skillName + QString::number(root->parentRef.key.instanceID);
                    if (visited.contains(key)) break;
                    visited.insert(key);
                    const SkillInstance *parent = player->findSkillInstance(root->parentRef.key.skillName, root->parentRef.key.instanceID);
                    if (!parent) break;
                    root = parent;
                }
                if (root->skillName != "heg_huashen") { ownSource = true; break; }
            }
            if (!ownSource) providers[skill->objectName()] << name;
        }
    }
    QList<SkillInstanceRef> roots;
    for (const SkillInstance &instance : player->getSkillInstances())
        if (instance.skillName == "heg_huashen" && instance.source != SourceAttached)
            roots << SkillInstanceRef(player->objectName(), instance.key());
    // Keep each Huashen copy separate, and never detach a same-name acquired/native source.
    for (const SkillInstance &instance : player->getSkillInstances()) {
        if (instance.source != SourceAttached || instance.parentRef.key.skillName != "heg_huashen") continue;
        const SkillInstanceRef ref(player->objectName(), instance.key());
        if (!roots.contains(instance.parentRef) || !providers.contains(instance.skillName)) room->detachAttachedSkill(ref);
    }
    for (const SkillInstanceRef &root : roots) {
        for (auto it = providers.cbegin(); it != providers.cend(); ++it) {
            const SkillInstanceRef ref = room->attachSkillToPlayer(player, it.key(), root, false);
            if (!ref.isValid()) continue;
            player->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "heg_huashen_souls", it.value());
            player->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "legacy_activation_lifecycle", true);
            for (const Skill *related : Sanguosha->getRelatedSkills(it.key())) {
                const SkillInstanceRef child = room->attachSkillToPlayer(player, related->objectName(), ref, false);
                if (child.isValid()) player->setSkillInstanceStateValue(child.key.skillName, child.key.instanceID,
                    "legacy_activation_lifecycle", true);
            }
        }
    }
}

void HDropHuashenGeneral(ServerPlayer *player, const QString &name)
{
    if (!player) return;
    QStringList souls = player->property("Huashens").toString().split("+", Qt::SkipEmptyParts);
    if (!souls.removeOne(name)) return;
    Room *room = player->getRoom();
    room->handleUsedGeneral("-" + name);
    player->addProperty("Huashens");
    player->setProperty("Huashens", souls.join("+"));
    room->notifyProperty(player, player, "Huashens");
    LogMessage log;
    log.type = "#dropHuashenDetail"; log.from = player; log.arg = name;
    room->sendLog(log);
    JsonArray args;
    args << QSanProtocol::S_GAME_EVENT_HUASHEN << player->objectName() << souls.join("+");
    room->doNotify(player, QSanProtocol::S_COMMAND_LOG_EVENT, args);
    HRefreshHuashenProjection(player);
}

//zuoci
class HHuashen : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HHuashen() : TriggerSkillV2("heg_huashen")
    {
        events << EventPhaseStart;

    }

    virtual bool canPreshow() const
    {
        return true;
    }

    static void AcquireGenerals(ServerPlayer *zuoci, int n, QString reason)
    {
        Room *room = zuoci->getRoom();
        QStringList huashens;
        if (!zuoci->property("Huashens").toString().isEmpty())
            huashens = zuoci->property("Huashens").toString().split("+");
        QStringList acquired = GetAvailableGenerals(zuoci, n);
        if (acquired.isEmpty()) return;
        if (n > 2) {

            LogMessage log;
            log.type = "#VeiwHuashenDetail";
            log.from = zuoci;
            log.arg = acquired.join("\\, \\");
            room->doNotify(zuoci, QSanProtocol::S_COMMAND_LOG_SKILL, log.toVariant());

            LogMessage log2;
            log2.type = "#VeiwHuashen";
            log2.from = zuoci;
            log2.arg = QString::number(n);
            room->doBroadcastNotify(room->getOtherPlayers(zuoci), QSanProtocol::S_COMMAND_LOG_SKILL, log2.toVariant());

            QString general_name = room->askForGeneral(zuoci, acquired, QString(), false, reason, QVariant());

            acquired = general_name.split("+");
        }

        LogMessage log;
        log.type = "#GetHuashenDetail";
        log.from = zuoci;
        log.arg = acquired.join("\\, \\");
        room->doNotify(zuoci, QSanProtocol::S_COMMAND_LOG_SKILL, log.toVariant());

        LogMessage log2;
        log2.type = "#GetHuashen";
        log2.from = zuoci;
        log2.arg = QString::number(acquired.length());
        room->doBroadcastNotify(room->getOtherPlayers(zuoci), QSanProtocol::S_COMMAND_LOG_SKILL, log2.toVariant());

        QStringList hidden;
        for (int i = 0; i < acquired.length(); i++) hidden << "unknown";
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p == zuoci)
                room->doAnimate(QSanProtocol::S_ANIMATE_HUASHEN, zuoci->objectName(), acquired.join(":"), QList<ServerPlayer *>() << p);
            else
                room->doAnimate(QSanProtocol::S_ANIMATE_HUASHEN, zuoci->objectName(), hidden.join(":"), QList<ServerPlayer *>() << p);
        }


        foreach (QString name, acquired) {
            huashens << name;
            room->handleUsedGeneral(name);
        }
        zuoci->addProperty("Huashens");
        zuoci->setProperty("Huashens", huashens.join("+"));
        room->notifyProperty(zuoci, zuoci, "Huashens");
        HRefreshHuashenProjection(zuoci);
        JsonArray arg;
        arg << QSanProtocol::S_GAME_EVENT_HUASHEN << zuoci->objectName() << huashens.join("+");
        room->doNotify(zuoci, QSanProtocol::S_COMMAND_LOG_EVENT, arg);

    }

    static QStringList GetAvailableGenerals(ServerPlayer *zuoci, int n)
    {
        Room *room = zuoci->getRoom();
        QStringList available;
        foreach (QString name, Sanguosha->getLimitedGeneralNames())
            if (isHuashenBundleGeneral(name) && !room->getUsedGeneral().contains(name))
                available << name;

    qsanShuffle(available);
        if (available.isEmpty()) return QStringList();
        n = qMin(n, int(available.length()));

        return available.mid(0, n);
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return ((player && player->isAlive() && player->hasSkill(objectName())) && player->getPhase() == Player::Start) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *zuoci, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        Room *room = zuoci->getRoom();
        QStringList huashens;
        if (!zuoci->property("Huashens").toString().isEmpty())
            huashens = zuoci->property("Huashens").toString().split("+");
        if (huashens.length() < 2) {
            AcquireGenerals(zuoci, 5, objectName());
        } else {
            QString result = room->askForGeneral(zuoci, huashens, QString(), true, objectName(), QVariant());

            HDropHuashenGeneral(zuoci, result);

            AcquireGenerals(zuoci, 1, objectName());
        }
        return false;
    }
};

class HHuashenBorrow : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HHuashenBorrow() : TriggerSkillV2("#heg_huashen-borrow")
    { events << EventSkillInvoking << EventSkillEffectFinished << EventAcquireSkill << EventLoseSkill; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *eventPlayer, QVariant &data) const override
    {
        if (event == EventAcquireSkill || event == EventLoseSkill) {
            if (eventPlayer) HRefreshHuashenProjection(eventPlayer);
            return true;
        }
        SkillContext context = data.value<SkillContext>();
        const SkillInstanceRef ref = context.activationRef;
        ServerPlayer *player = room->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!player || !ref.isValid()) return true;
        if (event == EventSkillInvoking) {
            const SkillInstance *instance = player->findSkillInstance(ref.key.skillName, ref.key.instanceID);
            if (!instance || instance->source != SourceAttached || instance->parentRef.key.skillName != "heg_huashen") return true;
            if (room->getThread()->isLegacySkillActivationActive(ref)
                && !context.extra_data.toMap().value("legacy_activation").toBool()) return true;
            const QStringList suppliers = player->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
                "heg_huashen_souls").toStringList();
            if (suppliers.isEmpty()) return true;
            // Snapshot the accepted exact source: completion may outlive attachment removal.
            QVariantMap marker;
            marker.insert("skill", ref.key.skillName); marker.insert("instance", ref.key.instanceID);
            marker.insert("owner", ref.ownerObjectName);
            marker.insert("parent", QVariant::fromValue(instance->parentRef));
            marker.insert("souls", suppliers);
            context.interceptor_data.insert("heg_huashen", marker);
            data = QVariant::fromValue(context);
            return true;
        }
        const QVariantMap marker = context.interceptor_data.value("heg_huashen");
        if (marker.value("skill").toString() != ref.key.skillName || marker.value("instance").toInt() != ref.key.instanceID
            || marker.value("owner").toString() != ref.ownerObjectName
            || marker.value("parent").value<SkillInstanceRef>().key.skillName != "heg_huashen") return true;
        const QStringList suppliers = marker.value("souls").toStringList();
        QStringList current = player->property("Huashens").toString().split("+", Qt::SkipEmptyParts), choices;
        for (const QString &name : suppliers) if (current.contains(name)) choices << name;
        if (choices.isEmpty()) return true;
        const QString selected = choices.size() == 1 ? choices.first()
            : room->askForGeneral(player, choices, choices.first(), true, "heg_huashen");
        HDropHuashenGeneral(player, choices.contains(selected) ? selected : choices.first());
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *eventPlayer, QVariant &) const override
    { return {}; }
};

class HHuashenClear : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HHuashenClear() : TriggerSkillV2("#heg_huashen-clear")
    {
        events << EventLoseSkill;
        global = true;
        frequency = Compulsory;
    }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        // Clear only after the last source is retired, never when a sibling survives.
        SkillChangeStruct change;
        if (!player || !change.tryParse(data) || change.skillName != "heg_huashen"
            || change.instanceID <= 0 || player->ownsSkill("heg_huashen")
            || player->hasSkillInstance(change.skillName, change.instanceID)) return true;
        QStringList huashens;
        if (!player->property("Huashens").toString().isEmpty())
            huashens = player->property("Huashens").toString().split("+");
        foreach (QString name, huashens)
            room->handleUsedGeneral("-" + name);
        JsonArray arg;
        arg << QSanProtocol::S_GAME_EVENT_HUASHEN << player->objectName() << QString();
        room->doNotify(player, QSanProtocol::S_COMMAND_LOG_EVENT, arg);
        player->addProperty("Huashens");
        player->setProperty("Huashens", QString());
        room->notifyProperty(player, player, "Huashens");
        HRefreshHuashenProjection(player);
        return true;
    }
};

class HXinsheng : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HXinsheng() : TriggerSkillV2("heg_xinsheng")
    {
        events << Damaged;
        frequency = Frequent;
    }

    virtual bool canPreshow() const
    {
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *zuoci, QVariant &) const override
    {
        if ((zuoci && zuoci->isAlive() && zuoci->hasSkill(objectName())))
            return TriggerList{{zuoci, {objectName()}}};
        return TriggerList();
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *zuoci = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (zuoci->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), zuoci);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *zuoci, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        HHuashen::AcquireGenerals(zuoci, 1, objectName());
        return false;
    }
};

HYiguiCard::HYiguiCard()
{
    setSkillName("heg_yigui");
    will_throw = false;
}

bool HYiguiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    QString generalName = getUserString().section("+", 1, 1);

    const General *general = Sanguosha->getGeneral(generalName);

    if (general == NULL) return false;

    Card *mutable_card = Sanguosha->cloneCard(getUserString().section("+", 0, 0));
    if (mutable_card) {
        mutable_card->setSkillName("heg_yigui");
        mutable_card->addSubcards(subcards);
        mutable_card->setCanRecast(false);
        mutable_card->deleteLater();
        mutable_card->setTag("YiguiGeneral", generalName);
    }

    return mutable_card && mutable_card->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, mutable_card, targets);
}

bool HYiguiCard::targetFixed() const
{
    Card *mutable_card = Sanguosha->cloneCard(getUserString().section("+", 0, 0));
    if (mutable_card) {
        mutable_card->setSkillName("heg_yigui");
        mutable_card->addSubcards(subcards);
        mutable_card->setCanRecast(false);
        mutable_card->deleteLater();
    }
    return mutable_card && mutable_card->targetFixed();
}

bool HYiguiCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    Card *mutable_card = Sanguosha->cloneCard(getUserString().section("+", 0, 0));
    if (mutable_card) {
        mutable_card->setSkillName("heg_yigui");
        mutable_card->addSubcards(subcards);
        mutable_card->setCanRecast(false);
        mutable_card->deleteLater();
    }

    return mutable_card && mutable_card->targetsFeasible(targets, Self);
}

const Card *HYiguiCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer *source = card_use.from;
    Room *room = source->getRoom();

    QStringList str = toString().split(":").last().split("+");
    if (str.length() != 2) return NULL;
    QString card_name = str.first();
    QString soul_name = str.last();

    Card *use_card = Sanguosha->cloneCard(card_name, Card::NoSuit, 0);
    if (!use_card || !source->getGeneralPile("soul").contains(soul_name)) { delete use_card; return nullptr; }
    const QString usedType = use_card->isKindOf("Slash") ? "Slash" : use_card->getClassName();
    if (source->hasFlag("Yigui_" + usedType) || use_card->isKindOf("Jink") || use_card->isKindOf("Nullification")
        || (use_card->getTypeId() != Card::TypeBasic && !use_card->isNDTrick())) { delete use_card; return nullptr; }

    use_card->setSkillName("heg_yigui");
    use_card->setCanRecast(false);
    use_card->setShowSkill("heg_yigui");
    use_card->setTag("YiguiGeneral", soul_name);

    bool available = true;

    available = available && use_card->isAvailable(source);
    use_card->deleteLater();
    if (!available) return NULL;

    QString classname;
    if (use_card->isKindOf("Slash"))
        classname = "Slash";
    else
        classname = use_card->getClassName();

    room->setPlayerFlag(source, "Yigui_" + classname);

    LogMessage log;
    log.type = "#dropHuashenDetail";
    log.from = source;
    log.arg = soul_name;
    room->sendLog(log);

    source->removeGeneralFromPile("soul", soul_name);

    return use_card;
}

const Card *HYiguiCard::validateInResponse(ServerPlayer *user) const
{
    Room *room = user->getRoom();

    QStringList str = toString().split(":").last().split("+");
    if (str.length() != 2) return NULL;
    QString card_name = str.first();
    QString soul_name = str.last();

    Card *c = Sanguosha->cloneCard(card_name, Card::NoSuit, 0);
    if (!c || !user->getGeneralPile("soul").contains(soul_name)) { delete c; return nullptr; }
    const QString usedType = c->isKindOf("Slash") ? "Slash" : c->getClassName();
    if (user->hasFlag("Yigui_" + usedType) || c->isKindOf("Jink") || c->isKindOf("Nullification")
        || (c->getTypeId() != Card::TypeBasic && !c->isNDTrick())) { delete c; return nullptr; }


    c->setTag("YiguiGeneral", soul_name);

    QString classname;
    if (c->isKindOf("Slash"))
        classname = "Slash";
    else
        classname = c->getClassName();

    room->setPlayerFlag(user, "Yigui_" + classname);

    c->setSkillName("heg_yigui");
    c->deleteLater();

    LogMessage log;
    log.type = "#dropHuashenDetail";
    log.from = user;
    log.arg = soul_name;
    room->sendLog(log);

    user->removeGeneralFromPile("soul", soul_name);

    return c;

}

class HYiguiViewAsSkill : public ViewAsSkillV2
{
public:
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty() && accepts(request, request.userString);
    }

    HYiguiViewAsSkill() : ViewAsSkillV2("heg_yigui") { response_or_use = true; }
    SkillDialogInfo getDialogInfo() const override
    {
        auto info = SkillDialogInfo::named("heg_yigui", objectName());
        info.parameters.insert("customDeclaration", true);
        return info;
    }
    static bool accepts(const ActiveSkillRequest &request, const QString &value)
    {
        const Player *self = request.initiator;
        const QStringList pair = value.split("+");
        if (!self || pair.size() != 2 || !self->getGeneralPile("soul").contains(pair.last())) return false;
        const General *soul = Sanguosha->getGeneral(pair.last());
        Card *card = Sanguosha->cloneCard(pair.first());
        if (!card || !soul) { delete card; return false; }
        card->deleteLater();
        if (card->getTypeId() != Card::TypeBasic && !card->isNDTrick()) return false;
        if (card->isKindOf("Jink") || card->isKindOf("Nullification")) return false;
        card->setSkillName("heg_yigui");
        card->setTag("YiguiGeneral", pair.last());
        card->setCanRecast(false);
        const QString type = card->isKindOf("Slash") ? "Slash" : card->getClassName();
        if (self->hasFlag("Yigui_" + type) || self->isCardLimited(card, Card::MethodUse)) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return card->isAvailable(self);
        if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
        if (!Sanguosha->matchExpPattern(request.pattern, self, card)) return false;
        if (card->isKindOf("Peach") || card->isKindOf("Analeptic")) {
            const Player *target = self;
            for (const Player *p : self->getAliveSiblings())
                if (p->hasFlag("Global_Dying")) { target = p; break; }
            if (self->isProhibited(target, card)) return false;
        }
        return true;
    }
    QList<SkillDeclarationCandidate> declarationCandidates(const Player *self, CardUseStruct::CardUseReason reason,
        const QString &pattern, const QStringList &banned, quint64) const override
    {
        QList<SkillDeclarationCandidate> result;
        if (!self) return result;
        ActiveSkillRequest request;
        request.initiator = self; request.reason = reason; request.pattern = pattern;
        // Publish soul/card pairs through the shared declaration model used by every client.
        for (const QString &soul : self->getGeneralPile("soul")) {
            for (const QString &name : Sanguosha->getCardNames()) {
                Card *card = Sanguosha->cloneCard(name);
                if (!card) continue;
                const bool excluded = banned.contains(card->getPackage());
                delete card;
                if (excluded) continue;
                const QString value = name + "+" + soul;
                if (!accepts(request, value)) continue;
                SkillDeclarationCandidate candidate;
                candidate.value = value;
                candidate.label = Sanguosha->translate(soul) + ": " + Sanguosha->translate(name);
                candidate.kind = "choice"; candidate.group = soul; candidate.enabled = true;
                result << candidate;
            }
        }
        return result;
    }
    SkillDeclarationReason declarationReason(const Player *self, const QString &value, const Card *) const override
    {
        const QStringList pair = value.split("+");
        return self && pair.size() == 2 && self->getGeneralPile("soul").contains(pair.last())
            ? SkillDeclarationReason::None : SkillDeclarationReason::CandidateRemoved;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && !request.initiator->getGeneralPile("soul").isEmpty()
            && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new HYiguiCard;
        card->setUserString(request.userString);
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HYiguiCard"; }
};

class HYigui : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    SkillDialogInfo getDialogInfo() const override { return view_as_skill->getDialogInfo(); }
    SkillDeclarationReason declarationReason(const Player *self, const QString &value, const Card *card) const override
    { return view_as_skill->declarationReason(self, value, card); }
    QList<SkillDeclarationCandidate> declarationCandidates(const Player *self, CardUseStruct::CardUseReason reason,
        const QString &pattern, const QStringList &banned, quint64 requestId) const override
    { return view_as_skill->declarationCandidates(self, reason, pattern, banned, requestId); }

    HYigui() : TriggerSkillV2("heg_yigui")
    {
        view_as_skill = new HYiguiViewAsSkill;
    }

    static void AcquireGenerals(ServerPlayer *zuoci, int n, QString reason)
    {
        Room *room = zuoci->getRoom();

        QStringList acquired = GetAvailableGenerals(zuoci, n);
        if (acquired.isEmpty()) return;

        LogMessage log;
        log.type = "#GetHuashenDetail";
        log.from = zuoci;
        log.arg = acquired.join("\\, \\");
        room->doNotify(zuoci, QSanProtocol::S_COMMAND_LOG_SKILL, log.toVariant());

        LogMessage log2;
        log2.type = "#GetHuashen";
        log2.from = zuoci;
        log2.arg = QString::number(acquired.length());
        room->doBroadcastNotify(room->getOtherPlayers(zuoci), QSanProtocol::S_COMMAND_LOG_SKILL, log2.toVariant());

        QStringList hidden;
        for (int i = 0; i < acquired.length(); i++) hidden << "unknown";
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p == zuoci)
                room->doAnimate(QSanProtocol::S_ANIMATE_HUASHEN, zuoci->objectName(), acquired.join(":"), QList<ServerPlayer *>() << p);
            else
                room->doAnimate(QSanProtocol::S_ANIMATE_HUASHEN, zuoci->objectName(), hidden.join(":"), QList<ServerPlayer *>() << p);
        }

        zuoci->addGeneralToPile("soul", acquired, false);
    }

    static QStringList GetAvailableGenerals(ServerPlayer *zuoci, int n)
    {
        Room *room = zuoci->getRoom();
        QStringList available;
        foreach (QString name, Sanguosha->getLimitedGeneralNames())
            if (isHuashenBundleGeneral(name) && !room->getUsedGeneral().contains(name))
                available << name;

    qsanShuffle(available);
        if (available.isEmpty()) return QStringList();
        n = qMin(n, int(available.length()));

        return available.mid(0, n);
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *eventPlayer, QVariant &) const override
    {
        return TriggerList();
    }
};

class HYiguiClear : public TriggerSkillV2
{
public:
    HYiguiClear() : TriggerSkillV2("#heg_yigui-clear")
    {
        events << EventLoseSkill;
        global = true;
        frequency = Compulsory;
    }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    bool recordEvent(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        // Skill removal is public cleanup; a surviving source still owns its soul pile.
        SkillChangeStruct change;
        if (player && change.tryParse(data) && change.skillName == "heg_yigui"
            && change.instanceID > 0 && !player->ownsSkill("heg_yigui")
            && !player->hasSkillInstance(change.skillName, change.instanceID))
            player->clearOnePrivatePile("soul");
        return true;
    }
};

class HYiguiTurnClear : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HYiguiTurnClear() : TriggerSkillV2("#heg_yigui-turn-clear")
    { events << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        // The donor allowance resets after each turn, including another player's turn.
        for (ServerPlayer *player : room->getAllPlayers(true))
            for (const QString &flag : player->getFlags())
                if (flag.startsWith("Yigui_")) room->setPlayerFlag(player, "-" + flag);
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *eventPlayer, QVariant &) const override
    { return {}; }
};

class HYiguiShow : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HYiguiShow() : TriggerSkillV2("#heg_yigui-show")
    {
        events << GeneralShowed;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (player->cheakSkillLocation("heg_yigui", data) && player->getMark("yiguiUsed") == 0) {
            return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        room->sendCompulsoryTriggerLog(player, "heg_yigui");
        room->broadcastSkillInvoke("heg_yigui");
        room->addPlayerMark(player, "yiguiUsed");
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        HYigui::AcquireGenerals(player, 2, "heg_yigui");
        return false;
    }
};

class HYiguiProhibit : public ProhibitSkill
{
public:
    HYiguiProhibit() : ProhibitSkill("#heg_yigui-prohibit")
    {
    }

    virtual bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        if (card->getSkillName(true) == "heg_yigui" && to->hasShownOneGeneral()) {
            QString generalName = card->getTag("YiguiGeneral").toString();
            const General *general = Sanguosha->getGeneral(generalName);
            return (general && !general->getKingdoms().contains(to->getKingdom()));
        }
        return false;
    }
};

class HJihun : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HJihun() : TriggerSkillV2("heg_jihun")
    {
        events << QuitDying << Damaged;
    }

    virtual TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        TriggerList skill_list;
        if (triggerEvent == QuitDying && player->isAlive() && player->hasShownOneGeneral()) {
            QList<ServerPlayer *> zuocis = room->findPlayersBySkillName(objectName());
            foreach (ServerPlayer *zuoci, zuocis) {
                if (zuoci != NULL && !zuoci->isFriendWith(player) && zuoci->hasShownOneGeneral())
                    skill_list.insert(zuoci, QStringList(objectName()));
            }
            return skill_list;
        } else if (triggerEvent == Damaged && (player && player->isAlive() && player->hasSkill(objectName()))) {
            skill_list.insert(player, QStringList(objectName()));
        }
        return skill_list;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName());
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        ctx.manual_effect = true;
        HYigui::AcquireGenerals(player, 1, objectName());
        return false;
    }
};

//shamoke

class HJili : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HJili() : TriggerSkillV2("heg_jili")
    {
        events << CardUsed << CardResponded;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName()))) {
            const Card *card = NULL;
            if (triggerEvent == CardUsed)
                card = data.value<CardUseStruct>().card;
            else if (triggerEvent == CardResponded)
                card = data.value<CardResponseStruct>().m_card;
            if (card == NULL) return TriggerList();

            const int used = player->getRoom()->countHistoryCards(player);
            const int responded = player->getRoom()->countHistoryCards(player, "turn", QString(), true);
            if (used < 0 || responded < 0) return {};
            const int x = used + responded;
            if (card->getTypeId() != Card::TypeSkill && x == player->getAttackRange()) {
                return TriggerList{{player, {objectName()}}};
            }
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        player->drawCards(player->getAttackRange(), "heg_jili");
        return false;
    }
};

//masu
HSanyaoCard::HSanyaoCard()
{
    setSkillName("heg_sanyao");
}

bool HSanyaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty()) return false;
    QList<const Player *> players = Self->getAliveSiblings();
    players << Self;
    int max = -1000;
    foreach (const Player *p, players) {
        if (max < p->getHp())
            max = p->getHp();
    }
    return to_select->getHp() == max;
}

void HSanyaoCard::onEffect(CardEffectStruct &effect) const
{
    effect.from->getRoom()->damage(DamageStruct("heg_sanyao", effect.from, effect.to));
}

class HSanyao : public ViewAsSkillV2
{
public:
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->hasUsed("HSanyaoCard");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !Sanguosha || !candidate || candidate->getEffectiveId() < 0 || !request.selectedCardIds.isEmpty() || candidate->hasFlag("using")) return false;
        return !request.initiator->isJilei(candidate) && Sanguosha->matchExpPattern(".", request.initiator, candidate);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.size() != 1) return false;
        // Replay selection to reject duplicate IDs and recheck the ordered constraints.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HSanyaoCard"; }

    HSanyao() : ViewAsSkillV2("heg_sanyao", 1)
    {
    }


    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalcard = Sanguosha->getCard(request.selectedCardIds.first());
        HSanyaoCard *first = new HSanyaoCard;
        first->addSubcard(originalcard->getId());
        first->setSkillName(objectName());
        first->setShowSkill(objectName());
        return first;
    }
};

class HZhiman : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HZhiman() : TriggerSkillV2("heg_zhiman")
    {
        events << DamageCaused;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName()))) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.to && player != damage.to)
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        DamageStruct damage = data.value<DamageStruct>();
        player->setTag("zhiman_data", data);  // for AI
        bool invoke = player->askForSkillInvoke(this, QVariant::fromValue(damage.to));
        player->removeTag("zhiman_data");
        if (invoke) {
            room->broadcastSkillInvoke(objectName(), 1, player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *to = damage.to;
        LogMessage log;
        log.type = "#Zhiman";
        log.from = player;
        log.arg = objectName();
        log.to << to;
        room->sendLog(log);
        room->setPlayerMark(to, objectName(), 1);
        to->setTag("zhiman_from", QVariant::fromValue(player));
        return true;
    }
};

class HZhimanSecond : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HZhimanSecond() : TriggerSkillV2("heg_zhiman-second")
    {
        events << DamageComplete;
        global = true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (player && damage.to == player && player->getMark("heg_zhiman") > 0) {
            ServerPlayer *masu = player->getTag("zhiman_from").value<ServerPlayer *>();
            if (masu && damage.from == masu) {
                // This completes the accepted prevention, without inventing an owned helper.
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = masu;
                ctx.invoker = player;
                ctx.initiator = player;
                ctx.original_data = &data;
                ctx.current_event = event;
                ctx.amount = getBaseAmount();
                contexts << ctx;
            }
        }
        return true;
    }

    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.invoker || !ctx.original_data) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        return damage.to == ctx.invoker && damage.from == ctx.owner
            && ctx.invoker->getMark("heg_zhiman") > 0
            && ctx.invoker->getTag("zhiman_from").value<ServerPlayer *>() == ctx.owner;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *to = ctx.invoker;
        ServerPlayer *player = ctx.owner;
        ctx.manual_effect = true;
        to->removeTag("zhiman_from");
        room->setPlayerMark(to, "heg_zhiman", 0);
        if (player->canGet(to, "ej")) {
            int card_id = room->askForCardChosen(player, to, "ej", "heg_zhiman", false, Card::MethodGet);
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
            room->obtainCard(player, Sanguosha->getCard(card_id), reason, false);
        }
        if (to->isFriendWith(player) && to->canTransform() && player->getMark("zhimantransformUsed") == 0
                && (room->askForChoice(player, "heg_zhiman", "yes+no", QVariant(), QString(), "@heg_zhiman-ask::"+to->objectName()) == "yes")
                && (room->askForChoice(to, "transform_zhiman", "yes+no", QVariant(), QString(), "@transform-ask:::"+objectName()) == "yes")) {
            room->addPlayerMark(player, "zhimantransformUsed");
            room->broadcastSkillInvoke("transform", to->isMale());
            room->transformDeputyGeneral(to);
        }
        return false;
    }
};

//lingtong
class HXuanlue : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HXuanlue() : TriggerSkillV2("heg_xuanlue")
    {
        events << CardsMoveBatch;
        frequency = Frequent;
    }

    virtual bool canPreshow() const
    {
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *lingtong, QVariant &data) const override
    {
        if (!(lingtong && lingtong->isAlive() && lingtong->hasSkill(objectName()))) return TriggerList();
        QVariantList move_datas = data.toList();
        foreach (QVariant move_data, move_datas) {
            CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
            if (move.from == lingtong && move.from_places.contains(Player::PlaceEquip)) {
                QList<ServerPlayer *> other_players = room->getOtherPlayers(lingtong);
                foreach (ServerPlayer *p, other_players) {
                    if (lingtong->canDiscard(p, "he"))
                        return TriggerList{{lingtong, {objectName()}}};
                }
            }
        }

        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *lingtong = ctx.invoker;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QList<ServerPlayer *> other_players = room->getOtherPlayers(lingtong);
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, other_players) {
            if (lingtong->canDiscard(p, "he"))
                targets << p;
        }
        ServerPlayer *to = room->askForPlayerChosen(lingtong, targets, objectName(), "heg_xuanlue-invoke", true, true);
        if (to) {
            lingtong->setTag("xuanlue_target", QVariant::fromValue(to));
            room->broadcastSkillInvoke(objectName(), lingtong);
            return true;
        } else lingtong->removeTag("xuanlue_target");
        return false;

    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *lingtong = ctx.invoker;
        ctx.manual_effect = true;
        ServerPlayer *to = lingtong->getTag("xuanlue_target").value<ServerPlayer *>();
        lingtong->removeTag("xuanlue_target");
        if (to && lingtong->canDiscard(to, "he")) {
            int card_id = room->askForCardChosen(lingtong, to, "he", objectName(), false, Card::MethodDiscard);
            CardMoveReason reason = CardMoveReason(CardMoveReason::S_REASON_DISMANTLE, lingtong->objectName(), to->objectName(), objectName(), NULL);
            room->throwCard(Sanguosha->getCard(card_id), reason, to, lingtong);
        }
        return false;
    }
};

HYongjinCard::HYongjinCard()
{
    setSkillName("heg_yongjin");
    target_fixed = true;
}

void HYongjinCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *lingtong = card_use.from;

    QVariant data = QVariant::fromValue(card_use);
    RoomThread *thread = room->getThread();

    thread->trigger(PreCardUsed, room, lingtong, data);

    LogMessage log;
    log.from = lingtong;
    log.to << card_use.to;
    log.type = "#UseCard";
    log.card_str = toString();
    room->sendLog(log);

    room->removePlayerMark(lingtong, "@brave");
    room->doSuperLightbox("heg_lingtong", "heg_yongjin");

    if (lingtong->ownSkill("heg_yongjin") && !lingtong->hasShownSkill("heg_yongjin"))
        lingtong->showGeneral(lingtong->inHeadSkills("heg_yongjin"));

    thread->trigger(CardUsed, room, lingtong, data);
    thread->trigger(CardFinished, room, lingtong, data);
}

void HYongjinCard::use(Room *room, ServerPlayer *lingtong, QList<ServerPlayer *> &) const
{
    for (int i = 0; i < 3; i++) {
        if (!room->askForQiaobian(lingtong, room->getAlivePlayers(), "heg_yongjin", "@heg_yongjin-next", true, false))
            break;
    }
}

class HYongjin : public ViewAsSkillV2
{
public:
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return player->getMark("@brave") >= 1;
    }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HYongjinCard"; }

    HYongjin() : ViewAsSkillV2("heg_yongjin")
    {
        frequency = Limited;
        limit_mark = "@brave";
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HYongjinCard *card = new HYongjinCard;
        card->setShowSkill(objectName());
        return card;
    }

};

//lvfan

class HDiaodu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HDiaodu() : TriggerSkillV2("heg_diaodu")
    {
        events << EventPhaseStart << CardUsed;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Play) {
            if ((player && player->isAlive() && player->hasSkill(objectName()))) {
                bool can_invoke = false;
                QList<ServerPlayer *> all_players = room->getAlivePlayers();
                foreach (ServerPlayer *p, all_players) {
                    if (player->isFriendWith(p) && player->canGet(p, "e")) {
                        can_invoke = true;
                        break;
                    }
                }
                if (can_invoke)
                    return TriggerList{{player, {objectName()}}};
            }
        } else if (triggerEvent == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if ((player && player->isAlive() && player->hasSkill(objectName())) && use.card->getTypeId() == Card::TypeEquip)
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (triggerEvent == EventPhaseStart) {
            QList<ServerPlayer *> targets;
            QList<ServerPlayer *> all_players = room->getAlivePlayers();
            foreach (ServerPlayer *p, all_players) {
                if (player->isFriendWith(p) && player->canGet(p, "e"))
                    targets << p;
            }
            ServerPlayer *victim;
            if ((victim = room->askForPlayerChosen(player, targets, objectName(), "@heg_diaodu", true, true)) != NULL) {
                room->broadcastSkillInvoke(objectName(), player);

                QStringList target_list = player->getTag("diaodu_target").toStringList();
                target_list.append(victim->objectName());
                player->setTag("diaodu_target", target_list);

                return true;
            }
        } else if (triggerEvent == CardUsed) {
            if (player->askForSkillInvoke(objectName())) {
                room->broadcastSkillInvoke(objectName(), player);
                return true;
            }
        }
        return false;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        if (triggerEvent == EventPhaseStart) {
            QStringList target_list = player->getTag("diaodu_target").toStringList();
            QString target_name = target_list.last();
            target_list.removeLast();
            player->setTag("diaodu_target", target_list);

            ServerPlayer *target = room->findPlayerByObjectName(target_name);
            if (target != NULL) {
                int card_id = room->askForCardChosen(player, target, "e", objectName(), false, Card::MethodGet);
                const Card *card = Sanguosha->getCard(card_id);

                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
                room->obtainCard(player, card, reason, false);

                if (room->getCardOwner(card_id) == player && room->getCardPlace(card_id) == Player::PlaceHand) {

                    QList<ServerPlayer *> targets = room->getOtherPlayers(player);
                    targets.removeOne(target);

                    ServerPlayer *victim = room->askForPlayerChosen(player, targets, "diaodu_give",
                                                                    "@heg_diaodu-give:::" + card->objectName(), (target != player));
                    if (victim != NULL) {
                        CardMoveReason reason2(CardMoveReason::S_REASON_GIVE, player->objectName(), victim->objectName(), "heg_diaodu", QString());
                        room->obtainCard(victim, card, reason2, true);
                    }

                }
            }
        } else if (triggerEvent == CardUsed)
            player->drawCards(1, objectName());

        return false;
    }
};

class HDiaoduDraw : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HDiaoduDraw() : TriggerSkillV2("#heg_diaodu-draw")
    {
        events << CardUsed;
        frequency = Compulsory;
    }

    virtual TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player != NULL && player->isAlive()) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->getTypeId() == Card::TypeEquip) {
                QList<ServerPlayer *> owners = room->findPlayersBySkillName("heg_diaodu");
                TriggerList skill_list;
                foreach (ServerPlayer *owner, owners)
                    if (owner != player && player->isFriendWith(owner) && owner->hasShownSkill("heg_diaodu"))
                        skill_list.insert(owner, QStringList(objectName()));
                return skill_list;
            }
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *owner = ctx.owner;
        QVariant &data = *ctx.original_data;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (room->askForChoice(player, "heg_diaodu", "yes+no", data, QString(), "@heg_diaodu-draw:" + owner->objectName()) == "yes") {
            LogMessage log;
            log.type = "#InvokeOthersSkill";
            log.from = player;
            log.to << owner;
            log.arg = "heg_diaodu";
            room->sendLog(log);
            room->broadcastSkillInvoke("heg_diaodu", owner);
            room->notifySkillInvoked(owner, "heg_diaodu");

            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        player->drawCards(1, "heg_diaodu");
        return false;
    }
};



class HDiancai : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HDiancai() : TriggerSkillV2("heg_diancai")
    {
        events << EventPhaseEnd;
    }

    virtual bool canPreshow() const
    {
        return true;
    }

    virtual TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (!(triggerEvent == EventPhaseEnd && player->getPhase() == Player::Play)) return TriggerList();
        QList<ServerPlayer *> players = room->findPlayersBySkillName(objectName());
        TriggerList skill_list;
        foreach (ServerPlayer *p, players) {
            if ((p && p->isAlive() && p->hasSkill(objectName())) && p != player) {
                // Count committed departures from hand/equipment in this Play
                // phase; transfers between the same owner's hand/equip do not count.
                QVariantMap query{{"phase_id", room->historyScopes().value("phase_id")},
                                  {"from", p->objectName()}};
                int lost = 0;
                bool complete = true;
                for (;;) {
                    const QVariantMap page = room->queryHistoryMoves(query);
                    complete = complete && page.value("complete").toBool();
                    query.insert("watermark", page.value("watermark"));
                    for (const QVariant &entry : page.value("items").toList()) {
                        const QVariantMap move = entry.toMap().value("data").toMap();
                        const int from = move.value("from_place").toInt();
                        const int to = move.value("to_place").toInt();
                        if ((from == Player::PlaceHand || from == Player::PlaceEquip)
                            && !(move.value("to").toString() == p->objectName()
                                 && (to == Player::PlaceHand || to == Player::PlaceEquip))) ++lost;
                    }
                    if (!page.value("has_more").toBool()) break;
                    query.insert("after", page.value("next_after"));
                }
                if (complete && lost >= qMax(p->getHp(), 1))
                    skill_list.insert(p, QStringList(objectName()));
            }
        }
        return skill_list;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *ask_who = ctx.owner;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (ask_who->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), ask_who);
            return true;
        }
        return false;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *ask_who = ctx.owner;
        ctx.manual_effect = true;
        if (ask_who->getHandcardNum() < ask_who->getMaxHp())
            ask_who->drawCards(ask_who->getMaxHp() - ask_who->getHandcardNum(), objectName());

        if (ask_who->canTransform() && ask_who->getMark("diancaitransformUsed") == 0
                && room->askForChoice(ask_who, "transform_diancai", "yes+no", QVariant(), QString(), "@transform-ask:::"+objectName()) == "yes") {
            room->addPlayerMark(ask_who, "diancaitransformUsed");
            room->broadcastSkillInvoke("transform", ask_who->isMale());
            room->transformDeputyGeneral(ask_who);
        }
        return false;
    }
};

//lord_sunquan
HLianziCard::HLianziCard()
{
    setSkillName("heg_lianzi");
    target_fixed = true;
    will_throw = true;
}

void HLianziCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    int x = source->getPile("flame_map").length();
    foreach (ServerPlayer *p, room->getAllPlayers()) {
        if (p->getSeemingKingdom() == "wu")
            x = x + p->getEquips().length();
    }

    QList<int> ids = room->getNCards(x);
    if (x == 0) return;

    CardsMoveStruct move(ids, source, Player::PlaceTable,
        CardMoveReason(CardMoveReason::S_REASON_TURNOVER, source->objectName(), "heg_lianzi", QString()));
    room->moveCardsAtomic(move, true);

    room->getThread()->delay();
    room->getThread()->delay();

    Card::CardType type = Sanguosha->getCard(this->getEffectiveId())->getTypeId();

    QList<int> card_to_throw;
    QList<int> card_to_gotback;
    for (int i = 0; i < x; i++) {
        if (Sanguosha->getCard(ids[i])->getTypeId() == type)
            card_to_gotback << ids[i];
        else
            card_to_throw << ids[i];
    }
    if (!card_to_gotback.isEmpty()) {
        DummyCard dummy2(card_to_gotback);
        CardMoveReason reason(CardMoveReason::S_REASON_GOTBACK, source->objectName());
        room->obtainCard(source, &dummy2, reason);
    }
    if (!card_to_throw.isEmpty()) {
        DummyCard dummy(card_to_throw);
        CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, source->objectName(), "heg_lianzi", QString());
        room->throwCard(&dummy, reason, NULL);
    }
    if (card_to_gotback.length() > 3)
        room->handleAcquireDetachSkills(source, "-heg_lianzi|heg_zhiheng");
}

class HLianzi : public ViewAsSkillV2
{
public:
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->hasUsed("HLianziCard");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !Sanguosha || !candidate || candidate->getEffectiveId() < 0 || !request.selectedCardIds.isEmpty() || candidate->hasFlag("using")) return false;
        return !request.initiator->isJilei(candidate) && Sanguosha->matchExpPattern(".|.|.|hand", request.initiator, candidate);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.size() != 1) return false;
        // Replay selection to reject duplicate IDs and recheck the ordered constraints.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HLianziCard"; }

    HLianzi() : ViewAsSkillV2("heg_lianzi", 1)
    {
    }


    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalcard = Sanguosha->getCard(request.selectedCardIds.first());
        HLianziCard *first = new HLianziCard;
        first->addSubcard(originalcard->getId());
        first->setSkillName(objectName());
        first->setShowSkill(objectName());
        return first;
    }
};

class HJubao : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HJubao() : TriggerSkillV2("heg_jubao")
    {
        events << EventPhaseStart << BeforeCardsMoveBatch;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Finish) {
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (p->getTreasure() && p->getTreasure()->isKindOf("HLuminousPearl"))
                    return TriggerList{{player, {objectName()}}};
            }
            foreach (int id, room->getDiscardPile()) {
                if (Sanguosha->getCard(id)->isKindOf("HLuminousPearl"))
                    return TriggerList{{player, {objectName()}}};
            }
        } else if (triggerEvent == BeforeCardsMoveBatch && player->getTreasure()) {
            int treasure_id = player->getTreasure()->getEffectiveId();
            QVariantList move_datas = data.toList();
            foreach (QVariant move_data, move_datas) {
                CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
                if (move.from != player) continue;
                if (move.to && move.to != move.from && move.to_place == Player::PlaceHand
                     && move.reason.m_reason != CardMoveReason::S_REASON_GIVE) {
                    foreach (int id, move.card_ids) {
                        if (treasure_id == id)
                            return TriggerList{{player, {objectName()}}};
                    }
                }
            }
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else
            invoke = player->askForSkillInvoke(this, data);

        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        if (triggerEvent == EventPhaseStart) {
            player->drawCards(1, objectName());
            QList<CardsMoveStruct> moves;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (p->getTreasure() && p->getTreasure()->isKindOf("HLuminousPearl") && player->canGet(p, "he")) {
                    int card_id = room->askForCardChosen(player, p, "he", objectName(), false, Card::MethodGet);
                    CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
                    CardsMoveStruct move(card_id, player, Player::PlaceHand, reason);
                    moves.append(move);
                }
            }
            if (!moves.isEmpty())
                room->moveCardsAtomic(moves, false);
        } else if (player->getTreasure()) {
            int id = player->getTreasure()->getEffectiveId();
            data = room->changeMoveData(data, QList<int>() << id);
        }
        return false;
    }
};

HFlameMapCard::HFlameMapCard()
{
    setSkillName("heg_flamemap");
    target_fixed = true;
}

void HFlameMapCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *source = card_use.from;
    ServerPlayer *sunquan = room->getLord(source->getSeemingKingdom());
    LogMessage log;
    log.type = "#InvokeOthersSkill";
    log.from = source;
    log.to << sunquan;
    log.arg = "heg_flamemap";
    room->sendLog(log);
    room->notifySkillInvoked(source, "heg_flamemap");
    room->broadcastSkillInvoke("heg_flamemap", qsanRandomBounded(2) + 1, sunquan);
    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, source->objectName(), sunquan->objectName());
    room->setCardFlag((subcards.first()), "flame_map");
    sunquan->addToPile("flame_map", subcards, true, room->getAllPlayers(), CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, source->objectName()));
}

class HFlameMapVS : public ViewAsSkillV2
{
public:
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        const Player *sunquan = player->getLord();
        if (!sunquan || !sunquan->hasLordSkill("heg_jiahe") || !player->isFriendWith(sunquan))
            return false;
        return !player->hasUsed("HFlameMapCard") && (player->hasShownOneGeneral() || player->canShowGeneral("h") || player->canShowGeneral("d"));
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !Sanguosha || !candidate || candidate->getEffectiveId() < 0 || !request.selectedCardIds.isEmpty() || candidate->hasFlag("using")) return false;
        return Sanguosha->matchExpPattern("EquipCard", request.initiator, candidate);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.size() != 1) return false;
        // Replay selection to reject duplicate IDs and recheck the ordered constraints.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HFlameMapCard"; }

    HFlameMapVS() : ViewAsSkillV2("heg_flamemap", 1)
    {
        attached_lord_skill = true;
    }


    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        HFlameMapCard *slash = new HFlameMapCard;
        slash->addSubcard(originalCard);
        slash->setShowSkill("showforviewhas");
        return slash;
    }
};

class HFlameMap : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HFlameMap() : TriggerSkillV2("heg_flamemap")
    {
        events << Damaged << EventPhaseStart << EventPhaseChanging;
        view_as_skill = new HFlameMapVS;
        attached_lord_skill = true;

    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive) {
                QStringList skills = player->getTag("FlamemapSkills").toStringList();
                QStringList detachList;
                foreach(QString skill_name, skills)
                    detachList.append("-" + skill_name);
                room->handleAcquireDetachSkills(player, detachList, true);
                player->setTag("FlamemapSkills", QVariant());
            }
        }
        return true;
    }

    virtual TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        TriggerList skill_list;
        if (player == NULL || player->isDead()) return skill_list;
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Start) {
            QList<ServerPlayer *> sunquans = room->findPlayersBySkillName(objectName());
            foreach (ServerPlayer *sunquan, sunquans) {
                if (sunquan->hasShownSkill("heg_jiahe") && !sunquan->getPile("flame_map").isEmpty() && sunquan->isFriendWith(player))
                    skill_list.insert(sunquan, QStringList(objectName()));
            }
        } else if (triggerEvent == Damaged && player->hasSkill("heg_jiahe") && !player->getPile("flame_map").isEmpty()) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card != NULL)
                skill_list.insert(player, QStringList(objectName()));
        }
        return skill_list;
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (triggerEvent == EventPhaseStart)
            return true;
        else {
            room->sendCompulsoryTriggerLog(player, objectName());
            room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) + 3, player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *sunquan = ctx.owner;
        ctx.manual_effect = true;
        if (triggerEvent == EventPhaseStart) {
            int n = sunquan->getPile("flame_map").length();
            QStringList skill_list;
            if (n > 0)
                skill_list << "heg_yingzi_flamemap";
            if (n > 1)
                skill_list << "heg_haoshi_flamemap";
            if (n > 2)
                skill_list << "heg_shelie";
            if (n > 3)
                skill_list << "heg_duoshi_flamemap";
            QString all_choices = "heg_yingzi_flamemap+heg_haoshi_flamemap+heg_shelie+heg_duoshi_flamemap+cancel";
            if (!skill_list.isEmpty()) {
                skill_list << "cancel";
                QString skill1 = room->askForChoice(player, objectName(), skill_list.join("+"), QVariant(), "@heg_flamemap-choose", all_choices);
                if (skill1 == "cancel") return false;
                QStringList acquired_skills;
                acquired_skills << skill1 + "!";
                skill_list.removeOne(skill1);
                if (n > 4) {
                    QString skill2 = room->askForChoice(player, objectName(), skill_list.join("+"), QVariant(), "@heg_flamemap-choose", all_choices);
                    if (skill2 != "cancel")
                        acquired_skills << skill2 + "!";
                }
                player->setTag("FlamemapSkills", QVariant::fromValue(acquired_skills));
                LogMessage log;
                log.type = "#InvokeOthersSkill";
                log.from = player;
                log.to << sunquan;
                log.arg = objectName();
                room->sendLog(log);
                room->notifySkillInvoked(player, objectName());
                room->handleAcquireDetachSkills(player, acquired_skills);
            }
        } else if (triggerEvent == Damaged) {
            QList<int> ids = sunquan->getPile("flame_map");
            if (!ids.isEmpty()) {
                room->fillAG(ids, sunquan);
                int id = room->askForAG(sunquan, ids, false, objectName());
                room->clearAG(sunquan);
                CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), sunquan->objectName(), "heg_flamemap", QString());
                room->throwCard(Sanguosha->getCard(id), reason, NULL);
            }
        }
        return false;
    }
};

class HJiahe : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HJiahe() : TriggerSkillV2("heg_jiahe$")
    {
        events << GeneralShown << Death << DFDebut;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player == NULL) return true;
        if (triggerEvent == GeneralShown) {
            if (player->hasLordSkill(objectName(), true)) {
                if (data.toBool() == player->inHeadSkills(objectName())) {
                    room->sendCompulsoryTriggerLog(player, objectName());
                    room->broadcastSkillInvoke(objectName(), player);
                    foreach(ServerPlayer *p, room->getAlivePlayers())
                        if (p->isFriendWith(player))
                            room->attachSkillToPlayer(p, "heg_flamemap");
                }
            } else {
                ServerPlayer *lord = room->getLord(player->getSeemingKingdom());
                 if (lord && lord->isAlive() && lord->hasLordSkill(objectName(), true))
                     room->attachSkillToPlayer(player, "heg_flamemap");
            }
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who == player && player->hasLordSkill(objectName(), true)) {
                foreach(ServerPlayer *p, room->getAlivePlayers()) {
                    room->detachSkillFromPlayer(p, "heg_flamemap");
                }
            }
        } else if (triggerEvent == DFDebut) {
            ServerPlayer *lord = room->getLord(player->getSeemingKingdom());
            if (lord && lord->isAlive() && lord->hasLordSkill(objectName(), true) && !player->getAcquiredSkills().contains("heg_flamemap")) {
                room->attachSkillToPlayer(player, "heg_flamemap");
            }
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *eventPlayer, QVariant &) const override
    {
        return TriggerList();
    }
};

class HJiaheClear : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HJiaheClear() : TriggerSkillV2("#heg_jiahe-clear")
    {
        events << EventLoseSkill;
        global = true;
        frequency = Compulsory;
    }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *sunquan, QVariant &data) const override
    {
        // Clear only after the last source is retired, never when a sibling survives.
        SkillChangeStruct change;
        if (!sunquan || !change.tryParse(data) || change.skillName != "heg_jiahe"
            || change.instanceID <= 0 || sunquan->ownsSkill("heg_jiahe")
            || sunquan->hasSkillInstance(change.skillName, change.instanceID)) return true;
        foreach(ServerPlayer *p, room->getAlivePlayers()) {
            room->detachSkillFromPlayer(p, "heg_flamemap");
        }
        sunquan->clearOnePrivatePile("flame_map");
        return true;
    }
};

class HYingziFlamemap : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HYingziFlamemap() : TriggerSkillV2("heg_yingzi_flamemap")
    {
        events << DrawNCards;
        frequency = Compulsory;
    }
    bool canPreshow() const override { return true; }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (!player->hasShownSkill(objectName()) && !player->askForSkillInvoke(this)) return false;
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName(), player);
        return true;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num += 1;
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.canConvert<DrawStruct>() && data.value<DrawStruct>().reason == "draw_phase"
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
};
class HYingziFlamemapMaxCards : public MaxCardsSkillV2
{
public:
    HYingziFlamemapMaxCards() : MaxCardsSkillV2("#heg_yingzi_flamemap-maxcards") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &) const override { return CorrectSkillResult::noEffect(); }
    CorrectSkillResult getFixedValue(const CorrectSkillContext &context) const override
    {
        return context.primary ? CorrectSkillResult::useAmount(context.primary->getMaxHp()) : CorrectSkillResult::noEffect();
    }
};

HHaoshiFlamemapCard::HHaoshiFlamemapCard()
{
    setSkillName("heg_haoshi_flamemap_give");
    will_throw = false;
    mute = true;
    handling_method = Card::MethodNone;
}

bool HHaoshiFlamemapCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty() || to_select == Self)
        return false;

    return to_select->getHandcardNum() == Self->getMark("heg_haoshi_flamemap");
}

void HHaoshiFlamemapCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *lusu = card_use.from;

    QVariant data = QVariant::fromValue(card_use);
    RoomThread *thread = room->getThread();

    thread->trigger(PreCardUsed, room, lusu, data);
    thread->trigger(CardUsed, room, lusu, data);
    thread->trigger(CardFinished, room, lusu, data);
}

void HHaoshiFlamemapCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(),
        targets.at(0)->objectName(), "heg_haoshi_flamemap", QString());
    room->moveCardTo(this, targets.at(0), Player::PlaceHand, reason);
}

class HHaoshiFlamemapGiveVS : public ViewAsSkillV2
{
public:
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        // MethodNone prompts retain UNKNOWN but must match the exact selector.
        return request.initiator && request.pattern == response_pattern
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !Sanguosha || !candidate || candidate->getEffectiveId() < 0 || request.selectedCardIds.contains(candidate->getEffectiveId())) return false;
        QSet<int> seen;
        for (int id : request.selectedCardIds) {
            if (id < 0 || seen.contains(id) || !Sanguosha->getCard(id)) return false;
            seen.insert(id);
        }
        return !candidate->isEquipped() && request.selectedCardIds.size() < request.initiator->getHandcardNum() / 2;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.size() != request.initiator->getHandcardNum() / 2) return false;
        // Replay selection to reject duplicate IDs and recheck the ordered constraints.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HHaoshiFlamemapCard"; }

    HHaoshiFlamemapGiveVS() : ViewAsSkillV2("heg_haoshi_flamemap_give")
    {
        response_pattern = "@@heg_haoshi_flamemap_give!";
    }


    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;

        HHaoshiFlamemapCard *card = new HHaoshiFlamemapCard;
        card->addSubcards(request.selectedCardIds);
        return card;
    }
};


class HHaoshiFlamemap : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HHaoshiFlamemap() : TriggerSkillV2("heg_haoshi_flamemap")
    {
        events << DrawNCards;

    }

    virtual bool canPreshow() const
    {
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this)) {
            if (player->getTriggerSkills().contains(this)) {
                room->broadcastSkillInvoke(objectName(), player);
            }
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *lusu, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        lusu->setFlags(objectName());
        draw.num += 2;
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.canConvert<DrawStruct>() && data.value<DrawStruct>().reason == "draw_phase"
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
};

class HHaoshiFlamemapGive : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HHaoshiFlamemapGive() : TriggerSkillV2("#heg_haoshi_flamemap-give")
    {
        events << AfterDrawNCards;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *lusu, QVariant &data) const override
    {
        if (!data.canConvert<DrawStruct>() || data.value<DrawStruct>().reason != "draw_phase") return {};
        if (!lusu || !lusu->isAlive()) return TriggerList();
        if (lusu->hasFlag("heg_haoshi_flamemap")) {
            if (lusu->getHandcardNum() <= 5) {
                lusu->setFlags("-heg_haoshi_flamemap");
                return TriggerList();
            }
            return TriggerList{{lusu, {objectName()}}};
        }
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *lusu = ctx.invoker;
        ctx.manual_effect = true;
        lusu->setFlags("-heg_haoshi_flamemap");
        QList<ServerPlayer *> other_players = room->getOtherPlayers(lusu);
        int least = 1000;
        foreach(ServerPlayer *player, other_players)
            least = qMin(player->getHandcardNum(), least);
        room->setPlayerMark(lusu, "heg_haoshi_flamemap", least);

        if (!room->askForUseCard(lusu, "@@heg_haoshi_flamemap_give!", "@haoshi-give:::"+QString::number(lusu->getHandcardNum() / 2), -1, Card::MethodNone)) {
            // force lusu to give his half cards
            ServerPlayer *beggar = NULL;
            foreach (ServerPlayer *player, other_players) {
                if (player->getHandcardNum() == least) {
                    beggar = player;
                    break;
                }
            }

            int n = lusu->getHandcardNum() / 2;
            QList<int> to_give = lusu->handCards().mid(0, n);
            HHaoshiFlamemapCard skill_card;
            skill_card.addSubcards(to_give);
            QList<ServerPlayer *> targets;
            targets << beggar;
            skill_card.use(room, lusu, targets);
        }
        return false;
    }
};

class HShelie : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HShelie() : TriggerSkillV2("heg_shelie")
    {
        events << EventPhaseStart;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *shenlvmeng, QVariant &) const override
    {
        return ((shenlvmeng && shenlvmeng->isAlive() && shenlvmeng->hasSkill(objectName())) && shenlvmeng->getPhase() == Player::Draw) ? TriggerList{{shenlvmeng, {objectName()}}} : TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *shenlvmeng = ctx.invoker;
        // Consent/payment runs after V2 interceptors; reveal only the accepted source.
        const bool flagAdded = !ctx.owner->hasFlag("Global_askForSkillCost");
        if (flagAdded) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costGuard = qScopeGuard([&] {
            if (flagAdded) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (shenlvmeng->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), shenlvmeng);
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *shenlvmeng, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        Room *room = shenlvmeng->getRoom();
        room->notifySkillInvoked(shenlvmeng, objectName());

        QList<int> card_ids = room->getNCards(5);

        QSet<Card::Suit> suits;
        foreach(int card_id, card_ids)
            suits << Sanguosha->getCard(card_id)->getSuit();

        AskForMoveCardsStruct result = room->askForMoveCards(shenlvmeng, card_ids, QList<int>(), true, objectName(), "differentsuit", "_"+objectName(), suits.size(), 0, false, true);
        QList<int> selected = result.bottom;
        DummyCard *dummy = new DummyCard(selected);
        room->obtainCard(shenlvmeng, dummy, true);
        QList<int> card_to_throw = result.top;
        dummy = new DummyCard(card_to_throw);
        CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, shenlvmeng->objectName(), "heg_shelie", QString());
        room->throwCard(dummy, reason, NULL);
        dummy->deleteLater();
        return true;
    }
};

class HDuoshiFlamemap : public ViewAsSkillV2
{
public:
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return player->usedTimes("ViewAsSkill_duoshi_flamemapCard") < 4;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !Sanguosha || !candidate || candidate->getEffectiveId() < 0 || !request.selectedCardIds.isEmpty() || candidate->hasFlag("using")) return false;
        // Conversion accepts the same hand-like piles as response-or-use skills.
        QStringList handPiles;
        handPiles << "hand";
        for (const QString &pile : request.initiator->getPileNames())
            if (pile.startsWith('&') || pile == "wooden_ox") handPiles << pile;
        return Sanguosha->matchExpPattern(".|red|.|" + handPiles.join(','), request.initiator, candidate);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.size() != 1) return false;
        // Replay selection to reject duplicate IDs and recheck the ordered constraints.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ViewAsSkill_duoshi_flamemapCard"; }
    HDuoshiFlamemap() : ViewAsSkillV2("heg_duoshi_flamemap", 1)
    {
        response_or_use = true;
    }


    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalcard = Sanguosha->getCard(request.selectedCardIds.first());
        HAwaitExhausted *await = new HAwaitExhausted(originalcard->getSuit(), originalcard->getNumber());
        await->addSubcard(originalcard->getId());
        await->setSkillName("heg_duoshi_flamemap");
        await->setShowSkill(objectName());
        return await;
    }
};

HTransformationPackage::HTransformationPackage()
    : Package("heg_transformation")
{
    General *Xunyou = new General(this, "heg_xunyou", "wei", 3); // Wei
    Xunyou->addSkill(new HQice);
    Xunyou->addSkill(new HZhiyu);
    Xunyou->addCompanion("heg_xunyu");

    General *Bianhuanghou = new General(this, "heg_bianhuanghou", "wei", 3, false);
    Bianhuanghou->addSkill(new HWanwei);
    Bianhuanghou->addSkill(new HYuejian);
    Bianhuanghou->addSkill(new HYuejianMaxCards);
    insertRelatedSkills("heg_yuejian", "#heg_yuejian-maxcard");
    Bianhuanghou->addCompanion("heg_caocao");

    General *Liguo = new General(this, "heg_lijueguosi", "qun"); // Qun
    Liguo->addSkill(new HXiongsuan);
    Liguo->addSkill(new HXiongsuanReset);
    insertRelatedSkills("heg_xiongsuan", "#heg_xiongsuan-reset");
    Liguo->addCompanion("heg_jiaxu");

    General *Zuoci = new General(this, "heg_zuoci", "qun", 3, true, true, true);
    Zuoci->addSkill(new HHuashen);
    Zuoci->addSkill(new HHuashenClear);
    insertRelatedSkills("heg_huashen", "#heg_huashen-clear");
    Zuoci->addSkill(new HXinsheng);
    //Zuoci->addCompanion("yuji");

    General *Zuoci_new = new General(this, "heg_new_zuoci", "qun", 3);
    Zuoci_new->addSkill(new HYigui);
    Zuoci_new->addSkill(new HYiguiShow);
    Zuoci_new->addSkill(new HYiguiProhibit);
    Zuoci_new->addSkill(new HYiguiClear);
    insertRelatedSkills("heg_yigui", "#heg_yigui-show");
    insertRelatedSkills("heg_yigui", "#heg_yigui-prohibit");
    insertRelatedSkills("heg_yigui", "#heg_yigui-clear");
    Zuoci_new->addSkill(new HJihun);
    Zuoci_new->addCompanion("heg_yuji");

    General *Shamoke = new General(this, "heg_shamoke", "shu"); // Shu
    Shamoke->addSkill(new HJili);

    General *Masu = new General(this, "heg_masu", "shu", 3);
    Masu->addSkill(new HSanyao);
    Masu->addSkill(new HZhiman);

    General *Lingtong = new General(this, "heg_lingtong", "wu"); // Wu
    Lingtong->addSkill(new HXuanlue);
    Lingtong->addSkill(new HYongjin);
    Lingtong->addCompanion("heg_ganning");

    General *lvfan = new General(this, "heg_lvfan", "wu", 3);
    lvfan->addSkill(new HDiaodu);
    lvfan->addSkill(new HDiaoduDraw);
    lvfan->addSkill(new HDiancai);
    insertRelatedSkills("heg_diaodu", "#heg_diaodu-draw");

    General *sunquan = new General(this, "heg_lord_sunquan$", "wu", 4, true, true);
    sunquan->addSkill(new HJiahe);
    sunquan->addSkill(new HJiaheClear);
    insertRelatedSkills("heg_jiahe", "#heg_jiahe-clear");
    sunquan->addSkill(new HLianzi);
    sunquan->addSkill(new HJubao);
    sunquan->addRelateSkill("heg_zhiheng");
    sunquan->addRelateSkill("heg_flamemap");
    sunquan->addRelateSkill("heg_yingzi_flamemap");
    sunquan->addRelateSkill("heg_haoshi_flamemap");
    sunquan->addRelateSkill("heg_shelie");
    sunquan->addRelateSkill("heg_duoshi_flamemap");
    insertRelatedSkills("heg_haoshi_flamemap", "#heg_haoshi_flamemap-give");

    insertRelatedSkills("heg_yingzi_flamemap", "#heg_yingzi_flamemap-maxcards");
    insertRelatedSkills("heg_haoshi_flamemap", "heg_haoshi_flamemap_give");
    addMetaObject<HHaoshiFlamemapCard>();
    addMetaObject<HYongjinCard>();
    addMetaObject<HQiceCard>();
    addMetaObject<HYiguiCard>();
    addMetaObject<HXiongsuanCard>();
    addMetaObject<HSanyaoCard>();
    addMetaObject<HLianziCard>();
    addMetaObject<HFlameMapCard>();

    skills << new HHuashenBorrow << new HYiguiTurnClear;
    skills << new HZhimanSecond;
    skills << new HFlameMap;
    skills << new HYingziFlamemap << new HYingziFlamemapMaxCards << new HHaoshiFlamemapGiveVS << new HShelie << new HHaoshiFlamemap << new HHaoshiFlamemapGive << new HDuoshiFlamemap;
}

ADD_PACKAGE(HTransformation)

HLuminousPearl::HLuminousPearl(Suit suit, int number) : Treasure(suit, number)
{
    setObjectName("LuminousPearl");
}

void HLuminousPearl::onUninstall(ServerPlayer *player) const
{
    Treasure::onUninstall(player);
    player->getRoom()->addPlayerHistory(player, "HZhihengLPCard", 0);
}

class HLuminousPearlSkill : public ViewAsSkillV2
{
public:
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->hasUsed("HZhihengLPCard") && ((!player->ownSkill("heg_zhiheng") && !player->getAcquiredSkills().contains("heg_zhiheng"))
                || !((player->inHeadSkills("heg_zhiheng") && player->hasShownGeneral1()) || (player->inDeputySkills("heg_zhiheng") && player->hasShownGeneral2()))) ;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !Sanguosha || !candidate || candidate->getEffectiveId() < 0 || request.selectedCardIds.contains(candidate->getEffectiveId())) return false;
        QSet<int> seen;
        for (int id : request.selectedCardIds) {
            if (id < 0 || seen.contains(id) || !Sanguosha->getCard(id)) return false;
            seen.insert(id);
        }
        return !request.initiator->isJilei(candidate) && request.selectedCardIds.size() < request.initiator->getMaxHp() && candidate != request.initiator->getTreasure();
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.isEmpty()) return false;
        // Replay selection to reject duplicate IDs and recheck the ordered constraints.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HZhihengLPCard"; }

    bool isEquipSkill() const override { return true; }
    HLuminousPearlSkill() : ViewAsSkillV2("heg_LuminousPearl")
    {
    }


    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;

        HZhihengLPCard *zhiheng_card = new HZhihengLPCard;
        zhiheng_card->addSubcards(request.selectedCardIds);
        return zhiheng_card;
    }

};

class HLuminousPearlEquipSkill : public TreasureSkillV2
{
public:
    HLuminousPearlEquipSkill() : TreasureSkillV2("heg_LuminousPearl", "LuminousPearl")
    {
        view_as_skill = new HLuminousPearlSkill;
    }
};

HZhihengLPCard::HZhihengLPCard()
{
    setSkillName("heg_LuminousPearl");
    target_fixed = true;
    mute = true;
}

void HZhihengLPCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    if (source->isAlive())
        room->drawCards(source, subcards.length());
}

HTransformationEquipPackage::HTransformationEquipPackage() : Package("heg_transformation_equip", CardPack)
{
    HLuminousPearl *np = new HLuminousPearl();
    np->setParent(this);

    addMetaObject<HZhihengLPCard>();

    skills << new HLuminousPearlEquipSkill;
}

ADD_PACKAGE(HTransformationEquip)
