/********************************************************************
    Copyright (c) 2013-2014 - QSanguosha-Rara

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

    QSanguosha-Rara
    *********************************************************************/

#include "h-standard-wei-generals.h"
#include "general.h"
#include "protocol.h"
#include "room.h"
#include "serverplayer.h"
#include "skill.h"

class HLuoshen : public TriggerSkillV2 {
public:
    HLuoshen() : TriggerSkillV2("heg_luoshen") {
        events << EventPhaseStart;
        frequency = Frequent;
    }

    bool canPreshow() const override { return false; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override {
        if (player && player->isAlive() && player->getPhase() == Player::Start
            && player->hasSkill(objectName()))
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override {
        return ctx.owner && ctx.owner->askForSkillInvoke(this);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        ServerPlayer *zhenji = ctx.owner;
        if (!zhenji || !zhenji->isAlive()) return false;
        room->broadcastSkillInvoke(objectName(), zhenji);

        // Keep each invocation's cards local: repeated skill instances and nested
        // judgments must not share a player tag or take each other's results.
        QList<int> cards;
        const auto remainingCards = [&]() {
            QList<int> remaining;
            for (int id : cards)
                if (room->getCardPlace(id) == Player::PlaceTable)
                    remaining << id;
            return remaining;
        };
        JudgeStruct judge;
        try {
            judge.pattern = ".|black";
            judge.good = true;
            judge.reason = objectName();
            judge.play_animation = false;
            judge.who = zhenji;
            judge.time_consuming = true;
            do {
                room->judge(judge);
                if (judge.isGood() && judge.card) {
                    const int id = judge.card->getEffectiveId();
                    if (room->getCardPlace(id) == Player::PlaceTable && !cards.contains(id))
                        cards << id;
                }
            } while (judge.isGood() && zhenji->isAlive() && zhenji->askForSkillInvoke(this));
        } catch (...) {
            // A broken turn must not leave the completed judgments on the table.
            if (judge.card && judge.isGood() && !cards.contains(judge.card->getEffectiveId()))
                cards << judge.card->getEffectiveId();
            const QList<int> remaining = remainingCards();
            if (!remaining.isEmpty())
                room->throwCard(remaining, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
                    zhenji->objectName(), objectName(), QString()), nullptr);
            throw;
        }
        const QList<int> remaining = remainingCards();
        if (!remaining.isEmpty()) {
            DummyCard result(remaining);
            if (zhenji->isAlive())
                room->obtainCard(zhenji, &result);
            else
                room->throwCard(&result, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
                    zhenji->objectName(), objectName(), QString()), nullptr);
        }
        return false;
    }
};

class HLuoshenMove : public TriggerSkillV2 {
public:
    HLuoshenMove() : TriggerSkillV2("#heg_luoshen-move") {
        events << FinishJudge;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override {
        const JudgeStruct *judge = data.value<JudgeStruct *>();
        if (player && player->isAlive() && player->hasSkill(objectName()) && judge
            && judge->who == player && judge->reason == "heg_luoshen" && judge->isGood()
            && judge->card && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge)
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        const JudgeStruct *judge = ctx.original_data ? ctx.original_data->value<JudgeStruct *>() : nullptr;
        if (judge && judge->who == ctx.owner && judge->reason == "heg_luoshen" && judge->isGood()
            && judge->card && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge) {
            // Defer acquisition until the owning Luoshen invocation has finished.
            CardMoveReason reason(CardMoveReason::S_REASON_JUDGEDONE,
                ctx.owner->objectName(), QString(), judge->reason);
            room->moveCardTo(judge->card, nullptr, Player::PlaceTable, reason, true);
        }
        return false;
    }
};

class HXiaoguo : public TriggerSkillV2 {
public:
    HXiaoguo() : TriggerSkillV2("heg_xiaoguo") {
        events << EventPhaseStart;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) {
            if (owner != player && owner->isAlive() && owner->canDiscard(owner, "h"))
                result[owner] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!ctx.owner || !ctx.invoker || !ctx.invoker->isAlive()) return false;
        // Selection is cancellable; only pay() may discard the chosen basic card.
        const Card *card = room->askForCard(ctx.owner, ".Basic", "@heg_xiaoguo",
            QVariant::fromValue(ctx.invoker), Card::MethodNone, nullptr, false, objectName());
        if (!card || card->isVirtualCard() || !canPay(room, ctx.owner, card->getEffectiveId()))
            return false;
        ctx.extra_data = card->getEffectiveId();
        ctx.targets = QList<ServerPlayer *>{ctx.invoker};
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        bool ok = false;
        const int id = ctx.extra_data.toInt(&ok);
        if (!ok || !canPay(room, ctx.owner, id)) return false;
        room->throwCard(id, objectName(), ctx.owner);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        room->broadcastSkillInvoke(objectName(), 1, ctx.owner);
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override {
        if (!target || !target->isAlive() || !ctx.owner) return false;
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ctx.owner->objectName(), target->objectName());
        if (!room->askForCard(target, ".Equip", "@heg_xiaoguo-discard", QVariant::fromValue(ctx.owner))) {
            room->broadcastSkillInvoke(objectName(), 2, ctx.owner);
            room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        } else {
            // Unlike the identity version, discarding equipment grants no draw.
            room->broadcastSkillInvoke(objectName(), 3, ctx.owner);
        }
        return false;
    }

private:
    static bool canPay(Room *room, ServerPlayer *owner, int id) {
        if (!owner || !owner->isAlive() || id < 0 || room->getCardOwner(id) != owner
            || room->getCardPlace(id) != Player::PlaceHand || !owner->canDiscard(owner, id))
            return false;
        const Card *card = room->getCard(id);
        return card && card->isKindOf("BasicCard");
    }
};

void HStandardPackage::addWeiGenerals()
{
    // Reuse canonical definitions, including their related skills, AI and history keys.
    General *caocao = new General(this, "heg_caocao", "wei"); // WEI 001
    caocao->addCompanion("heg_dianwei");
    caocao->addCompanion("heg_xuchu");
    caocao->addSkill("nosjianxiong");

    General *simayi = new General(this, "heg_simayi", "wei", 3); // WEI 002
    simayi->addSkill("nosfankui");
    simayi->addSkill("nosguicai");

    General *xiahoudun = new General(this, "heg_xiahoudun", "wei"); // WEI 003
    xiahoudun->addCompanion("heg_xiahouyuan");
    xiahoudun->addSkill("nosganglie");

    General *zhangliao = new General(this, "heg_zhangliao", "wei"); // WEI 004
    zhangliao->addSkill("nostuxi");

    General *xuchu = new General(this, "heg_xuchu", "wei"); // WEI 005
    xuchu->addSkill("nosluoyi");

    General *guojia = new General(this, "heg_guojia", "wei", 3); // WEI 006
    guojia->addSkill("tiandu");
    guojia->addSkill("nosyiji");

    General *zhenji = new General(this, "heg_zhenji", "wei", 3, false); // WEI 007
    zhenji->addSkill("qingguo");
    zhenji->addSkill(new HLuoshen);
    zhenji->addSkill(new HLuoshenMove);
    insertRelatedSkills("heg_luoshen", "#heg_luoshen-move");

    General *xiahouyuan = new General(this, "heg_xiahouyuan", "wei"); // WEI 008
    xiahouyuan->addSkill("shensu");

    General *zhanghe = new General(this, "heg_zhanghe", "wei"); // WEI 009
    zhanghe->addSkill("qiaobian");

    General *xuhuang = new General(this, "heg_xuhuang", "wei"); // WEI 010
    xuhuang->addSkill("duanliang");

    General *caoren = new General(this, "heg_caoren", "wei"); // WEI 011
    caoren->addSkill("nosjushou");

    General *dianwei = new General(this, "heg_dianwei", "wei"); // WEI 012
    dianwei->addSkill("qiangxi");

    General *xunyu = new General(this, "heg_xunyu", "wei", 3); // WEI 013
    xunyu->addSkill("quhu");
    xunyu->addSkill("jieming");

    General *caopi = new General(this, "heg_caopi", "wei", 3); // WEI 014
    caopi->addCompanion("heg_zhenji");
    caopi->addSkill("xingshang");
    caopi->addSkill("fangzhu");

    General *yuejin = new General(this, "heg_yuejin", "wei", 4); // WEI 016
    yuejin->addSkill(new HXiaoguo);
}
