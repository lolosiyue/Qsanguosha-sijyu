#include "maotu.h"
//#include "client.h"
//#include "general.h"
//#include "skill.h"
//#include "standard-generals.h"
#include "engine.h"
#include "maneuvering.h"
//#include "json.h"
#include "settings.h"
#include "clientplayer.h"
//#include "util.h"
//#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
//#include "clientstruct.h"
#include "mobile.h"
#include "wind.h"
#include "skill-instance-utils.h"

namespace {

// MethodNone prompts carry UNKNOWN but still require the exact selector.
bool isPromptRequest(const ActiveSkillRequest &request, const QString &pattern)
{
    return request.initiator && request.pattern == pattern
        && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
            || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
}

bool matchesFilter(const ActiveSkillRequest &request, const Card *card, const QString &pattern)
{
    return request.initiator && card && card->getEffectiveId() >= 0 && !card->hasFlag("using")
        && !request.selectedCardIds.contains(card->getEffectiveId())
        && Sanguosha->matchExpPattern(pattern, request.initiator, card);
}

// Replay the selection in order, exactly as the server rebuilds it.
bool replaySelection(const ViewAsSkillV2 *skill, const ActiveSkillRequest &request)
{
    ActiveSkillRequest prefix = request;
    prefix.selectedCardIds.clear();
    foreach (int id, request.selectedCardIds) {
        const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
        if (!card || !skill->canSelectCard(prefix, card)) return false;
        prefix.selectedCardIds << id;
    }
    return true;
}

// V2 records history under the activation skill; conversions keep the card's own key.
QString cardHistoryKey(const QString &cardName, const QString &fallback)
{
    Card *card = Sanguosha->cloneCard(cardName);
    if (!card) return fallback;
    const QString key = card->getClassName();
    card->deleteLater();
    return key;
}

}

class MTLiaoshi : public TriggerSkillV2
{
public:
    MTLiaoshi() : TriggerSkillV2("mtliaoshi")
    {
        events << MarkChanged << CardsMoveOneTime << HpChanged;
        waked_skills = "#mtliaoshi";
    }

    static void DOSTH(ServerPlayer *player, QString choice, bool change_num, QString reason)
    {
        Room *room = player->getRoom();
        int num = player->getMark("&mtliaoshi_num");

        if (choice == "discard") {
            num++;
            if (num > 8) num = 1;
            if (player->canDiscard(player, "he"))
                room->askForDiscard(player, reason, 2, 2, false, true); //objectName()会报错
        } else if (choice == "lose") {
            num++;
            if (num > 8) num = 1;
            room->loseHp(HpLostStruct(player, 1, reason, player));
        } else if (choice == "draw") {
            num--;
            if (num < 1) num = 8;
            player->drawCards(2, reason);
        } else {
            num--;
            if (num < 1) num = 8;
            room->recover(player, RecoverStruct("mtliaoshi", player));
        }
        if (change_num && player->isAlive()) {  //复活后数字应该也变化了，这里为了ui，偷懒
            room->setPlayerMark(player, "&mtliaoshi_num", num);
            QVariant data = "mtliaoshi_choice_" + choice;
            room->getThread()->trigger(EventForDiy, room, player, data);
        }
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        const int num = player->getMark("&mtliaoshi_num");
        if (num <= 0) return TriggerList();
        bool matched = false;
        if (event == MarkChanged) {
            matched = data.value<MarkStruct>().name == "&mtliaoshi_num"
                && (num == player->getHp() || num == player->getHandcardNum());
        } else if (event == HpChanged) {
            matched = num == player->getHp();
        } else {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            matched = num == player->getHandcardNum()
                && ((move.to == player && move.to_place == Player::PlaceHand)
                    || (move.from == player && move.from_places.contains(Player::PlaceHand)));
        }
        return matched ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        int dis = 0;
        QStringList choices;
        foreach (int id, player->handCards() + player->getEquipsId()) {
            if (player->canDiscard(player, id))
                dis++;
        }
        if (dis > 1) choices << "discard";
        choices << "lose" << "draw";
        if (player->isWounded()) choices << "recover";
        choices << "cancel";

        const QString choice = room->askForChoice(player, objectName(), choices.join("+"));
        if (choice == "cancel") return false;
        ctx.choice = choice;
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        player->skillInvoked(this, 0);
        DOSTH(player, ctx.choice, true, objectName());
        return false;
    }
};

class MTLiaoshiChoose : public TriggerSkillV2
{
public:
    MTLiaoshiChoose() : TriggerSkillV2("#mtliaoshi")
    {
        events << GameStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, "mtliaoshi", true, true);
        QStringList choices;
        for (int i = 1; i < 9; i++) {
            if (i == player->getHp() || i == player->getHandcardNum()) continue;
            choices << QString::number(i);
        }
        if (choices.isEmpty()) return false;
        QString choice = room->askForChoice(player, "mtliaoshi_num", choices.join("+"));
        int num = choice.toInt();
        if (num <= 0) num = 1;
        room->setPlayerMark(player, "&mtliaoshi_num", num);
        return false;
    }
};

class MTTongyi : public TriggerSkillV2
{
public:
    MTTongyi() : TriggerSkillV2("mttongyi")
    {
        events << EventForDiy;
    }

    static QString liaoshiChoice(const QVariant &data)
    {
        const QString str = data.toString();
        return str.startsWith("mtliaoshi_choice_") ? str.split("_").last() : QString();
    }

    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *player, const QString &choice)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getMark("mttongyi_target-Keep") > 0) continue;
            if (choice == "recover" && !p->isWounded()) continue;
            if (choice == "discard" && p->getCardCount() < 2) continue;
            targets << p;
        }
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        const QString choice = liaoshiChoice(data);
        if (choice.isEmpty() || candidates(room, player, choice).isEmpty()) return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QList<ServerPlayer *> targets = candidates(room, player, liaoshiChoice(*ctx.original_data));
        if (targets.isEmpty()) return false;
        ServerPlayer *t = room->askForPlayerChosen(player, targets, objectName(), "@mttongyi-invoke", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        player->peiyin(this);
        room->addPlayerMark(target, "mttongyi_target-Keep");
        MTLiaoshi::DOSTH(target, liaoshiChoice(*ctx.original_data), false, objectName());
        return false;
    }
};

class MTXianzhengVS : public ViewAsSkillV2
{
public:
    MTXianzhengVS() : ViewAsSkillV2("mtxianzheng", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@mtxianzheng");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!ViewAsSkillV2::canSelectCard(request, card) || !matchesFilter(request, card, ".")) return false;
        Slash slash(Card::SuitToBeDecided, -1);
        slash.addSubcard(card);
        slash.setSkillName(objectName());
        return slash.isAvailable(request.initiator);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && replaySelection(this, request);
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return cardHistoryKey("slash", "Slash");
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        Slash *slash = new Slash(Card::SuitToBeDecided, -1);
        slash->addSubcards(request.selectedCardIds);
        slash->setSkillName(objectName());
        return slash;
    }
};

class MTXianzheng : public TriggerSkillV2
{
public:
    MTXianzheng() : TriggerSkillV2("mtxianzheng")
    {
        events << EventPhaseStart << Damage;
        view_as_skill = new MTXianzhengVS;
    }

    static bool canMove(Room *room, ServerPlayer *to)
    {
        if (!to || to->isDead() || (to->getEquips().isEmpty() && to->getJudgingArea().isEmpty())) return false;
        return room->canMoveField("ej", QList<ServerPlayer *>{to}, room->getOtherPlayers(to));
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Start || (player->isNude() && player->getHandPile().isEmpty()))
                return TriggerList();
        } else {
            const DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("Slash") || !canMove(room, damage.to)) return TriggerList();
        }
        return TriggerList{{player, QStringList{objectName()}}};
    }

    // At the start phase the Slash is the invocation; declining it never invoked Xianzheng.
    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart)
            return room->askForUseCard(player, "@@mtxianzheng", "@mtxianzheng");
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        return canMove(room, damage.to)
            && player->askForSkillInvoke(this, "mtxianzheng:" + damage.to->objectName());
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != Damage) return false;
        ServerPlayer *to = ctx.original_data->value<DamageStruct>().to;
        player->peiyin(this);
        room->moveField(player, objectName(), false, "ej", QList<ServerPlayer *>{to}, room->getOtherPlayers(to));
        return false;
    }
};

class MTNianchou : public TriggerSkillV2
{
public:
    MTNianchou() : TriggerSkillV2("mtnianchou")
    {
        events << EventPhaseStart << Death;
        shiming_skill = true;
        waked_skills = "tenyearshensu,baobian,#mtnianchou";
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::RoundStart || room->getOtherPlayers(player).isEmpty()) return TriggerList();
        } else {
            const DeathStruct death = data.value<DeathStruct>();
            if (!death.who || death.who == player || !death.damage || death.damage->from != player) return TriggerList();
        }
        // Each pending mission instance resolves on its own.
        QStringList names;
        foreach (int id, player->getValidSkillInstanceIds(objectName())) {
            const SkillInstanceRef ref(player->objectName(), SkillInstanceKey(objectName(), id));
            if (room->getShimingStatus(ref) == 0)
                names << SkillInstanceUtils::formatName(objectName(), id);
        }
        return names.isEmpty() ? TriggerList() : TriggerList{{player, names}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (room->getShimingStatus(ctx.activationRef) > 0) return false;
        if (event != EventPhaseStart) return true;
        ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@mtnianchou-target", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = ctx.activationRef;
        if (event == EventPhaseStart) {
            if (ctx.targets.isEmpty()) return false;
            player->peiyin(this);
            room->addPlayerMark(player, "mtnianchou_from-Clear");
            room->addPlayerMark(ctx.targets.first(), "mtnianchou_to-Clear");

            if (player->getHp() != 1) return false;
            if (!room->sendShimingLog(ref, false)) return false;
            room->detachSkillFromPlayer(player, ref.key.toString());
            room->handleAcquireDetachSkills(player, "-mtxianzheng|baobian");
        } else {
            if (!room->sendShimingLog(ref)) return false;

            QString choices = "draw";
            if (player->isWounded()) choices = "recover+draw";
            QString choice = room->askForChoice(player, objectName(), choices);

            if (choice == "draw")
                player->drawCards(2, objectName());
            else
                room->recover(player, RecoverStruct("mtnianchou", player));

            room->handleAcquireDetachSkills(player, "tenyearshensu");
        }
        return false;
    }
};

class MTNianchouTargetMod : public TargetModSkillV2
{
public:
    MTNianchouTargetMod() : TargetModSkillV2("#mtnianchou", "^SkillCard")
    {
        frequency = NotFrequent;
        shiming_skill = true;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType == TargetModSkill::DistanceLimit && ctx.primary && ctx.secondary
            && ctx.primary->getMark("mtnianchou_from-Clear") > 0 && ctx.secondary->getMark("mtnianchou_to-Clear") > 0)
            return CorrectSkillResult::useAmount(1000);
        return CorrectSkillResult::noEffect();
    }
};

class MTJieliVS : public ViewAsSkillV2
{
public:
    MTJieliVS() : ViewAsSkillV2("mtjieli")
    {
    }

    // Every hand card of the colour declared in the dialog, as one Duel.
    static Duel *colourDuel(const Player *player, const QString &colour)
    {
        if (!player || (colour != "red" && colour != "black")) return nullptr;
        Duel *duel = new Duel(Card::SuitToBeDecided, -1);
        duel->setSkillName("mtjieli");
        foreach (const Card *c, player->getHandcards()) {
            if ((c->isRed() && colour == "red") || (c->isBlack() && colour == "black"))
                duel->addSubcard(c);
        }
        if (duel->subcardsLength() > 0) return duel;
        delete duel;
        return nullptr;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || request.initiator->isKongcheng())
            return false;
        const int limit = qMax(1, request.initiator->getMark("SkillDescriptionArg1_mtjieli"));
        return request.initiator->usedTimes("MTJieliCard") < limit;
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        Duel *duel = colourDuel(request.initiator, request.userString);
        const bool ok = duel && candidate && duel->targetFilter(selected, candidate, request.initiator)
            && !request.initiator->isProhibited(candidate, duel, selected);
        delete duel;
        return ok;
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        Duel *duel = colourDuel(request.initiator, request.userString);
        const bool ok = duel && duel->targetsFeasible(selected, request.initiator);
        delete duel;
        return ok;
    }

    // The colour's cards are the Duel's material, not a discarded price.
    bool willThrowSelectedCards() const override
    {
        return false;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "MTJieliCard";
    }

    // The proxy carries the declared colour to the server.
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        Duel *duel = colourDuel(request.initiator, request.userString);
        const bool usable = duel && duel->isAvailable(request.initiator);
        delete duel;
        return usable ? ViewAsSkillV2::createCard(request) : nullptr;
    }

    // The accepted activation is used as the ordinary Duel over the colour's cards.
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        Duel *duel = colourDuel(request.initiator, request.userString);
        if (!duel) return false;
        duel->deleteLater();
        duel->setActivationSkill(objectName(), request.getActivationInstanceId());
        if (ctx.use_card)
            duel->setSourceSkill(ctx.use_card->getSourceSkillName(), ctx.use_card->getSourceSkillInstanceId());
        ctx.updated_card = duel;
        return true;
    }
};

class MTJieli : public TriggerSkillV2
{
public:
    MTJieli() : TriggerSkillV2("mtjieli")
    {
        events << CardFinished << DamageDone << EventPhaseChanging;
        view_as_skill = new MTJieliVS;
        waked_skills = "#mtjieli";
    }

    SkillDialogInfo getDialogInfo() const override
    {
        return SkillDialogInfo::tiansuan("mtjieli", "red,black");
    }

    static int damagePoint(const Card *card)
    {
        int d = 0;
        foreach (QString flag, card->getFlags()) {
            if (!flag.startsWith("mtjieli_damage_point_")) continue;
            QStringList flags = flag.split("_");
            if (flags.length() != 4) continue;
            d = qMax(d, flags.last().toInt());
        }
        return d;
    }

    // Duel damage and the turn-end reset are bookkeeping for every player.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging) {
            if (player && data.value<PhaseChangeStruct>().to == Player::NotActive) {
                room->setPlayerMark(player, "SkillDescriptionArg1_mtjieli", 0);
                room->changeTranslation(player, "mtjieli", 2);
            }
        } else if (event == DamageDone) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("Duel")) return true;
            ServerPlayer *user = room->getCardUser(damage.card);
            if (!user || user->getPhase() == Player::NotActive) return true;

            int d = 0;
            foreach (QString flag, damage.card->getFlags()) {
                if (!flag.startsWith("mtjieli_damage_point_")) continue;
                QStringList flags = flag.split("_");
                if (flags.length() != 4) continue;

                room->setCardFlag(damage.card, "-" + flag);

                int dd = flags.last().toInt();
                if (dd > d)
                    d = dd;
            }

            d += damage.damage;
            room->setCardFlag(damage.card, "mtjieli_damage_point_" + QString::number(d));
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardFinished || !player || !player->hasSkill(objectName()) || !player->hasFlag("CurrentPlayer"))
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Duel") || damagePoint(use.card) < 2) return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        LogMessage log;
        log.type = "#MTJieliTimes";
        log.from = player;
        log.arg = objectName();
        room->sendLog(log);
        room->notifySkillInvoked(player, objectName());

        int mark = player->getMark("SkillDescriptionArg1_mtjieli");
        if (mark <= 0) mark = 1;
        room->setPlayerMark(player, "SkillDescriptionArg1_mtjieli", mark + 1);
        player->setSkillDescriptionSwap("mtjieli", "%arg1", QString::number(mark + 1));
        room->changeTranslation(player, "mtjieli", 1);
        return false;
    }
};

class MTJieliTargetMod : public TargetModSkillV2
{
public:
    MTJieliTargetMod() : TargetModSkillV2("#mtjieli", "Duel")
    {
        frequency = NotFrequent;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType == TargetModSkill::ExtraTarget && ctx.primary && ctx.primary->hasSkill("mtjieli"))
            return CorrectSkillResult::useAmount(1);
        return CorrectSkillResult::noEffect();
    }
};

class MTFuyi : public TriggerSkillV2
{
public:
    MTFuyi() : TriggerSkillV2("mtfuyi")
    {
        events << Death;
        frequency = Wake;
        waked_skills = "#mtfuyi,#mtfuyi-turn";
    }

    static bool killedBy(const DeathStruct &death, const ServerPlayer *player)
    {
        return death.damage && death.damage->from && death.damage->from == player;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || player->getMark(objectName()) > 0 || !player->hasSkill(objectName()))
            return TriggerList();
        // canWake() consumes its grant, so selection only peeks at it.
        if (killedBy(data.value<DeathStruct>(), player)
            && player->getTag(objectName() + "_SKILLCANWAKE").toStringList().isEmpty())
            return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        // Another instance may already have awakened this player.
        if (player->getMark(objectName()) > 0) return false;
        if (killedBy(ctx.original_data->value<DeathStruct>(), player) && !player->canWake(objectName()))
            return false;
        room->sendCompulsoryTriggerLog(player, this);
        room->doSuperLightbox(player, "mtfuyi");
        room->setPlayerMark(player, "mtfuyi", 1);
        if (room->changeMaxHpForAwakenSkill(player, 1, objectName())) {
            room->recover(player, RecoverStruct(objectName(), player));
            room->addPlayerMark(player, "&mtfuyi_buff");
            room->addPlayerMark(player, "mtfuyi_extra_turn");
        }
        return false;
    }
};

class MTFuyiDamage : public TriggerSkillV2
{
public:
    MTFuyiDamage() : TriggerSkillV2("#mtfuyi")
    {
        events << ConfirmDamage;
        frequency = Compulsory;
    }

    // The buff follows its mark on either side of the damage.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.to || damage.to->isDead()) return true;

        int d = damage.damage;

        if (damage.from && damage.from->getMark("&mtfuyi_buff") > 0) {
            d += damage.from->getMark("&mtfuyi_buff");

            LogMessage log;
            log.type = "#MTFuyiDamage";
            log.from = damage.from;
            log.to << damage.to;
            log.arg = "mtfuyi";
            log.arg2 = QString::number(d);
            room->sendLog(log);
            damage.from->peiyin("mtfuyi");
            room->notifySkillInvoked(damage.from, "mtfuyi");

            damage.damage = d;
            data = QVariant::fromValue(damage);
        }

        if (damage.to->getMark("&mtfuyi_buff") > 0) {
            d += damage.to->getMark("&mtfuyi_buff");

            LogMessage log;
            log.type = "#MTFuyiDamage";
            log.from = damage.to;
            log.to << damage.to;
            log.arg = "mtfuyi";
            log.arg2 = QString::number(d);
            room->sendLog(log);
            damage.to->peiyin("mtfuyi");
            room->notifySkillInvoked(damage.to, "mtfuyi");

            damage.damage = d;
            data = QVariant::fromValue(damage);
        }
        return true;
    }
};

class MTFuyiTurn : public TriggerSkillV2
{
public:
    MTFuyiTurn() : TriggerSkillV2("#mtfuyi-turn")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    // Extra turns granted by Fuyi start after whichever turn ends, even if the skill is gone.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || player->getPhase() != Player::NotActive) return true;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            int mark = p->getMark("mtfuyi_extra_turn");
            if (mark <= 0) continue;

            for (int i = 0; i < mark; i++) {
                if (p->isDead()) break;
                room->removePlayerMark(p, "mtfuyi_extra_turn");

                LogMessage log;
                log.type = "#MTFuyiTurn";
                log.from = p;
                log.arg = "mtfuyi";
                room->sendLog(log);

                p->gainAnExtraTurn();
            }
        }
        return true;
    }
};

class MTZhongyi : public TriggerSkillV2
{
public:
    MTZhongyi() : TriggerSkillV2("mtzhongyi")
    {
        events << TargetConfirming << EventPhaseStart;
    }

    static bool isSoleDamageTarget(const CardUseStruct &use)
    {
        return use.card && use.to.length() == 1
            && (use.card->isKindOf("Slash") || (use.card->isDamageCard() && !use.card->isKindOf("DelayedTrick")));
    }

    // How many extractions p may take from the current player this finish phase.
    static int extractions(Room *room, ServerPlayer *p)
    {
        int total = 0;
        foreach (ServerPlayer *pp, room->getAllPlayers()) {
            if (pp->getHp() != pp->getMark("mtzhongyi_hp-Keep")) continue;
            total += qMax(0, pp->getMark("mtzhongyi_" + p->objectName() + "-Keep"));
        }
        return total;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player) return result;
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Finish) return result;
            ServerPlayer *current = room->getCurrent();
            if (!current || current->isDead() || current->isNude()) return result;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->isAlive() && p->hasSkill(objectName()) && extractions(room, p) > 0
                    && !(p == current && p->getEquips().isEmpty()))
                    result[p] << objectName();
            }
        } else {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!isSoleDamageTarget(use) || use.to.first() != player || player->isDead()) return result;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->isAlive() && p->hasSkill(objectName())
                    && (player == p || player->getHandcardNum() < p->getHandcardNum()))
                    result[p] << objectName();
            }
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *, ServerPlayer *p, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) return true;
        ServerPlayer *t = ctx.invoker;
        return t && t->isAlive() && p->askForSkillInvoke(this, "draw:" + t->objectName());
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        if (event == TargetConfirming) {
            ServerPlayer *t = ctx.invoker;
            p->peiyin(this);
            t->addMark("mtzhongyi_" + p->objectName() + "-Keep");
            t->drawCards(1, objectName());
            return false;
        }

        ServerPlayer *current = room->getCurrent();
        foreach (ServerPlayer *pp, room->getAllPlayers()) {
            if (pp->getHp() != pp->getMark("mtzhongyi_hp-Keep")) continue;
            int mark = pp->getMark("mtzhongyi_" + p->objectName() + "-Keep");
            if (mark < 1 || p->isDead()) continue;

            for (int i = 0; i < mark; i++) {
                if (!current || current->isDead() || current->isNude()) break;
                if (p == current && p->getEquips().isEmpty()) break;
                if (!p->askForSkillInvoke(this, "current:" + current->objectName())) break;
                int card_id = room->askForCardChosen(p, current, p == current ? "e" : "he", "mtzhongyi");
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, p->objectName());
                room->obtainCard(p, Sanguosha->getCard(card_id), reason, false);
            }
        }
        return false;
    }
};

class MTWeiqieVS : public ViewAsSkillV2
{
public:
    MTWeiqieVS() : ViewAsSkillV2("mtweiqie")
    {
        expand_pile = "#mtweiqie";
    }

    static int gcd(int a, int b)
    {
        return b == 0 ? a : gcd(b, a % b);
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@mtweiqie");
    }

    // Every pair of chosen shown cards must have coprime numbers.
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || request.selectedCardIds.contains(card->getEffectiveId())
            || !getExpandPileCardIds(request.initiator).contains(card->getEffectiveId())) return false;
        foreach (int id, request.selectedCardIds) {
            if (gcd(card->getNumber(), Sanguosha->getCard(id)->getNumber()) != 1) return false;
        }
        return true;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return !request.selectedCardIds.isEmpty() && replaySelection(this, request);
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "DummyCard";
    }

    // Only the selection; Weiqie itself moves the chosen cards.
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        DummyCard *dummy = new DummyCard;
        dummy->addSubcards(request.selectedCardIds);
        return dummy;
    }
};

class MTWeiqie : public TriggerSkillV2
{
public:
    MTWeiqie() : TriggerSkillV2("mtweiqie")
    {
        events << DrawNCards;
        view_as_skill = new MTWeiqieVS;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.value<DrawStruct>().reason == "draw_phase"
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
    {
        return player->askForSkillInvoke(this);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        player->peiyin(this);

        QList<int> cards = room->showDrawPile(player, 4, objectName());
        player->setTag("mtweiqieForAI", QVariant::fromValue(cards));
        room->notifyMoveToPile(player, cards, objectName(), Player::PlaceTable, true);
        const Card *c = room->askForCard(player, "@@mtweiqie", "@mtweiqie", QVariant(), Card::MethodNone);
        room->notifyMoveToPile(player, cards, objectName(), Player::PlaceTable, false);
        player->removeTag("mtweiqieForAI");

        if (c && c->subcardsLength() > 0) {
            QList<int> subcards = c->getSubcards();
            foreach (int id, subcards)
                cards.removeOne(id);

            DummyCard *dummy = new DummyCard(subcards);
            room->obtainCard(player, dummy);
            dummy->deleteLater();
        }

        if (!cards.isEmpty()) {
            DummyCard *dummy = new DummyCard(cards);
            CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), "mtweiqie", "");
            room->throwCard(dummy, reason, nullptr);
            dummy->deleteLater();
        }

        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num = 0;
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class MTGuanda : public TriggerSkillV2
{
public:
    MTGuanda() : TriggerSkillV2("mtguanda")
    {
        events << CardsMoveOneTime;
    }

    // Whether the move put a Slash into the discard pile, and whether one of them is red.
    static bool movedSlash(const CardsMoveOneTimeStruct &move, bool *red = nullptr)
    {
        if (move.to_place != Player::DiscardPile) return false;
        bool slash = false;
        if (red) *red = false;
        foreach (int id, move.card_ids) {
            const Card *c = Sanguosha->getCard(id);
            if (c->isKindOf("Slash")) {
                slash = true;
                if (c->isRed() && red)
                    *red = true;
            }
        }
        return slash;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && movedSlash(data.value<CardsMoveOneTimeStruct>())
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
    {
        return player->askForSkillInvoke(this);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        bool red = false;
        movedSlash(ctx.original_data->value<CardsMoveOneTimeStruct>(), &red);
        player->peiyin(this);

        QList<int> two = room->getNCards(2);
        LogMessage log;
        log.from = player;
        log.type = "$ViewDrawPile";
        log.card_str = ListI2S(two).join("+");
        room->sendLog(log, player);

        log.type = "#ViewDrawPile";
        log.arg = "2";
        room->sendLog(log, room->getOtherPlayers(player, true));

        room->fillAG(two, player);
        int id = room->askForAG(player, two, true, objectName(), red ? "@mtguanda-get" : "@mtguanda-see");
        room->clearAG(player);

        room->returnToTopDrawPile(two);
        if (red && id > -1)
            room->obtainCard(player, id, false);
        return false;
    }
};

class MTZhilie : public ViewAsSkillV2
{
public:
    MTZhilie() : ViewAsSkillV2("mtzhilie")
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("MTZhilieCard");
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return selected.isEmpty() && candidate && !candidate->isKongcheng() && candidate != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "MTZhilieCard";
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *to) const override
    {
        ServerPlayer *from = ctx.invoker;
        if (!from || from->isDead() || to->isKongcheng()) return ContinueEffects;

        Room *room = from->getRoom();

        int id = room->askForCardChosen(from, to, "h", "mtzhilie");
        room->showCard(to, id);
        const Card *show = Sanguosha->getCard(id);

        QStringList choices;
        int same = 0, dis = 0;

        foreach (const Card *c, from->getEquips()) {
            if (c->sameColorWith(show)) {
                same++;
                if (from->canDiscard(from, c->getEffectiveId()))
                    dis++;
            }
        }
        if (same > 0 && dis > 0) {
            same = qMin(same, 2);
            choices << QString("damage=%1=%2=%3").arg(show->getColorString()).arg(to->objectName()).arg(same);
        }

        Card *usecard = Sanguosha->cloneCard(show->objectName(), Card::NoSuit, 0);
        if (usecard) {
            usecard->setSkillName("_mtzhilie");
            usecard->deleteLater();
            if ((usecard->isKindOf("BasicCard") || usecard->isNDTrick()) && from->canUse(usecard, to, true))
                choices << "use=" + to->objectName() + "=" + show->objectName();
        }

        choices << "cancel";

        CardUseStruct use;
        use.from = to;
        use.card = usecard;
        QVariant data = QVariant::fromValue(use);  //For AI

        QString choice = room->askForChoice(from, "mtzhilie", choices.join("+"), data);

        if (choice == "cancel")
            return ContinueEffects;
        else if (choice.startsWith("damage")) {
            same = 0;
            DummyCard *discard = new DummyCard;

            foreach (const Card *c, from->getEquips()) {
                if (c->sameColorWith(show)) {
                    same++;
                    if (from->canDiscard(from, c->getEffectiveId()))
                        discard->addSubcard(c);
                }
            }

            same = qMin(same, 2);
            if (discard->subcardsLength() > 0) {
                room->throwCard(discard, from);
                room->damage(DamageStruct("mtzhilie", from, to, same));
            }
            discard->deleteLater();
        } else {
            if (usecard && from->canUse(usecard, to, true))
                room->useCard(CardUseStruct(usecard, from, to));
        }
        return ContinueEffects;
    }
};

class MTChuanjiu : public TriggerSkillV2
{
public:
    MTChuanjiu() : TriggerSkillV2("mtchuanjiu")
    {
        events << CardUsed << EventPhaseStart;
        frequency = Compulsory;
        waked_skills = "#mtchuanjiu";
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!player->hasFlag("CurrentPlayer") || !use.card || !use.card->isKindOf("TrickCard")) return TriggerList();
            Analeptic ana(Card::NoSuit, 0);
            ana.setSkillName("_mtchuanjiu");
            if (!Analeptic::IsAvailable(player, &ana)) return TriggerList();
        } else if (player->getPhase() != Player::Finish || !player->isWounded()
                   || player->getMark("mtchuanjiu_Analeptic-Clear") <= 1) {
            return TriggerList();
        }
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (event == CardUsed) {
            Analeptic *ana = new Analeptic(Card::NoSuit, 0);
            ana->setSkillName("_mtchuanjiu");
            ana->deleteLater();

            if (!Analeptic::IsAvailable(player, ana)) return false;
            room->sendCompulsoryTriggerLog(player, this);
            room->useCard(CardUseStruct(ana, player), !player->isWounded());
        } else {
            room->sendCompulsoryTriggerLog(player, objectName());
            room->loseHp(HpLostStruct(player, 1, objectName(), player));
        }
        return false;
    }
};


class MTDianpei : public TriggerSkillV2
{
public:
    MTDianpei() : TriggerSkillV2("mtdianpei")
    {
        events << EventPhaseStart;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::RoundStart && player->getHandcardNum() <= player->getHp()
            && !room->getOtherPlayers(player).isEmpty()
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@mtdianpei-invoke", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &, ServerPlayer *t) const override
    {
        player->peiyin(this);

        if (!player->isKongcheng())
            t->addToPile(objectName(), player->handCards());

        if (player->isDead() || t->isDead()) return false;
        if (t->isNude() || player->getHandcardNum() >= player->getHp()) return false;

        int num = qMax(player->getLostHp(), 1), draw = qMax(player->getHp() - player->getHandcardNum(), 0);
        QString prompt = QString("@mtdianpei-give:%1:%2:%3").arg(player->objectName()).arg(num).arg(draw);
        const Card *ex = room->askForExchange(t, objectName(), num, num, true, prompt, true);
        if (!ex)
            player->drawCards(player->getHp() - player->getHandcardNum());
        else
            room->giveCard(t, player, ex, objectName());

        QList<int> pile = t->getPile(objectName());
        if (t->isAlive() && !pile.isEmpty()) {  //偷懒处理成获得全部，不管是不是player扣置的
            LogMessage log;
            log.type = "$KuangbiGet";
            log.from = t;
            log.arg = objectName();
            log.card_str = ListI2S(pile).join("+");
            room->sendLog(log, t);

            log.type = "#MTDianpeiGet";
            log.from = t;
            log.arg2 = QString::number(pile.length());
            room->sendLog(log, room->getOtherPlayers(t, true));

            DummyCard get(pile);
            room->obtainCard(t, &get, false);
        }
        return false;
    }
};

class MTRenyiVS : public ViewAsSkillV2
{
public:
    MTRenyiVS() : ViewAsSkillV2("mtrenyi")
    {
    }

    // @@mtrenyi1 distributes the drawn cards; @@mtrenyi2 uses the chosen basic card.
    static bool isDistribution(const ActiveSkillRequest &request)
    {
        return request.pattern == "@@mtrenyi1";
    }

    static Card *basicCard(const ActiveSkillRequest &request)
    {
        const int id = request.initiator ? request.initiator->getMark("mtrenyi_id-Clear") - 1 : -1;
        if (id < 0) return nullptr;
        Card *card = Sanguosha->cloneCard(Sanguosha->getEngineCard(id)->objectName());
        if (card) card->setSkillName("_mtrenyi");
        return card;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@mtrenyi1") || isPromptRequest(request, "@@mtrenyi2");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return isDistribution(request) && matchesFilter(request, card, ".")
            && request.selectedCardIds.length() < request.initiator->getMark("mtrenyiShowNum-Clear");
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!isDistribution(request)) return request.selectedCardIds.isEmpty();
        return !request.selectedCardIds.isEmpty()
            && request.selectedCardIds.length() == request.initiator->getMark("mtrenyiShowNum-Clear")
            && replaySelection(this, request);
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return candidate && candidate != request.initiator && selected.length() < request.selectedCardIds.length();
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return selected.length() == request.selectedCardIds.length();
    }

    TargetEffectMode targetEffectMode() const override
    {
        return WholeTargetGroup;
    }

    QString historyKey(const ActiveSkillRequest &request) const override
    {
        if (isDistribution(request)) return "MTRenyiCard";
        const int id = request.initiator ? request.initiator->getMark("mtrenyi_id-Clear") - 1 : -1;
        return id < 0 ? objectName() : cardHistoryKey(Sanguosha->getEngineCard(id)->objectName(), objectName());
    }

    bool willThrowSelectedCards() const override
    {
        return false;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        if (isDistribution(request)) return ViewAsSkillV2::createCard(request);
        return basicCard(request);
    }

    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !ctx.use_card) return FinishSkill;
        Room *room = source->getRoom();
        room->setPlayerMark(source, "mtrenyiShowNum-Clear", 0);

        QList<int> subcards = ctx.use_card->getSubcards();

        try {
            room->fillAG(subcards);
            foreach (ServerPlayer *p, targets) {
                if (subcards.isEmpty()) break;
                if (p->isDead()) continue;
                int id = room->askForAG(p, subcards, false, "mtrenyi");
                subcards.removeOne(id);
                room->takeAG(p, id, false);
                room->obtainCard(p, id);
            }
            room->getThread()->delay();
            room->clearAG();
        }
        catch (TriggerEvent triggerEvent) {
            if (triggerEvent == TurnBroken || triggerEvent == StageChange)
                room->clearAG();
        }

        if (source->isDead() || source->getPhase() == Player::NotActive) return FinishSkill;

        QList<int> list = room->getAvailableCardList(source, "basic", "mtrenyi");
        if (list.isEmpty()) return FinishSkill;

        room->fillAG(list, source);
        int id = room->askForAG(source, list, true, "mtrenyi", "@mtrenyi-use");
        room->clearAG(source);
        if (id < 0) return FinishSkill;

        QString name = Sanguosha->getEngineCard(id)->objectName();
        room->setPlayerMark(source, "mtrenyi_id-Clear", id + 1);
        Card *card = Sanguosha->cloneCard(name);
        if (!card) return FinishSkill;
        card->deleteLater();
        card->setSkillName("_mtrenyi");

        if (card->targetFixed()) {
            if (!source->askForSkillInvoke("mtrenyi", QString("mtrenyi_use:%1").arg(name), false)) return FinishSkill;
            room->useCard(CardUseStruct(card, source));
        } else
            room->askForUseCard(source, "@@mtrenyi2", "@mtrenyi2:" + name, 2, Card::MethodUse, false);
        return FinishSkill;
    }
};

class MTRenyi : public TriggerSkillV2
{
public:
    MTRenyi() : TriggerSkillV2("mtrenyi")
    {
        events << CardsMoveOneTime;
        view_as_skill = new MTRenyiVS;
        waked_skills = "#mtrenyi";
    }

    static int drawnCount(ServerPlayer *player, const CardsMoveOneTimeStruct &move)
    {
        if (move.to != player || move.to_place != Player::PlaceHand || !move.from_places.contains(Player::DrawPile)) return 0;
        int show = 0;
        for (int i = 0; i < move.card_ids.length(); i++) {
            int id = move.card_ids.at(i);
            if (!player->hasCard(id) || move.from_places.at(i) != Player::DrawPile) continue;
            show++;
        }
        return show;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && !room->getTag("FirstRound").toBool()
            && drawnCount(player, data.value<CardsMoveOneTimeStruct>()) > 0
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    // The distribution card is the invocation; declining it never invoked Renyi.
    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const int show = drawnCount(player, ctx.original_data->value<CardsMoveOneTimeStruct>());
        if (show <= 0) return false;
        room->setPlayerMark(player, "mtrenyiShowNum-Clear", show);
        const Card *used = room->askForUseCard(player, "@@mtrenyi1", "@mtrenyi1:" + QString::number(show), 1, Card::MethodNone);
        room->setPlayerMark(player, "mtrenyiShowNum-Clear", 0);
        return used != nullptr;
    }
};

class MTRenyiTargetMod : public TargetModSkillV2
{
public:
    MTRenyiTargetMod() : TargetModSkillV2("#mtrenyi", "BasicCard")
    {
        frequency = NotFrequent;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType == TargetModSkill::Residue && ctx.card && ctx.card->getSkillName() == "mtrenyi")
            return CorrectSkillResult::useAmount(999);
        return CorrectSkillResult::noEffect();
    }
};

class MTFeirenVS : public ViewAsSkillV2
{
public:
    MTFeirenVS() : ViewAsSkillV2("mtfeiren", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Slash::IsAvailable(request.initiator);
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && (request.pattern.contains("slash") || request.pattern.contains("Slash"));
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!ViewAsSkillV2::canSelectCard(request, card) || !matchesFilter(request, card, ".")
            || card->getTypeId() != Card::TypeEquip) return false;
        Slash slash(Card::SuitToBeDecided, -1);
        slash.addSubcard(card->getEffectiveId());
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return slash.isAvailable(request.initiator);
        return !request.initiator->isLocked(&slash);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && replaySelection(this, request);
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return cardHistoryKey("slash", "Slash");
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->addSubcard(originalCard);
        slash->setSkillName(objectName());
        return slash;
    }
};

class MTFeiren : public TriggerSkillV2
{
public:
    MTFeiren() : TriggerSkillV2("mtfeiren")
    {
        events << CardFinished << CardUsed;
        view_as_skill = new MTFeirenVS;
    }

    static QString usedMark(Room *room)
    {
        ServerPlayer *current = room->getCurrent();
        if (!current || current->getPhase() >= Player::NotActive) return QString();
        return "mtfeiren_used-" + QString::number(current->getPhase()) + "Clear";
    }

    // Only the owner's own damage cards are marked for a later return.
    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != CardUsed || ctx.owner != player || !player->isAlive() || !player->hasSkill(objectName())) return;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (use.card && (use.card->isKindOf("Slash") || (use.card->isDamageCard() && !use.card->isKindOf("DelayedTrick"))))
            room->setCardFlag(use.card, "mtfeirenBf");
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardFinished || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->isKongcheng()) return TriggerList();
        const QString mark = usedMark(room);
        if (mark.isEmpty() || player->getMark(mark) > 0) return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->hasFlag("mtfeirenBf") || !use.card->hasFlag("DamageDone")) return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QList<int> ids;
        if (use.card->isVirtualCard())
            ids = use.card->getSubcards();
        else
            ids << use.card->getEffectiveId();

        room->fillAG(ids, player);
        const Card *c = room->askForCard(player, ".|.|.|hand", "@mtfeiren", *ctx.original_data, Card::MethodNone);
        room->clearAG(player);
        if (!c) return false;
        ctx.extra_data = c->getEffectiveId();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QString mark = usedMark(room);
        if (!mark.isEmpty())
            player->addMark(mark);

        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = player;
        log.arg = objectName();
        room->sendLog(log);
        player->peiyin(this);
        room->notifySkillInvoked(player, objectName());

        CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), "mtfeiren", "");
        room->throwCard(Sanguosha->getCard(ctx.extra_data.toInt()), reason, nullptr);
        if (player->isDead()) return false;
        room->obtainCard(player, ctx.original_data->value<CardUseStruct>().card);
        return false;
    }
};

class MTFeirenTargetMod : public TargetModSkillV2
{
public:
    MTFeirenTargetMod() : TargetModSkillV2("#mtfeiren")
    {
        frequency = NotFrequent;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType == TargetModSkill::DistanceLimit && ctx.card && ctx.card->getSkillName() == "mtfeiren")
            return CorrectSkillResult::useAmount(999);
        return CorrectSkillResult::noEffect();
    }
};

class MTFuzhan : public TriggerSkillV2
{
public:
    MTFuzhan() : TriggerSkillV2("mtfuzhan")
    {
        events << EventPhaseStart;
    }

    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *p)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *q, room->getOtherPlayers(p)) {
            if (p->getMark("mtfuzhanDamage_" + q->objectName() + "-Clear") <= 0 &&
                    q->getMark("mtfuzhanDamage_" + p->objectName() + "-Clear") <= 0) continue;
            if (!p->canPindian(q)) continue;
            targets << q;
        }
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->getPhase() != Player::Finish) return result;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isAlive() && p->hasSkill(objectName()) && !candidates(room, p).isEmpty())
                result[p] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        ServerPlayer *t = room->askForPlayerChosen(p, candidates(room, p), objectName(), "@mtfuzhan-pindian", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &, ServerPlayer *t) const override
    {
        p->peiyin(this);

        PindianStruct *pindian = p->PinDian(t, objectName());
        if (pindian->from_number == pindian->to_number) return false;

        ServerPlayer *winner = t, *loser = p;
        if (pindian->success) {
            winner = p;
            loser = t;
        }

        Slash *slash = new Slash(Card::SuitToBeDecided, -1);
        slash->addSubcard(pindian->from_card);
        if (pindian->from_card->getEffectiveId() != pindian->to_card->getEffectiveId())
            slash->addSubcard(pindian->to_card);
        slash->setSkillName("_mtfuzhan");
        slash->deleteLater();

        Duel *duel = new Duel(Card::SuitToBeDecided, -1);
        duel->addSubcard(pindian->from_card);
        if (pindian->from_card->getEffectiveId() != pindian->to_card->getEffectiveId())
            duel->addSubcard(pindian->to_card);
        duel->setSkillName("_mtfuzhan");
        duel->deleteLater();

        QStringList choices;
        if (winner->canSlash(loser, slash, false))
            choices << "slash=" + loser->objectName();
        if (winner->canUse(duel, loser, true))
            choices << "duel=" + loser->objectName();
        if (choices.isEmpty()) return false;

        QString choice = room->askForChoice(winner, objectName(), choices.join("+"), QVariant::fromValue(pindian));
        if (choice.startsWith("slash"))
            room->useCard(CardUseStruct(slash, winner, loser));
        else
            room->useCard(CardUseStruct(duel, winner, loser));
        return false;
    }
};


class MTRenyu : public TriggerSkillV2
{
public:
    MTRenyu() : TriggerSkillV2("mtrenyu")
    {
        events << TargetSpecified;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !(use.card->isKindOf("Slash") || use.card->isNDTrick())) return TriggerList();
        foreach (ServerPlayer *p, use.to) {
            if (p != player && p->isAlive())
                return TriggerList{{player, QStringList{objectName()}}};
        }
        return TriggerList();
    }

    // Each target answers separately: the owner discards from allies, other kingdoms choose for themselves.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        CardUseStruct use = data.value<CardUseStruct>();
        foreach (ServerPlayer *p, use.to) {
            if (player->isDead() || !player->hasSkill(objectName())) break;
            if (p->isDead() || !use.to.contains(p) || p == player) continue;

            QString kingdom1 = player->getKingdom(), kingdom2 = p->getKingdom();
            if (kingdom1 == kingdom2) {
                if (!player->canDiscard(p, "he")) continue;
                player->setTag("MTRenyuData", data);
                bool invoke = player->askForSkillInvoke(this, p);
                player->removeTag("MTRenyuData");

                if (!invoke) continue;
                player->peiyin(this);

                int id = room->askForCardChosen(player, p, "he", objectName(), false, Card::MethodDiscard);
                room->throwCard(id, p, player);

                use.nullified_list << p->objectName();
                data = QVariant::fromValue(use);
            } else {
                p->setTag("MTRenyuData", data);
                bool invoke = p->askForSkillInvoke("mtrenyu_jin", "mtrenyu_jin");
                p->removeTag("MTRenyuData");
                if (!invoke) continue;

                LogMessage log;
                log.type = "#InvokeOthersSkill";
                log.from = p;
                log.to << player;
                log.arg = objectName();
                room->sendLog(log);
                player->peiyin(this);
                room->notifySkillInvoked(player, objectName());

                p->drawCards(1, objectName());

                log.type = "#ChangeKingdom2";
                log.arg = p->getKingdom();
                log.arg2 = "jin";
                room->sendLog(log);
                room->setPlayerProperty(p, "kingdom", "jin");

                use.nullified_list << p->objectName();
                data = QVariant::fromValue(use);
            }
        }
        return false;
    }
};

class MTFengshang : public TriggerSkillV2
{
public:
    MTFengshang() : TriggerSkillV2("mtfengshang")
    {
        events << CardFinished;
        waked_skills = "#mtfengshang";
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play)
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->isKindOf("SkillCard")) return TriggerList();

        QString kingdom = player->getKingdom();
        int same = 0;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getKingdom() == kingdom)
                same++;
        }
        if (same <= player->getMark("mtfengshang_times-PlayClear")) return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *t = room->askForPlayerChosen(player, room->getAllPlayers(), objectName(), "@mtfengshang-invoke", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &, ServerPlayer *t) const override
    {
        room->addPlayerMark(player, "mtfengshang_times-PlayClear");
        player->peiyin(this);

        t->drawCards(1, objectName());

        if (player->isAlive() && t->getHandcardNum() > player->getHandcardNum()) {
            QString phase = QString::number((int)Player::Finish);
            room->addPlayerMark(player, "&mtfengshang_debuff-Self" + phase + "Clear");
        }
        return false;
    }
};

class MTFengshangKeep : public MaxCardsSkillV2
{
public:
    MTFengshangKeep() : MaxCardsSkillV2("#mtfengshang")
    {
        frequency = NotFrequent;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const int debuff = ctx.primary
            ? ctx.primary->getMark("&mtfengshang_debuff-Self" + QString::number((int)Player::Finish) + "Clear") : 0;
        return debuff > 0 ? CorrectSkillResult::useAmount(-debuff) : CorrectSkillResult::noEffect();
    }
};

class MTJiawei : public TriggerSkillV2
{
public:
    MTJiawei() : TriggerSkillV2("mtjiawei$")
    {
        events << CardUsed;
    }

    static bool canBecomeUser(Room *room, ServerPlayer *player, ServerPlayer *simayan, const CardUseStruct &use)
    {
        if (simayan->isDead() || !simayan->hasLordSkill("mtjiawei") || simayan->getMark("mtjiawei_used-Clear") > 0) return false;
        foreach (ServerPlayer *p, use.to) {
            if (!use.card->isAvailable(simayan) || simayan->isLocked(use.card) || room->isProhibited(player, p, use.card) ||
                    !use.card->targetFilter(QList<const Player *>(), p, simayan))
                return false;
        }
        return true;
    }

    // The liege's card use triggers the lord; the liege decides whether to hand it over.
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getKingdom() != "jin" || player->getPhase() == Player::NotActive)
            return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || (!use.card->isKindOf("Slash") && !use.card->isNDTrick())) return result;
        foreach (ServerPlayer *simayan, room->getOtherPlayers(player)) {
            if (simayan->hasSkill(objectName()) && canBecomeUser(room, player, simayan, use))
                result[simayan] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *simayan, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!player || !canBecomeUser(room, player, simayan, use)) return false;
        return room->askForPlayerChosen(player, QList<ServerPlayer *>{simayan}, objectName(),
            "@mtjiawei-invoke:" + use.card->objectName(), true) == simayan;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *simayan, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        room->addPlayerMark(simayan, "mtjiawei_used-Clear");

        LogMessage log;
        log.type = "#InvokeOthersSkill";
        log.from = player;
        log.to << simayan;
        log.arg = simayan->isWeidi() ? "weidi" : objectName();
        room->sendLog(log);
        room->doAnimate(1, player->objectName(), simayan->objectName());
        if (simayan->isWeidi()) {
            simayan->peiyin("weidi");
            room->notifySkillInvoked(simayan, "weidi");
        } else {
            simayan->peiyin(this);
            room->notifySkillInvoked(simayan, objectName());
        }

        log.type = "#BecomeUser";
        log.from = simayan;
        log.card_str = use.card->toString();
        room->sendLog(log);

        use.from = simayan;
        *ctx.original_data = QVariant::fromValue(use);

        if (simayan->isDead() || player->isDead()) return false;
        if (!simayan->askForSkillInvoke("mtjiawei", "mtjiawei:" + player->objectName(), false)) return false;

        if (simayan->getPhase() == Player::Play)
            room->addPlayerMark(simayan, "mtfengshang_times-PlayClear");

        log.type = "#ChoosePlayerWithSkill";
        log.from = simayan;
        log.to.clear();
        log.to << player;
        log.arg = "mtfengshang";
        room->sendLog(log);
        room->doAnimate(1, simayan->objectName(), player->objectName());
        simayan->peiyin("mtfengshang");
        room->notifySkillInvoked(simayan, "mtfengshang");

        player->drawCards(1, "mtfengshang");

        if (simayan->isAlive() && player->getHandcardNum() > simayan->getHandcardNum()) {
            QString phase = QString::number((int)Player::Finish);
            room->addPlayerMark(simayan, "&mtfengshang_debuff-Self" + phase + "Clear");
        }
        return false;
    }
};

class MTGuzhaoVS : public ViewAsSkillV2
{
public:
    MTGuzhaoVS() : ViewAsSkillV2("mtguzhao")
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("MTGuzhaoCard");
    }

    // Up to three consecutive other players with hand cards.
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets,
                         const Player *to_select) const override
    {
        if (!to_select || to_select->isKongcheng() || to_select == request.initiator || targets.length() > 2) return false;
        if (targets.isEmpty()) return true;
        return to_select->isAdjacentTo(targets.last()) || to_select->isAdjacentTo(targets.first());
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return !targets.isEmpty();
    }

    TargetEffectMode targetEffectMode() const override
    {
        return WholeTargetGroup;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "MTGuzhaoCard";
    }

    // One simultaneous pindian: the source's single card against each target's card.
    static int pindian(ServerPlayer *from, ServerPlayer *target, const Card *card1, const Card *card2)
    {
        if (!card2 || !from->canPindian(target, false)) return -2;

        Room *room = from->getRoom();

        PindianStruct *pindian_struct = new PindianStruct;
        pindian_struct->from = from;
        pindian_struct->to = target;
        pindian_struct->from_card = card1;
        pindian_struct->to_card = card2;
        pindian_struct->from_number = card1->getNumber();
        pindian_struct->to_number = card2->getNumber();
        pindian_struct->reason = "mtguzhao";
        QVariant data = QVariant::fromValue(pindian_struct);

        QList<CardsMoveStruct> moves;
        CardsMoveStruct move1;
        move1.card_ids << pindian_struct->from_card->getEffectiveId();
        move1.from = pindian_struct->from;
        move1.to = nullptr;
        move1.to_place = Player::PlaceTable;
        move1.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->from->objectName(),
            pindian_struct->to->objectName(), pindian_struct->reason, "");

        CardsMoveStruct move2;
        move2.card_ids << pindian_struct->to_card->getEffectiveId();
        move2.from = pindian_struct->to;
        move2.to = nullptr;
        move2.to_place = Player::PlaceTable;
        move2.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->to->objectName(),
            pindian_struct->reason, "");

        moves.append(move1);
        moves.append(move2);
        room->moveCardsAtomic(moves, true);

        LogMessage log;
        log.type = "$PindianResult";
        log.from = pindian_struct->from;
        log.card_str = QString::number(pindian_struct->from_card->getEffectiveId());
        room->sendLog(log);

        log.type = "$PindianResult";
        log.from = pindian_struct->to;
        log.card_str = QString::number(pindian_struct->to_card->getEffectiveId());
        room->sendLog(log);

        RoomThread *thread = room->getThread();
        thread->trigger(PindianVerifying, room, from, data);

        pindian_struct = data.value<PindianStruct *>();

        pindian_struct->success = pindian_struct->from_number > pindian_struct->to_number;

        log.type = pindian_struct->success ? "#PindianSuccess" : "#PindianFailure";
        log.from = from;
        log.to.clear();
        log.to << target;
        log.card_str.clear();
        room->sendLog(log);

        JsonArray arg;
        arg << QSanProtocol::S_GAME_EVENT_REVEAL_PINDIAN << pindian_struct->from->objectName() << pindian_struct->from_card->getEffectiveId()
            << target->objectName() << pindian_struct->to_card->getEffectiveId() << pindian_struct->success << "mtguzhao";
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, arg);

        data = QVariant::fromValue(pindian_struct);
        thread->trigger(Pindian, room, from, data);

        moves.clear();
        if (room->getCardPlace(pindian_struct->from_card->getEffectiveId()) == Player::PlaceTable) {
            CardsMoveStruct move1;
            move1.card_ids << pindian_struct->from_card->getEffectiveId();
            move1.from = pindian_struct->from;
            move1.to = nullptr;
            move1.to_place = Player::DiscardPile;
            move1.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->from->objectName(),
                pindian_struct->to->objectName(), pindian_struct->reason, "");
            moves.append(move1);
        }

        if (room->getCardPlace(pindian_struct->to_card->getEffectiveId()) == Player::PlaceTable) {
            CardsMoveStruct move2;
            move2.card_ids << pindian_struct->to_card->getEffectiveId();
            move2.from = pindian_struct->to;
            move2.to = nullptr;
            move2.to_place = Player::DiscardPile;
            move2.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->to->objectName(),
                pindian_struct->reason, "");
            moves.append(move2);
        }
        if (!moves.isEmpty())
            room->moveCardsAtomic(moves, true);

        QVariant decisionData = QVariant::fromValue(QString("pindian:%1:%2:%3:%4:%5")
            .arg("mtguzhao").arg(from->objectName()).arg(pindian_struct->from_card->getEffectiveId())
            .arg(target->objectName()).arg(pindian_struct->to_card->getEffectiveId()));
        thread->trigger(ChoiceMade, room, from, decisionData);

        if (pindian_struct->success) return 1;
        else if (pindian_struct->from_number == pindian_struct->to_number) return 0;
        else if (pindian_struct->from_number < pindian_struct->to_number) return -1;
        return -2;
    }

    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source) return FinishSkill;
        Room *room = source->getRoom();

        QHash<ServerPlayer *, int> show;
        QList<ServerPlayer *> new_targets;
        foreach (ServerPlayer *target, targets) {
            if (target->isDead() || target->isKongcheng()) continue;
            int id = target->getRandomHandCardId();
            show[target] = id;
            new_targets << target;
            room->showCard(target, id, source, false);
        }

        if (new_targets.isEmpty() || !source->canPindian()) return FinishSkill;

        LogMessage log;
        log.type = "#Pindian";
        log.from = source;
        log.to = new_targets;
        room->sendLog(log);

        const Card *cardss = nullptr;
        QHash<ServerPlayer *, const Card *> hash;
        foreach (ServerPlayer *target, new_targets) {
            if (!source->canPindian(target, false)) continue;

            PindianStruct *pindian = new PindianStruct;
            pindian->from = source;
            pindian->to = target;
            pindian->from_card = cardss;
            pindian->to_card = nullptr;
            pindian->reason = "mtguzhao";

            RoomThread *thread = room->getThread();
            QVariant data = QVariant::fromValue(pindian);
            thread->trigger(AskforPindianCard, room, source, data);

            pindian = data.value<PindianStruct *>();

            if (!pindian->from_card && !pindian->to_card) {
                QList<const Card *> cards = room->askForPindianRace(source, target, "mtguzhao");
                pindian->from_card = cards.first();
                pindian->to_card = cards.last();
            } else if (!pindian->to_card) {
                if (pindian->from_card->isVirtualCard())
                    pindian->from_card = Sanguosha->getCard(pindian->from_card->getEffectiveId());
                pindian->to_card = room->askForPindian(target, source, "mtguzhao");
            } else if (!pindian->from_card) {
                if (pindian->to_card->isVirtualCard())
                    pindian->to_card = Sanguosha->getCard(pindian->to_card->getEffectiveId());
                pindian->from_card = room->askForPindian(source, source, "mtguzhao");
            }
            cardss = pindian->from_card;
            hash[target] = pindian->to_card;
        }

        if (!cardss) return FinishSkill;

        FireSlash *fire_slash = new FireSlash(Card::NoSuit, 0);
        fire_slash->deleteLater();
        fire_slash->setSkillName("_mtguzhao");
        if (source->isLocked(fire_slash) || !fire_slash->IsAvailable(source)) return FinishSkill;

        QList<ServerPlayer *> slash_targets;
        bool all_win = true;
        foreach (ServerPlayer *target, new_targets) {
            int n = pindian(source, target, cardss, hash[target]);
            if (n == -2) continue;
            if (n != 1) all_win = false;

            if (!show[target] || show[target] < 0 || show[target] == hash[target]->getEffectiveId() || !source->canSlash(target, fire_slash, false)) continue;
            slash_targets << target;
        }

        if (slash_targets.isEmpty()) return FinishSkill;
        if (all_win) room->setCardFlag(fire_slash, "mtguzhao_all_win");
        room->useCard(CardUseStruct(fire_slash, source, slash_targets));
        return FinishSkill;
    }
};

class MTGuzhao : public TriggerSkillV2
{
public:
    MTGuzhao() : TriggerSkillV2("mtguzhao")
    {
        events << ConfirmDamage;
        view_as_skill = new MTGuzhaoVS;
        waked_skills = "#mtguzhao";
    }

    // The bonus belongs to the Fire Slash itself, whoever holds Guzhao now.
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.card->isKindOf("FireSlash") || !damage.card->hasFlag("mtguzhao_all_win")) return true;
        ++damage.damage;
        data = QVariant::fromValue(damage);
        return true;
    }
};

class MTGuzhaoTargetMod : public TargetModSkillV2
{
public:
    MTGuzhaoTargetMod() : TargetModSkillV2("#mtguzhao")
    {
        frequency = NotFrequent;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType == TargetModSkill::Residue && ctx.card && ctx.card->getSkillName() == "mtguzhao")
            return CorrectSkillResult::useAmount(1000);
        return CorrectSkillResult::noEffect();
    }
};

class MTGuquVS : public ViewAsSkillV2
{
public:
    MTGuquVS() : ViewAsSkillV2("mtguqu")
    {
    }

    static QStringList suits(const Player *player)
    {
        const QString record = player->property("MTGuquSuits").toString();
        return record.isEmpty() ? QStringList() : record.split("+");
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@mtguqu");
    }

    // One card of each missing suit.
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!matchesFilter(request, card, ".") || request.initiator->isJilei(card) || !card->hasSuit()) return false;
        if (!suits(request.initiator).contains(card->getSuitString())) return false;
        foreach (int id, request.selectedCardIds) {
            if (Sanguosha->getCard(id)->getSuit() == card->getSuit())
                return false;
        }
        return true;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return !request.selectedCardIds.isEmpty() && request.selectedCardIds.length() == suits(request.initiator).length()
            && replaySelection(this, request);
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "DummyCard";
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        DummyCard *card = new DummyCard;
        card->setSkillName(objectName());
        card->addSubcards(request.selectedCardIds);
        return card;
    }
};

class MTGuqu : public TriggerSkillV2
{
public:
    MTGuqu() : TriggerSkillV2("mtguqu")
    {
        events << EventPhaseStart;
        view_as_skill = new MTGuquVS;
    }

    // The ended turn's suit record is consumed here, even if nobody can use it.
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || player->getPhase() != Player::NotActive) return true;
        player->setTag("MTGuquPending", player->getTag("MTGuquRecord"));
        player->removeTag("MTGuquRecord");
        return true;
    }

    static QStringList missingSuits(ServerPlayer *player)
    {
        QStringList all_suits;
        all_suits << "heart" << "diamond" << "spade" << "club";
        foreach (QString suit, player->getTag("MTGuquPending").toStringList())
            all_suits.removeOne(suit);
        return all_suits;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->getPhase() != Player::NotActive) return result;
        const QStringList all_suits = missingSuits(player);
        if (all_suits.isEmpty()) return result;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->isAlive() && p->hasSkill(objectName()) && p->getCardCount() >= all_suits.length())
                result[p] << objectName();
        }
        return result;
    }

    // Discarding the missing suits is the invocation; declining never invoked Guqu.
    bool cost(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        const QStringList all_suits = missingSuits(ctx.invoker);
        if (all_suits.isEmpty()) return false;

        QStringList records;
        records << QString("@mtguqu-discard1%1").arg(all_suits.length());
        foreach (QString suit, all_suits)
            records << "<img src='image/system/cardsuit/" + suit + ".png' height=17/>";

        room->setPlayerProperty(p, "MTGuquSuits", all_suits.join("+"));
        const Card *c = room->askForCard(p, "@@mtguqu", records.join(""), QVariant(), objectName());
        return c && p->isAlive();
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *p, SkillContext &) const override
    {
        p->peiyin(this);
        p->gainAnExtraTurn();
        return false;
    }
};

class MTLunhuanVS : public ViewAsSkillV2
{
public:
    MTLunhuanVS() : ViewAsSkillV2("mtlunhuan")
    {
    }

    // Counts of each suit, in the order heart, diamond, spade, club.
    static QList<int> suitCounts(const QStringList &suits)
    {
        QList<int> counts{0, 0, 0, 0};
        const QStringList order{"heart", "diamond", "spade", "club"};
        foreach (QString suit, suits) {
            const int i = order.indexOf(suit);
            if (i >= 0) counts[i]++;
        }
        return counts;
    }

    static QStringList shownSuits(const Player *player)
    {
        const QString record = player->property("MTLunhuanSuits").toString();
        return record.isEmpty() ? QStringList() : record.split("+");
    }

    static QStringList selectedSuits(const ActiveSkillRequest &request)
    {
        QStringList suits;
        foreach (int id, request.selectedCardIds)
            suits << Sanguosha->getCard(id)->getSuitString();
        return suits;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@mtlunhuan");
    }

    // Hand cards matching the shown suits, no more of a suit than was shown.
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!matchesFilter(request, card, ".|.|.|hand") || request.initiator->isJilei(card) || !card->hasSuit()) return false;
        const QStringList shown = shownSuits(request.initiator);
        if (shown.isEmpty()) return false;
        const QStringList order{"heart", "diamond", "spade", "club"};
        const int i = order.indexOf(card->getSuitString());
        return i >= 0 && suitCounts(selectedSuits(request)).at(i) < suitCounts(shown).at(i);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        const QStringList shown = shownSuits(request.initiator);
        return !request.selectedCardIds.isEmpty() && !shown.isEmpty()
            && suitCounts(selectedSuits(request)) == suitCounts(shown) && replaySelection(this, request);
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "DummyCard";
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        DummyCard *card = new DummyCard;
        card->setSkillName(objectName());
        card->addSubcards(request.selectedCardIds);
        return card;
    }
};

class MTLunhuan : public TriggerSkillV2
{
public:
    MTLunhuan() : TriggerSkillV2("mtlunhuan")
    {
        events << EventPhaseEnd;
        waked_skills = "#mtlunhuan";
        view_as_skill = new MTLunhuanVS;
    }

    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!p->isKongcheng())
                targets << p;
        }
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
            && !candidates(room, player).isEmpty()
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *t = room->askForPlayerChosen(player, candidates(room, player), objectName(), "@mtlunhuan-invoke", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &, ServerPlayer *t) const override
    {
        player->peiyin(this);

        QList<int> show_ids;
        if (t->getHandcardNum() <= 4)
            show_ids = t->handCards();
        else {
            for (int i = 0; i < 4; ++i) {
                if (t->getHandcardNum() <= i) break;
                int id = room->askForCardChosen(player, t, "h", objectName(), false, Card::MethodNone, show_ids);
                if (id < 0) break;
                show_ids << id;
            }
        }
        if (show_ids.isEmpty()) return false;
        room->showCard(t, show_ids);

        if (player->isDead() || player->getCardCount() < show_ids.length() || player->getMark("MTLunhuanDamage-Keep") > 0) return false;

        QStringList suits;
        foreach (int id, show_ids)
            suits << Sanguosha->getCard(id)->getSuitString();

        room->setPlayerProperty(player, "MTLunhuanSuits", suits.join("+"));
        if (!room->askForCard(player, "@@mtlunhuan", QString("@mtlunhuan-discard:%1:%2").arg(t->objectName()).arg(show_ids.length()),
            QVariant::fromValue(t), objectName())) return false;
        player->peiyin(this);
        room->damage(DamageStruct(objectName(), player, t, show_ids.length(), DamageStruct::Fire));
        return false;
    }
};

class MTLunhuanDamage : public TriggerSkillV2
{
public:
    MTLunhuanDamage() : TriggerSkillV2("#mtlunhuan")
    {
        events << DamageDone;
        frequency = Compulsory;
    }

    // Recorded for the damage source, even after Lunhuan is gone.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.from && damage.reason == "mtlunhuan")
            room->setPlayerMark(damage.from, "MTLunhuanDamage-Keep", 1);
        return true;
    }
};

class MTJiyeVS : public ViewAsSkillV2
{
public:
    MTJiyeVS() : ViewAsSkillV2("mtjiye")
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@mtjiye");
    }

    // Only suits missing from the Ye pile, one card of each.
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!matchesFilter(request, card, ".") || !card->hasSuit()) return false;
        foreach (int id, request.initiator->getPile("yhjyye")) {
            if (Sanguosha->getCard(id)->getSuit() == card->getSuit())
                return false;
        }
        foreach (int id, request.selectedCardIds) {
            if (Sanguosha->getCard(id)->getSuit() == card->getSuit())
                return false;
        }
        return true;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return !request.selectedCardIds.isEmpty() && replaySelection(this, request);
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "DummyCard";
    }

    // Only the selection; Jiye itself puts the cards on the pile.
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        DummyCard *dummy = new DummyCard;
        dummy->addSubcards(request.selectedCardIds);
        return dummy;
    }
};

class MTJiye : public TriggerSkillV2
{
public:
    MTJiye() : TriggerSkillV2("mtjiye")
    {
        events << RoundStart;
        frequency = Compulsory;
        view_as_skill = new MTJiyeVS;
    }

    static int getQueshaoSuitsNum(const Player *player)
    {
        QList<int> ye = player->getPile("yhjyye");
        QList<Card::Suit> all_suits;
        all_suits << Card::Heart << Card::Diamond << Card::Spade << Card::Club;
        foreach (int id, ye) {
            Card::Suit suit = Sanguosha->getCard(id)->getSuit();
            if (!all_suits.contains(suit)) continue;
            all_suits.removeOne(suit);
        }
        return all_suits.length();
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && getQueshaoSuitsNum(player) > 0
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        int num = getQueshaoSuitsNum(player);
        if (num <= 0) return false;
        room->sendCompulsoryTriggerLog(player, this);
        player->drawCards(num, objectName());
        if (player->isNude()) return false;
        const Card *c = room->askForCard(player, "@@mtjiye", "@mtjiye", QVariant(), Card::MethodNone);
        if (c && c->subcardsLength() > 0 && player->isAlive())
            player->addToPile("yhjyye", c);
        return false;
    }
};

class MTZhiheVS : public ViewAsSkillV2
{
public:
    MTZhiheVS() : ViewAsSkillV2("mtzhihe")
    {
        response_or_use = true;
    }

    static int needed(const Player *player)
    {
        return qMax(1, MTJiye::getQueshaoSuitsNum(player));
    }

    // Basic and non-delayed trick names that Zhihe may declare, from the Ye pile.
    static QStringList pileNames(const Player *player)
    {
        QStringList names;
        foreach (int id, player->getPile("yhjyye")) {
            const Card *c = Sanguosha->getCard(id);
            if ((c->isKindOf("BasicCard") || c->isNDTrick()) && !names.contains(c->objectName()))
                names << c->objectName();
        }
        return names;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.initiator->getPile("yhjyye").isEmpty()) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return !pileNames(request.initiator).isEmpty();
        if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
            && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
        if (request.pattern == "peach" && request.initiator->getMark("Global_PreventPeach") > 0) return false;
        return !usableNames(request).isEmpty();
    }

    // X hand cards of one suit.
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!matchesFilter(request, card, ".|.|.|hand")) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE) {
            if (request.initiator->isCardLimited(card, Card::MethodResponse)) return false;
        } else if (request.initiator->isLocked(card)) {
            return false;
        }
        if (request.selectedCardIds.length() >= needed(request.initiator)) return false;
        return request.selectedCardIds.isEmpty()
            || Sanguosha->getCard(request.selectedCardIds.first())->getSuit() == card->getSuit();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.length() == needed(request.initiator) && replaySelection(this, request);
    }

protected:
    bool allowDeclaration(const Player *player, const QString &name) const override
    {
        return pileNames(player).contains(name);
    }
};

class MTZhihe : public TriggerSkillV2
{
public:
    MTZhihe() : TriggerSkillV2("mtzhihe")
    {
        events << CardFinished;
        view_as_skill = new MTZhiheVS;
    }

    SkillDialogInfo getDialogInfo() const override
    {
        return SkillDialogInfo::guhuo("mtzhihe", true, true, true);
    }

    // The declared Ye card leaves the pile at the retired skill's priority.
    bool usesEventPriority() const override
    {
        return true;
    }

    int getPriority(TriggerEvent) const override
    {
        return 0;
    }

    static QList<int> declared(ServerPlayer *player, const Card *card)
    {
        QList<int> remove;
        foreach (int id, player->getPile("yhjyye")) {
            if (Sanguosha->getCard(id)->sameNameWith(card, true))
                remove << id;
        }
        return remove;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        const Card *card = data.value<CardUseStruct>().card;
        if (!card || card->isKindOf("SkillCard") || !card->getSkillNames().contains(objectName())
            || declared(player, card).isEmpty()) return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QList<int> remove = declared(player, ctx.original_data->value<CardUseStruct>().card);
        if (remove.isEmpty()) return false;

        DummyCard remove_card(remove);
        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, player->objectName(), objectName(), "");
        room->throwCard(&remove_card, reason, nullptr);
        return false;
    }
};

class MTWenqi : public TriggerSkillV2
{
public:
    MTWenqi() : TriggerSkillV2("mtwenqi")
    {
        events << Damaged;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->hasTurn()) return result;
        const DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *from = damage.from, *to = damage.to;
        if (!from || from == to || from->isDead()) return result;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isAlive() && p->hasSkill(objectName()) && p->getMark("mtwenqi_Used-Clear") <= 0
                && (from == p || to == p))
                result[p] << objectName();
        }
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
        if (!from || from->isDead() || p->getMark("mtwenqi_Used-Clear") > 0) return false;
        room->sendCompulsoryTriggerLog(p, this);
        room->addPlayerMark(p, "mtwenqi_Used-Clear");

        QStringList choices;
        int num = MTJiye::getQueshaoSuitsNum(p);

        if (!p->getPile("yhjyye").isEmpty())
            choices << "get=" + QString::number(num + 1);
        choices << "draw=" + QString::number(num);

        QString choice = room->askForChoice(from, objectName(), choices.join("+"), QVariant::fromValue(p));

        if (choice.startsWith("get")) {
            QList<int> ye = p->getPile("yhjyye");
            if (!ye.isEmpty()) {
                room->fillAG(ye, from);
                int id = room->askForAG(from, ye, false, objectName(), "@mtwenqi-get");
                room->clearAG(from);

                if (from == p) {
                    LogMessage log;
                    log.type = "$KuangbiGet";
                    log.from = from;
                    log.arg = "yhjyye";
                    log.card_str = QString::number(id);
                    room->sendLog(log);
                }
                room->obtainCard(from, id);

                num = MTJiye::getQueshaoSuitsNum(p);
                if (num > 0 && from->isAlive() && !from->isNude())
                    room->askForDiscard(from, objectName(), num, num, false, true);
            }
        } else {
            num = MTJiye::getQueshaoSuitsNum(p);
            from->drawCards(num, objectName());
            if (from->isAlive())
                from->turnOver();
        }
        return false;
    }
};


class MTYanyi : public TriggerSkillV2
{
public:
    MTYanyi() : TriggerSkillV2("mtyanyi")
    {
        events << EventPhaseChanging;
        global = true;
        frequency = Compulsory;
    }

    // The phase is replaced after other phase-change handling, as the retired skill did.
    bool usesEventPriority() const override
    {
        return true;
    }

    int getPriority(TriggerEvent) const override
    {
        return -1;
    }

    static bool countsPhase(ServerPlayer *player, const QVariant &data)
    {
        if (!player || !player->isAlive() || !player->hasSkill("mtyanyi") || player->getMaxHp() <= 0
            || player->getPhase() == Player::NotActive) return false;
        const Player::Phase phase = data.value<PhaseChangeStruct>().to;
        return !player->isSkipped(phase) && phase != Player::NotActive && phase != Player::RoundStart;
    }

    // Every entered phase counts once, whichever instance later replaces it.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (countsPhase(player, data))
            room->addPlayerMark(player, "mtyanyi_phase-Clear");
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return countsPhase(player, data) && player->getMark("mtyanyi_phase-Clear") == player->getMaxHp()
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        PhaseChangeStruct change = ctx.original_data->value<PhaseChangeStruct>();

        QStringList phases;
        phases << "roundstart" << "start" << "judge" << "draw" << "play" << "discard" << "finish" << "notactive";

        LogMessage log;
        log.type = "#MTYanyiPhase";
        log.from = player;
        log.arg = objectName();
        log.arg2 = phases.at(int(change.to));
        log.arg3 = "play";
        room->sendLog(log);
        room->notifySkillInvoked(player, objectName());
        player->peiyin(this);

        change.to = Player::Play;
        *ctx.original_data = QVariant::fromValue(change);
        return false;
    }
};

class MTJishiVS : public ViewAsSkillV2
{
public:
    MTJishiVS() : ViewAsSkillV2("mtjishi")
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("MTJishiCard") && request.initiator->getKingdom() == "wei";
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets,
                         const Player *to_select) const override
    {
        Slash slash(Card::NoSuit, 0);
        slash.setSkillName("_mtjishi");
        slash.setFlags("mtjishi_user_" + request.initiator->objectName());
        return slash.targetFilter(targets, to_select, request.initiator)
            && !request.initiator->isProhibited(to_select, &slash, targets);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return !targets.isEmpty();
    }

    TargetEffectMode targetEffectMode() const override
    {
        return WholeTargetGroup;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "MTJishiCard";
    }

    // The dialog declares whether health or maximum health is lost.
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (request.userString != "hp" && request.userString != "maxhp") return nullptr;
        return ViewAsSkillV2::createCard(request);
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator) return false;
        if (request.userString == "hp")
            room->loseHp(HpLostStruct(ctx.initiator, 1, "mtjishi", ctx.initiator));
        else if (request.userString == "maxhp")
            room->loseMaxHp(ctx.initiator, 1, "mtjishi");
        else
            return false;
        return true;
    }

    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || source->isDead()) return FinishSkill;
        Room *room = source->getRoom();

        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_mtjishi");
        room->setCardFlag(slash, "mtjishi_user_" + source->objectName());
        slash->deleteLater();

        if (source->isLocked(slash)) return FinishSkill;

        QList<ServerPlayer *> tos;
        foreach (ServerPlayer *p, targets) {
            if (source->canSlash(p, slash, false))
                tos << p;
        }
        if (tos.isEmpty()) return FinishSkill;

        room->setCardFlag(slash, "SlashIgnoreArmor");
        room->useCard(CardUseStruct(slash, source, tos));
        return FinishSkill;
    }
};

class MTJishi : public TriggerSkillV2
{
public:
    MTJishi() : TriggerSkillV2("mtjishi")
    {
        events << Damage;
        view_as_skill = new MTJishiVS;
        waked_skills = "#mtjishi";
    }

    SkillDialogInfo getDialogInfo() const override
    {
        return SkillDialogInfo::tiansuan("mtjishi", "hp,maxhp");
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.from != player || !damage.card || !damage.card->isKindOf("Slash") || !damage.by_user) return TriggerList();
        if (damage.card->getSkillName() != objectName() && !damage.card->hasFlag("mtjishi_used_slash")) return TriggerList();
        if (!damage.card->hasFlag("mtjishi_user_" + player->objectName())) return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *user, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(user, this);
        user->drawCards(1, objectName());
        if (user->isDead()) return false;

        QStringList choices;
        choices << "add";
        if (user->isWounded())
            choices << "recover";
        QString choice = room->askForChoice(user, objectName(), choices.join("+"));
        if (choice == "add")
            room->gainMaxHp(user, 1, objectName());
        else
            room->recover(user, RecoverStruct(objectName(), user, 1));
        return false;
    }
};

class MTJishiTargetMod : public TargetModSkillV2
{
public:
    MTJishiTargetMod() : TargetModSkillV2("#mtjishi")
    {
        frequency = NotFrequent;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if ((ctx.modType == TargetModSkill::Residue || ctx.modType == TargetModSkill::DistanceLimit)
            && ctx.primary && ctx.card && ctx.card->hasFlag("mtjishi_user_" + ctx.primary->objectName())
            && (ctx.card->getSkillName() == "mtjishi" || ctx.card->hasFlag("mtjishi_used_slash")))
            return CorrectSkillResult::useAmount(1000);
        return CorrectSkillResult::noEffect();
    }
};

class MTYitaoVS : public ViewAsSkillV2
{
public:
    MTYitaoVS() : ViewAsSkillV2("mtyitao")
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("MTYitaoCard") && request.initiator->getKingdom() == "wu";
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets,
                         const Player *to_select) const override
    {
        Duel duel(Card::NoSuit, 0);
        duel.setSkillName("_mtyitao");
        return duel.targetFilter(targets, to_select, request.initiator)
            && !request.initiator->isProhibited(to_select, &duel, targets);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return !targets.isEmpty();
    }

    TargetEffectMode targetEffectMode() const override
    {
        return WholeTargetGroup;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "MTYitaoCard";
    }

    // The dialog declares whether health or maximum health is lost.
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (request.userString != "hp" && request.userString != "maxhp") return nullptr;
        return ViewAsSkillV2::createCard(request);
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator) return false;
        if (request.userString == "hp")
            room->loseHp(HpLostStruct(ctx.initiator, 1, "mtyitao", ctx.initiator));
        else if (request.userString == "maxhp")
            room->loseMaxHp(ctx.initiator, 1, "mtyitao");
        else
            return false;
        return true;
    }

    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || source->isDead()) return FinishSkill;
        Room *room = source->getRoom();

        Duel *duel = new Duel(Card::NoSuit, 0);
        duel->setSkillName("_mtyitao");
        duel->deleteLater();

        if (source->isLocked(duel)) return FinishSkill;

        QList<ServerPlayer *> tos;
        foreach (ServerPlayer *p, targets) {
            if (source->canUse(duel, p, true))
                tos << p;
        }
        if (tos.isEmpty()) return FinishSkill;

        CardUseStruct use;
        use.card = duel;
        use.from = source;
        use.to = tos;
        use.no_offset_list << "_ALL_TARGETS";
        room->useCard(use);
        return FinishSkill;
    }
};

class MTYitao : public TriggerSkillV2
{
public:
    MTYitao() : TriggerSkillV2("mtyitao")
    {
        events << Damage;
        view_as_skill = new MTYitaoVS;
    }

    SkillDialogInfo getDialogInfo() const override
    {
        return SkillDialogInfo::tiansuan("mtyitao", "hp,maxhp");
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.from != player || !damage.card || !damage.card->isKindOf("Duel") || !damage.by_user || !damage.to)
            return TriggerList();
        if (damage.card->getSkillName() != objectName() || room->getCardUser(damage.card) != player) return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *user, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        room->sendCompulsoryTriggerLog(user, this);
        user->drawCards(1, objectName());
        if (user->isDead()) return false;

        int hp = damage.to->getHp(), hand = damage.to->getHandcardNum();
        hp = qMax(1, hp);
        hp = qMin(hp, 6);
        hand = qMax(1, hand);
        hand = qMin(hand, 6);

        QStringList choices;
        choices << QString("hp2=%1=%2").arg(damage.to->objectName()).arg(hp);
        choices << QString("hand=%1=%2").arg(damage.to->objectName()).arg(hand);
        QString choice = room->askForChoice(user, objectName(), choices.join("+"));

        int maxhp = user->getMaxHp();
        if (choice.startsWith("hp2")) {
            //room->setPlayerProperty(user, "maxhp", hp);
            hp = damage.to->getHp();
            hp = qMax(1, hp);
            hp = qMin(hp, 6);
            if (maxhp < hp)
                room->gainMaxHp(user, hp - maxhp, objectName());
            else if (maxhp > hp)
                room->loseMaxHp(user, maxhp - hp, objectName());
         } else {
            //room->setPlayerProperty(user, "maxhp", hand);
            hand = damage.to->getHandcardNum();
            hand = qMax(1, hand);
            hand = qMin(hand, 6);
            if (maxhp < hand)
                room->gainMaxHp(user, hand - maxhp, objectName());
            else if (maxhp > hand)
                room->loseMaxHp(user, maxhp - hand, objectName());
        }
        return false;
    }
};

class MTJuyuan : public TriggerSkillV2
{
public:
    MTJuyuan() : TriggerSkillV2("mtjuyuan")
    {
        events << Dying;
        waked_skills = "#mtjuyuan-flag,#mtjuyuan-prohibit";
    }

    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *who)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (who->inMyAttackRange(p))
                targets << p;
        }
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        ServerPlayer *who = data.value<DyingStruct>().who;
        if (!who || who->isDead() || who == player || candidates(room, who).isEmpty()) return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *who = ctx.original_data->value<DyingStruct>().who;
        QList<ServerPlayer *> targets = candidates(room, who);
        if (targets.isEmpty()) return false;
        targets = room->askForPlayersChosen(player, targets, objectName(), 0, targets.length(), "@mtjuyuan-draw:" + who->objectName());
        if (targets.isEmpty()) return false;
        QStringList names;
        foreach (ServerPlayer *p, targets)
            names << p->objectName();
        ctx.extra_data = names;
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        DyingStruct dying = ctx.original_data->value<DyingStruct>();
        ServerPlayer *who = dying.who;
        QList<ServerPlayer *> targets;
        foreach (QString name, ctx.extra_data.toStringList()) {
            ServerPlayer *p = room->findPlayerByObjectName(name);
            if (p) targets << p;
        }
        player->peiyin(this);
        room->notifySkillInvoked(player, objectName());

        int n = targets.length();

        room->drawCards(targets, 1, objectName());

        foreach (ServerPlayer *p, targets) {
            if (p->isAlive())
                room->setPlayerFlag(p, "mtjuyuanPreventPeach_" + who->objectName());
        }

        // A dying caused by losing health carries no damage to annotate.
        DamageStruct *damage = dying.damage;
        if (!damage) return false;
        damage->tips << "mtjuyuanNum_" + QString::number(n) << "mtjuyuanPlayer_" + player->objectName();
        *ctx.original_data = QVariant::fromValue(dying);
        return false;
    }
};

class MTJuyuanFlag : public TriggerSkillV2
{
public:
    MTJuyuanFlag() : TriggerSkillV2("#mtjuyuan-flag")
    {
        events << AskForPeaches << AskForPeachesDone << QuitDying << EventPhaseChanging;
        frequency = Compulsory;
    }

    // Flag and mark cleanup applies to every player, with or without Juyuan.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == AskForPeachesDone) {
            ServerPlayer *who = data.value<DyingStruct>().who;
            if (!who) return true;
            foreach (ServerPlayer *p, room->getAllPlayers(true)) {
                if (p->hasFlag("mtjuyuanPreventPeach_" + who->objectName()))
                    room->setPlayerFlag(p, "-mtjuyuanPreventPeach_" + who->objectName());
            }
        } else if (event == EventPhaseChanging) {
            if (!player || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
            if (player->getMark("mtjuyuanProhibited-Keep") <= 0) return true;
            room->setPlayerMark(player, "mtjuyuanProhibited-Keep", 0);
            foreach (ServerPlayer *p, room->getAllPlayers(true))
                room->setPlayerMark(p, "mtjuyuanProhibited_" + player->objectName(), 0);
        }
        return true;
    }

    static ServerPlayer *juyuanUser(Room *room, const QVariant &data)
    {
        const DyingStruct dying = data.value<DyingStruct>();
        if (!dying.damage) return nullptr;
        foreach (QString tip, dying.damage->tips) {
            if (tip.startsWith("mtjuyuanPlayer_")) {
                ServerPlayer *from = room->findChild<ServerPlayer *>(tip.split("_").last());
                if (from && from->isAlive()) return from;
            }
        }
        return nullptr;
    }

    static int juyuanNum(const QVariant &data)
    {
        const DyingStruct dying = data.value<DyingStruct>();
        int num = 0;
        if (!dying.damage) return num;
        foreach (QString tip, dying.damage->tips) {
            if (tip.startsWith("mtjuyuanNum_")) {
                int n = tip.split("_").last().toInt();
                if (n > 0) num = n;
            }
        }
        return num;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || player->isDead()) return result;
        if (event == AskForPeaches) {
            // A chosen player may not save the dying character; any Juyuan holder enforces it.
            ServerPlayer *who = data.value<DyingStruct>().who;
            if (!who || !player->hasFlag("mtjuyuanPreventPeach_" + who->objectName())) return result;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->isAlive() && p->hasSkill(objectName())) {
                    result[p] << objectName();
                    break;
                }
            }
        } else if (event == QuitDying) {
            ServerPlayer *from = juyuanUser(room, data);
            if (from && from->hasSkill(objectName()) && juyuanNum(data) > 0)
                result[from] << objectName();
        }
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *from, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (event == AskForPeaches) {
            ServerPlayer *who = ctx.original_data->value<DyingStruct>().who;
            if (!player->hasFlag("mtjuyuanPreventPeach_" + who->objectName())) return false;
            room->setPlayerFlag(player, "-mtjuyuanPreventPeach_" + who->objectName());
            return true;
        }

        const int num = juyuanNum(*ctx.original_data);
        if (num <= 0 || from->isDead()) return false;

        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_mtjuyuan");
        slash->deleteLater();
        try {
            for (int i = 0; i < num; i++) {
                room->setCardFlag(slash, "SlashNoRespond");
                if (player->isDead() || player->isLocked(slash) || from->isDead() || !player->canSlash(from, false))
                    break;
                room->useCard(CardUseStruct(slash, player, from));
            }

            if (player->isAlive()) {//因为描述是“其于其下回合内不能使用牌指定除你与其外的角色为目标”，如果濒死的是孟达自己，这里就是当回合就加了标记而不是下回合，会导致bug
                room->setPlayerMark(player, "mtjuyuanProhibited-Keep", 1);
                if (from->isAlive())
                    room->setPlayerMark(from, "mtjuyuanProhibited_" + player->objectName(), 1);
            }
        }
        catch (TriggerEvent triggerEvent) {
            if (triggerEvent == TurnBroken || triggerEvent == StageChange) {
                if (player->isAlive()) {   //因为描述是“其于其下回合内不能使用牌指定除你与其外的角色为目标”，如果濒死的是孟达自己，就是当回合不是下回合
                    room->setPlayerMark(player, "mtjuyuanProhibited-Keep", 1);
                    if (from->isAlive())
                        room->setPlayerMark(from, "mtjuyuanProhibited_" + player->objectName(), 1);
                }
            }
        }
        return false;
    }
};

class MTJuyuanProhibit : public ProhibitSkill
{
public:
    MTJuyuanProhibit() : ProhibitSkill("#mtjuyuan-prohibit")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        if (from->getPhase() == Player::NotActive || from->getMark("mtjuyuanProhibited-Keep") <= 0 || card->isKindOf("SkillCard")) return false;
        return from != to && to->getMark("mtjuyuanProhibited_" + from->objectName()) <= 0;
    }
};

class MTFupan : public TriggerSkillV2
{
public:
    MTFupan() : TriggerSkillV2("mtfupan")
    {
        events << Damaged;
        frequency = Compulsory;
    }

    void MoveAndChange(Room *room, ServerPlayer *target, ServerPlayer *to, QList<int> ids) const
    {
        LogMessage log;
        log.type = "#ChoosePlayerWithSkill";
        log.from = target;
        log.to << to;
        log.arg = objectName();
        room->sendLog(log);
        room->doAnimate(1, target->objectName(), to->objectName());
        room->notifySkillInvoked(target, objectName());
        target->peiyin(this);

        room->giveCard(target, to, ids, objectName());
        if (target->isAlive()) {
            if (!room->canMoveField("ej")) return;
            room->moveField(target, objectName(), false, "ej");
            if (target->isDead() || target == to) return;
            room->changeKingdom(target, "wei");
        }
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *target, QVariant &data) const override
    {
        if (!target || !target->isAlive() || !target->hasSkill(objectName())) return TriggerList();
        ServerPlayer *from = data.value<DamageStruct>().from;
        bool eligible = false;
        if (target->getKingdom() == "wei") {
            eligible = from && from->isAlive() && !from->isAllNude();
        } else if (target->getKingdom() == "shu" && !target->isKongcheng()) {
            QList<ServerPlayer *> targets = room->getOtherPlayers(target);
            if (from) targets.removeOne(from);
            eligible = !targets.isEmpty();
        }
        return eligible ? TriggerList{{target, QStringList{objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *target, SkillContext &ctx) const override
    {
        QString kim = target->getKingdom();
        ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;

        if (kim == "wei") {
            if (!from || from->isDead() || from->isAllNude()) return false;
            DummyCard *dummy = new DummyCard;
            dummy->deleteLater();
            QStringList areas;
            areas << "h" << "e" << "j";

            for (int i = 0; i < 3; i++) {
                if (!from || from->isDead() || from->getCards(areas.at(i)).isEmpty() || target->isDead()) break;
                int id = room->askForCardChosen(target, from, areas.at(i), objectName());
                if (id > 0)
                    dummy->addSubcard(id);
            }
            if (target->isDead() || dummy->subcardsLength() <= 0) return false;
            room->obtainCard(target, dummy, objectName());

            if (target->isDead()) return false;
            QList<int> ids, hands = target->handCards();
            foreach (int id, dummy->getSubcards()) {
                if (!hands.contains(id)) continue;
                ids << id;
            }
            if (ids.isEmpty()) return false;
            QList<ServerPlayer *> targets = room->getAllPlayers();
            targets.removeOne(from);
            if (targets.isEmpty()) return false;

            room->fillAG(ids, target);
            ServerPlayer *to = room->askForPlayerChosen(target, targets, objectName(), "@mtfupan-give:" + from->objectName());
            room->clearAG(target);
            if (target != to)
                room->giveCard(target, to, dummy, objectName());

            if (target->isAlive() && to != target)
                room->changeKingdom(target, "shu");
        } else if (kim == "shu") {
            if (target->isKongcheng()) return false;
            QList<ServerPlayer *> targets = room->getOtherPlayers(target);
            if (from)
                targets.removeOne(from);
            if (targets.isEmpty()) return false;

            QList<int> handcards = target->handCards();
            CardsMoveStruct move = room->askForYijiStruct(target, handcards, objectName(), false, false, false, 999, targets,
                            CardMoveReason(), from ? "@mtfupan-give2:" + from->objectName() : "@mtfupan-give3", false, false);

            if (move.to && !move.card_ids.isEmpty())
                MoveAndChange(room, target, (ServerPlayer *)move.to, move.card_ids);
            else {
                int id = target->getRandomHandCardId();
                ServerPlayer * to = targets.at(qsanRandomBounded(targets.length()));
                MoveAndChange(room, target, to, QList<int>() << id);
            }
        }
        return false;
    }
};

class MTFeiyan : public TriggerSkillV2
{
public:
    MTFeiyan() : TriggerSkillV2("mtfeiyan")
    {
        events << TargetSpecified;
        waked_skills = "#mtfeiyan";
    }

    // Returns the target and the X of the description, or nullptr when Feiyan does not apply.
    static ServerPlayer *feiyanTarget(ServerPlayer *player, const QVariant &data, int *num)
    {
        if (player->getPhase() != Player::Play || player->getMark("mtfeiyan-PlayClear") > 0) return nullptr;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.to.length() != 1) return nullptr;
        ServerPlayer *to = use.to.first();
        if (to == player || !(use.card->isKindOf("Slash") || use.card->isNDTrick())) return nullptr;

        int from_num = player->getEquips().length(), to_num = to->getEquips().length();
        if (from_num > to_num) return nullptr;
        if (num) *num = qMax(1, to_num - from_num);
        return to;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        ServerPlayer *to = feiyanTarget(player, data, nullptr);
        const QString kim = player->getKingdom();
        if (!to || !(kim == "wei" || (kim == "qun" && !to->isNude()))) return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        int num = 0;
        ServerPlayer *to = feiyanTarget(player, *ctx.original_data, &num);
        if (!to) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (player->getKingdom() == "wei")
            return player->askForSkillInvoke(this, QString("wei:%1::%2:%3").arg(to->objectName()).arg(use.card->objectName()).arg(num));
        return !to->isNude() && player->askForSkillInvoke(this, QString("qun:%1::%2").arg(to->objectName()).arg(num));
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        int num = 0;
        ServerPlayer *to = feiyanTarget(player, *ctx.original_data, &num);
        if (!to) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (player->getKingdom() == "wei") {
            player->peiyin(this);
            room->setCardFlag(use.card, QString("mtfeiyanEffect_%1_%2_%3").arg(player->objectName()).arg(to->objectName()).arg(num));
            room->addPlayerMark(player, "mtfeiyan-PlayClear");

            LogMessage log;
            log.type = "#TongliTimes";
            log.card_str = use.card->toString();
            log.arg = QString::number(num);
            log.to = use.to;
            room->sendLog(log);

            if (num > 2 && player->isAlive())
                player->setTag("mtfeiyan", "qun");  //不考虑如果发动多次“飞燕”，应该在出牌阶段结束时多次触发效果了
        } else {
            player->peiyin(this);
            room->addPlayerMark(player, "mtfeiyan-PlayClear");

            DummyCard *dummy = new DummyCard;
            dummy->deleteLater();
            for (int i = 0; i < num; i++) {
                int id = room->askForCardChosen(player, to, "he", objectName(), false, Card::MethodNone, dummy->getSubcards());
                if (id < 0) break; //id<0应该随机获得一张满足条件的牌，这里偷懒
                dummy->addSubcard(id);
            }
            if (dummy->subcardsLength() <= 0) return false;
            room->obtainCard(player, dummy, objectName());

            if (num > 2 && player->isAlive())
                player->setTag("mtfeiyan", "wei");  //不考虑如果发动多次“飞燕”，应该在出牌阶段结束时多次触发效果了
        }
        return false;
    }
};

class MTFeiyanEffect : public TriggerSkillV2
{
public:
    MTFeiyanEffect() : TriggerSkillV2("#mtfeiyan")
    {
        events << EventPhaseEnd << CardFinished << DamageInflicted;
        frequency = Compulsory;
    }

    // The user who set the repeat flag, with its target and repeat count.
    static ServerPlayer *repeatUser(Room *room, const Card *card, ServerPlayer **to, int *num)
    {
        ServerPlayer *from = nullptr;
        *to = nullptr;
        *num = 0;
        foreach (QString flag, card->getFlags()) {
            if (!flag.startsWith("mtfeiyanEffect")) continue;
            QStringList flags = flag.split("_");
            if (flags.length() != 4) continue;
            from = room->findChild<ServerPlayer *>(flags.at(1));
            *to = room->findChild<ServerPlayer *>(flags.at(2));
            *num = flags.last().toInt();

            if (from && from->isAlive() && *to && (*to)->isAlive() && *num > 0) break;
        }
        if (!from || from->isDead() || !*to || (*to)->isDead() || *num <= 0) return nullptr;
        return from;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive()) return TriggerList();
        ServerPlayer *owner = nullptr;
        if (event == EventPhaseEnd) {
            if (player->getPhase() == Player::Play && !player->getTag("mtfeiyan").toString().isEmpty())
                owner = player;
        } else if (event == DamageInflicted) {
            if (player->getMark("&mtfeiyanDamage") > 0)
                owner = player;
        } else {
            const Card *card = data.value<CardUseStruct>().card;
            ServerPlayer *to = nullptr;
            int num = 0;
            if (card && (card->isKindOf("Slash") || card->isNDTrick()))
                owner = repeatUser(room, card, &to, &num);
        }
        return owner && owner->hasSkill(objectName()) ? TriggerList{{owner, QStringList{objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        if (event == EventPhaseEnd) {
            QString kim = player->getTag("mtfeiyan").toString();
            player->removeTag("mtfeiyan");
            /*if (kim == "qun" || kim == "wei")
                room->changeKingdom(player, kim);
            else {
                if (player->getKingdom() == "qun")
                    room->changeKingdom(player, "wei");
                else if (player->getKingdom() == "wei")
                    room->changeKingdom(player, "qun");
                else
                    room->changeKingdom(player, "wei");
            }*/
            if (kim == "qun" || kim == "wei") {
                LogMessage log;
                log.type = "#TriggerSkill";
                log.from = player;
                log.arg = "mtfeiyan";
                room->sendLog(log);
                player->peiyin("mtfeiyan");
                room->notifySkillInvoked(player, "mtfeiyan");
                room->changeKingdom(player, kim);

                room->addPlayerMark(player, "&mtfeiyanDamage");
            }
        } else if (event == DamageInflicted) {
            int n = player->getMark("&mtfeiyanDamage");
            if (n <= 0) return false;
            room->setPlayerMark(player, "&mtfeiyanDamage", 0);

            DamageStruct damage = data.value<DamageStruct>();
            int a = damage.damage, b = a + n;

            damage.damage = b;
            data = QVariant::fromValue(damage);

            LogMessage log;
            log.type = "#MTFeiyanDamage";
            log.from = player;
            log.arg = "mtfeiyan";
            log.arg2 = QString::number(a);
            log.arg3 = QString::number(b);
            room->sendLog(log);
            player->peiyin("mtfeiyan");
            room->notifySkillInvoked(player, "mtfeiyan");
        } else {
            const Card *card = data.value<CardUseStruct>().card;
            ServerPlayer *to = nullptr;
            int num = 0;
            ServerPlayer *from = repeatUser(room, card, &to, &num);
            if (!from) return false;

            for (int i = 0; i < num; i++) {
                if (from->isDead() || to->isDead()) break;
                card->use(room, from, QList<ServerPlayer *>() << to);
            }
        }
        return false;
    }
};

class MTJiukuang : public TriggerSkillV2
{
public:
    MTJiukuang() : TriggerSkillV2("mtjiukuang")
    {
        events << CardFinished;
        frequency = Compulsory;
        waked_skills = "#mtjiukuang";
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = data.value<CardUseStruct>().card;
        return player && player->isAlive() && player->hasSkill(objectName()) && card && card->isKindOf("Analeptic")
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, this);

        if (player->getLostHp() > 0)
            room->recover(player, RecoverStruct(objectName(), player, 1));
        if (player->isAlive())
            player->drawCards(1, objectName());
        return false;
    }
};

class MTJiukuangTMD : public TargetModSkillV2
{
public:
    MTJiukuangTMD() : TargetModSkillV2("#mtjiukuang", "Analeptic")
    {
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType == TargetModSkill::Residue && ctx.primary
            && ctx.primary->getPhase() == Player::Play && ctx.primary->hasSkill("mtjiukuang"))
            return CorrectSkillResult::useAmount(10000);
        return CorrectSkillResult::noEffect();
    }
};

class MTZongqingVS : public ViewAsSkillV2
{
public:
    MTZongqingVS() : ViewAsSkillV2("mtzongqing")
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("MTZongqingCard") && !request.initiator->isKongcheng();
    }

    TargetMode targetMode() const override
    {
        return NoTarget;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "MTZongqingCard";
    }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || source->isDead() || source->isKongcheng()) return FinishSkill;
        Room *room = source->getRoom();

        QList<int> types;
        QList<Card::Suit> suits;
        bool damage = false;
        foreach (int id, source->handCards()) {
            if (!source->canDiscard(source, id)) continue;
            const Card *c = Sanguosha->getCard(id);
            int type = c->getTypeId();
            Card::Suit suit = c->getSuit();
            if (!types.contains(type))
                types << type;
            if (!suits.contains(suit))
                suits << suit;
            if (c->isDamageCard())
                damage = true;
        }

        source->throwAllHandCards();
        if (source->isDead()) return FinishSkill;
        source->drawCards(1, "mtzongqing");
        if (source->isDead()) return FinishSkill;

        if (types.length() >= 2) {
            Analeptic *ana = new Analeptic(Card::NoSuit, 0);
            ana->setSkillName("_mtzongqing");
            ana->deleteLater();
            if (source->canUse(ana, source, true))
                room->useCard(CardUseStruct(ana, source, source));
        }
        if (suits.length() >= 2 && source->isAlive()) {
            ServerPlayer *to = room->askForPlayerChosen(source, room->getAllPlayers(), "mtzongqing", "@mtzongqing-draw");
            room->doAnimate(1, source->objectName(), to->objectName());
            to->drawCards(2, "mtzongqing");
        }
        if (damage && source->isAlive())
            room->addPlayerMark(source, "&mtzongqing");
            //room->addDistance(source, 1, false, false);
        return FinishSkill;
    }
};

class MTZongqing : public TriggerSkillV2
{
public:
    MTZongqing() : TriggerSkillV2("mtzongqing")
    {
        events << EventPhaseStart;
        view_as_skill = new MTZongqingVS;
        waked_skills = "#mtzongqing";
    }

    // The distance mark lasts until the holder's next turn, even without Zongqing.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (player && player->isAlive() && player->getPhase() == Player::RoundStart && player->getMark("&mtzongqing") > 0)
            room->setPlayerMark(player, "&mtzongqing", 0);
        return true;
    }
};

class MTZongqingDis : public DistanceSkillV2
{
public:
    MTZongqingDis() : DistanceSkillV2("#mtzongqing")
    {
        frequency = NotCompulsory;
        setHolderSelector(CorrectSkill_Secondary);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const int mark = ctx.secondary ? ctx.secondary->getMark("&mtzongqing") : 0;
        return mark > 0 ? CorrectSkillResult::useAmount(mark) : CorrectSkillResult::noEffect();
    }
};

class MTZanzhangVS : public ViewAsSkillV2
{
public:
    MTZanzhangVS() : ViewAsSkillV2("mtzanzhang")
    {
    }

    static int excess(const Player *player)
    {
        return player->getHandcardNum() - player->getHp();
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || excess(request.initiator) == 0) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return !request.initiator->hasUsed("MTZanzhangCard");
        return isPromptRequest(request, "@@mtzanzhang");
    }

    // With more hand cards than health, give up to the excess; otherwise take one from the field.
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const int x = excess(request.initiator);
        return x > 0 && matchesFilter(request, card, ".|.|.|hand") && request.selectedCardIds.length() < x;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        const int x = excess(request.initiator);
        if (x < 0) return request.selectedCardIds.isEmpty();
        return x > 0 && !request.selectedCardIds.isEmpty() && replaySelection(this, request);
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets,
                         const Player *to_select) const override
    {
        if (!targets.isEmpty() || !to_select) return false;
        const int x = excess(request.initiator);
        if (x > 0)
            return to_select != request.initiator;
        return x < 0 && !(to_select->getEquips().isEmpty() && to_select->getJudgingArea().isEmpty());
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.length() == 1;
    }

    bool willThrowSelectedCards() const override
    {
        return false;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "MTZanzhangCard";
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || source->isDead() || !ctx.use_card) return ContinueEffects;
        Room *room = source->getRoom();
        const QList<int> ids = ctx.use_card->getSubcards();
        if (ids.isEmpty()) {
            if (target->getEquips().isEmpty() && target->getJudgingArea().isEmpty()) return ContinueEffects;
            int id = room->askForCardChosen(source, target, "ej", "mtzanzhang");
            room->obtainCard(source, id, "mtzanzhang");
        } else
            room->giveCard(source, target, ids, "mtzanzhang");
        return ContinueEffects;
    }
};

class MTZanzhang : public TriggerSkillV2
{
public:
    MTZanzhang() : TriggerSkillV2("mtzanzhang")
    {
        events << EventPhaseStart;
        view_as_skill = new MTZanzhangVS;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *target, QVariant &) const override
    {
        if (!target || !target->isAlive() || !target->hasSkill(objectName()) || target->getPhase() != Player::Finish)
            return TriggerList();
        const int x = target->getHandcardNum() - target->getHp();
        if (x == 0 || (x > 0 && target->isKongcheng())) return TriggerList();
        return TriggerList{{target, QStringList{objectName()}}};
    }

    // The Zanzhang card is the invocation; declining it never invoked the skill.
    bool cost(TriggerEvent, Room *room, ServerPlayer *target, SkillContext &) const override
    {
        const int x = target->getHandcardNum() - target->getHp();
        if (x == 0) return false;
        QString pro = x > 0 ? "@mtzanzhang-give:" + QString::number(x) : "@mtzanzhang-get";
        return room->askForUseCard(target, "@@mtzanzhang", pro, -1, Card::MethodNone);
    }
};

class MTHongya : public TriggerSkillV2
{
public:
    MTHongya() : TriggerSkillV2("mthongya")
    {
        events << CardsMoveOneTime;
    }

    static QString phaseMark(Room *room)
    {
        ServerPlayer *current = room->getCurrent();
        if (!current || current->isDead() || current->getPhase() == Player::NotActive || current->getPhase() == Player::PhaseNone)
            return QString();
        return QString("mthongya-%1Clear").arg(current->getPhase());
    }

    static QList<ServerPlayer *> wuTargets(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (player->canDiscard(p, "he"))
                targets << p;
        }
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        const QString mark = phaseMark(room);
        if (mark.isEmpty() || player->getMark(mark) > 0) return TriggerList();

        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from == move.to) return TriggerList();

        bool flag = false;
        if (move.from == player && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))) {
            if (move.reason.m_reason == CardMoveReason::S_REASON_GIVE)
                flag = true;
        } else if (move.from && move.to == player) {
            if (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip) {
                if (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
                    flag = true;
            }
        }
        if (!flag) return TriggerList();

        if (player->getKingdom() == "wu") {
            if (!player->canDiscard(player, "he") || wuTargets(room, player).isEmpty()) return TriggerList();
        } else if (player->getKingdom() != "shu") {
            return TriggerList();
        }
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *t = nullptr;
        if (player->getKingdom() == "wu")
            t = room->askForPlayerChosen(player, wuTargets(room, player), objectName(), "@mthongya-wu", true, true);
        else
            t = room->askForPlayerChosen(player, room->getAllPlayers(), objectName(), "@mthongya-shu", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        ctx.choice = player->getKingdom();
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *t) const override
    {
        player->peiyin(this);
        const QString mark = phaseMark(room);
        if (!mark.isEmpty())
            room->setPlayerMark(player, mark, 1);

        if (ctx.choice == "shu") {
            t->drawCards(1, objectName());
            return false;
        }

        QList<CardsMoveStruct> moves;
        if (player->isAlive() && player->canDiscard(player, "he")) {
            int id = room->askForCardChosen(player, player, "he", objectName(), true, Card::MethodDiscard);
            QList<int> ids;
            ids << id;
            CardMoveReason reason(CardMoveReason::S_REASON_THROW, player->objectName(), objectName(), "");
            LogMessage log;
            log.from = player;
            log.type = "$DiscardCard";
            log.card_str = ListI2S(ids).join("+");
            room->sendLog(log);
            CardsMoveStruct move(ids, NULL, Player::DiscardPile, reason);
            moves << move;
        }
        if (player->isAlive() && t->isAlive() && player->canDiscard(t, "he")) {
            int id = room->askForCardChosen(player, t, "he", objectName(), false, Card::MethodDiscard);
            QList<int> ids;
            ids << id;
            CardMoveReason reason(CardMoveReason::S_REASON_DISMANTLE, player->objectName(), t->objectName(), objectName(), "");
            LogMessage log;
            log.from = player;
            log.type = "$DiscardCardByOther";
            log.card_str = ListI2S(ids).join("+");
            log.to << t;
            room->sendLog(log);
            CardsMoveStruct move(ids, NULL, Player::DiscardPile, reason);
            moves << move;
        }
        room->moveCardsAtomic(moves, true);
        return false;
    }
};

MTYinglveCard::MTYinglveCard()
{
    mute = true;
}

// The card recorded in mtyinglve_cardID, cloned so it can only be used (not
// recast). Returns nullptr when there is no record or the room lacks the card.
static Card *cloneMTYinglveCard(const Player *player)
{
    if (!player) return nullptr;
    int id = player->getMark("mtyinglve_cardID") - 1;
    if (id < 0) return nullptr;
    const Card *c = Sanguosha->getCard(id);
    if (!c) return nullptr;
    Card *card = Sanguosha->cloneCard(c);
    if (!card) return nullptr;
    card->addSubcard(c);
    card->setCanRecast(false);
    card->deleteLater();
    return card;
}

bool MTYinglveCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Card *card = cloneMTYinglveCard(Self);
    return card && card->targetFilter(targets, to_select, Self);
}

bool MTYinglveCard::targetFixed() const
{
    // The server has no engine Self. Report not-fixed there so
    // Room::areCardTargetsLegal checks the use through the player-aware
    // targetFilter/targetsFeasible, which handle target-fixed cards as well.
    Card *card = cloneMTYinglveCard(Self);
    return card && card->targetFixed();
}

bool MTYinglveCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    Card *card = cloneMTYinglveCard(Self);
    return card && card->targetsFeasible(targets, Self);
}

void MTYinglveCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *from = card_use.from;
    int id = from->getMark("mtyinglve_cardID") - 1;
    if (id < 0) return;
    const Card *c = Sanguosha->getCard(id);
    if (!c) return;
    room->useCard(CardUseStruct(c, from, card_use.to));
    from->drawCards(1, "mtyinglve");
}

// Stays a legacy view-as skill: the chosen target, who does not own Yinglve, answers @@mtyinglve.
class MTYinglveVS : public ZeroCardViewAsSkill
{
public:
    MTYinglveVS() : ZeroCardViewAsSkill("mtyinglve")
    {
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern == "@@mtyinglve";
    }

    const Card *viewAs() const
    {
        int id = Self->getMark("mtyinglve_cardID") - 1;
        if (id < 0) return NULL;
        const Card *card = Sanguosha->getCard(id);
        if (!card) return NULL;
        //return card;  这样的话，【铁索连环】可以重铸，而不是描述说的只能使用
        return new MTYinglveCard;
    }
};

class MTYinglve : public TriggerSkillV2
{
public:
    MTYinglve() : TriggerSkillV2("mtyinglve")
    {
        events << EventPhaseStart;
        view_as_skill = new MTYinglveVS;
    }

    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!p->isKongcheng())
                targets << p;
        }
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
            && !candidates(room, player).isEmpty()
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *t = room->askForPlayerChosen(player, candidates(room, player), objectName(), "@mtyinglve-target", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &, ServerPlayer *t) const override
    {
        player->peiyin(this);

        QList<int> hands = t->handCards();
        if (!hands.isEmpty()) {
            int id = room->doGongxin(player, t, hands, objectName());
            if (id < 0) id = t->getRandomHandCardId();
            room->showCard(t, id);

            const Card *card = Sanguosha->getCard(id);

            room->setPlayerMark(t, "mtyinglve_cardID", id + 1);
            if (!card->isAvailable(t)  || t->isLocked(card, false) ||
                    !room->askForUseCard(t, "@@mtyinglve", "@mtyinglve:" + card->objectName())) {
                QList<int> ids;
                foreach (int card_id, t->handCards()) {
                    if (card_id == id) continue;
                    ids << card_id;
                }
                if (ids.isEmpty()) return false;

                DummyCard *dummy = new DummyCard(ids);
                dummy->deleteLater();

                LogMessage log;
                log.type = "$RecastCard";
                log.from = t;
                log.card_str = ListI2S(ids).join("+");
                room->sendLog(log);

                room->moveCardTo(dummy, t, NULL, Player::DiscardPile,
                          CardMoveReason(CardMoveReason::S_REASON_RECAST, t->objectName(), objectName(), ""));
                t->drawCards(ids.length(), "recast");
            }
        }
        return false;
    }
};

class MTXianding : public TriggerSkillV2
{
public:
    MTXianding() : TriggerSkillV2("mtxianding")
    {
        events << DamageCaused;
        waked_skills = "#mtxianding";
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->isDead()) return result;
        ServerPlayer *current = room->getCurrent();
        if (!current || current->isDead() || current->getPhase() == Player::NotActive
            || current->getMark("mtxiandingUseCard-Clear") > 1) return result;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isAlive() && p->hasSkill(objectName()))
                result[p] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *p, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!player || player->isDead()) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        return p->askForSkillInvoke(this, QString("damage:%1:%2:%3").arg(player->objectName())
            .arg(damage.to->objectName()).arg(damage.damage + 1));
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *p, SkillContext &ctx) const override
    {
        p->peiyin(this);
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        damage.damage++;
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

class MTXiandingRecord : public TriggerSkillV2
{
public:
    MTXiandingRecord() : TriggerSkillV2("#mtxianding")
    {
        events << PreCardUsed;
        global = true;
    }

    // Counts the current player's cards and marks the second one for Bishi.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *current = room->getCurrent();
        if (!current || current->isDead() || current != player || current->getPhase() == Player::NotActive) return true;
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->isKindOf("SkillCard")) return true;
        room->addPlayerMark(player, "mtxiandingUseCard-Clear");
        int mark = player->getMark("mtxiandingUseCard-Clear");
        if (mark == 2)
            room->setCardFlag(use.card, "mtbishiSecond");
        return true;
    }
};

class MTWangheVS : public ViewAsSkillV2
{
public:
    MTWangheVS() : ViewAsSkillV2("mtwanghe")
    {
    }

    static QString chosenName(const ActiveSkillRequest &request)
    {
        const int id = request.initiator ? request.initiator->getMark("mtwanghe_id") - 1 : -1;
        return id < 0 ? QString() : Sanguosha->getEngineCard(id)->objectName();
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@mtwanghe!") && !chosenName(request).isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &request) const override
    {
        return cardHistoryKey(chosenName(request), objectName());
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const QString name = chosenName(request);
        Card *c = name.isEmpty() ? nullptr : Sanguosha->cloneCard(name);
        if (c) c->setSkillName(objectName());
        return c;
    }
};

class MTWanghe : public TriggerSkillV2
{
public:
    MTWanghe() : TriggerSkillV2("mtwanghe")
    {
        events << Damaged << CardsMoveOneTime << Death;
        view_as_skill = new MTWangheVS;
    }

    static QString eventName(TriggerEvent event)
    {
        if (event == Damaged) return "damaged";
        if (event == CardsMoveOneTime) return "cardsmove";
        return "death";
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        if (event == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from == move.to || move.from != player) return TriggerList();
            int num = 0;
            for (int i = 0; i < move.card_ids.length(); i++) {
                if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip)
                    num++;
            }
            if (num < 2) return TriggerList();
        }
        if (player->getMark(QString("mtwangheDelete_%1-Keep").arg(eventName(event))) > 0) return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        const QString e = eventName(event);
        QString delete_mark = QString("mtwangheDelete_%1-Keep").arg(e);
        if (player->getMark(delete_mark) > 0) return false;

        QString mark = QString("mtwangheEvent_%1_lun").arg(e);

        // A second trigger by the same timing in one round deletes that timing.
        if (player->getMark(mark) > 0) {
            QStringList records = player->property("mtwangheEvent_Delete").toStringList();
            if (records.contains("mtwanghe:" + e)) return false;
            records << "mtwanghe:" + e << ",";
            room->setPlayerProperty(player, "mtwangheEvent_Delete", records.join("+"));
            player->setSkillDescriptionSwap(objectName(), "%arg1", records.join("+"));
            room->changeTranslation(player, objectName(), 1);
            room->setPlayerMark(player, delete_mark, 1);

            LogMessage log;
            log.type = "#MTWangheDelete";
            log.from = player;
            log.arg = objectName();
            log.arg2 = "mtwanghe:" + e;
            room->sendLog(log);
            player->peiyin(this);
            room->notifySkillInvoked(player, objectName());
            return false;
        }

        QList<int> list = room->getAvailableCardList(player, "basic", objectName());
        if (list.isEmpty()) return false;
        room->fillAG(list, player);
        int id = room->askForAG(player, list, true, objectName(), "@mtwanghe-basic");
        room->clearAG(player);
        if (id < 0) return false;

        QString name = Sanguosha->getEngineCard(id)->objectName();
        room->setPlayerMark(player, "mtwanghe_id", id + 1);
        Card *card = Sanguosha->cloneCard(name);
        if (!card) return false;
        card->deleteLater();
        card->setSkillName("mtwanghe");

        room->setPlayerMark(player, mark, 1);
        if (card->targetFixed())
            room->useCard(CardUseStruct(card, player), true);
        else {
            if (!room->askForUseCard(player, "@@mtwanghe!", "@mtwanghe:" + name)) {
                QList<ServerPlayer *> targets = room->getCardTargets(player, card);
                if (targets.isEmpty()) return false;
                ServerPlayer *to = targets.at(qsanRandomBounded(targets.length()));
                room->useCard(CardUseStruct(card, player, to), true);
            }
        }
        return false;
    }
};

class MTChunzu : public TriggerSkillV2
{
public:
    MTChunzu() : TriggerSkillV2("mtchunzu")
    {
        events << BuryVictim << DrawNCards;
        frequency = Limited;
        limit_mark = "@mtchunzuMark";
        waked_skills = "#mtchunzu";
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player) return result;
        if (event == DrawNCards) {
            DrawStruct draw = data.value<DrawStruct>();
            if (player->isAlive() && player->hasSkill(objectName()) && player->getMark("mtchunzuUsed") > 0
                && draw.reason != "InitialHandCards" && draw.num >= 1)
                result[player] << objectName();
            return result;
        }
        DeathStruct de = data.value<DeathStruct>();
        if (!de.who || de.who->getRole() != "rebel") return result;
        foreach (ServerPlayer *p, room->getOtherPlayers(de.who)) {
            if (p->isAlive() && p->hasSkill(objectName()) && p->getMark("@mtchunzuMark") > 0)
                result[p] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *, ServerPlayer *p, SkillContext &) const override
    {
        if (event == DrawNCards) return true;
        return p->getMark("@mtchunzuMark") > 0 && p->askForSkillInvoke(this);
    }

    bool pay(TriggerEvent event, Room *room, ServerPlayer *p, SkillContext &) const override
    {
        if (event == DrawNCards) return true;
        if (p->getMark("@mtchunzuMark") <= 0) return false;
        room->removePlayerMark(p, "@mtchunzuMark");
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == BuryVictim) {
            player->peiyin(this);
            room->doSuperLightbox(player, "mtchunzu");
            room->setPlayerMark(player, "mtchunzuUsed", 1);
            int x = player->getMaxHp() - player->getHp();
            if (x > 0)
                room->recover(player, RecoverStruct(objectName(), player, x));
            return false;
        }

        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        int num = draw.num;
        if (draw.reason == "InitialHandCards" || num < 1) return false;

        draw.num = INT_MIN;
        *ctx.original_data = QVariant::fromValue(draw);

        QVariantList used = player->getTag("MTChunzuUsedIds").toList(), got = player->getTag("MTChunzuGotIds").toList();

        QList<int> used_ids;
        foreach (QVariant id, used) {
            int idd = id.toInt();
            if (room->getCardPlace(idd) == Player::DiscardPile && !got.contains(idd))
                used_ids << idd;
        }

        if (used_ids.isEmpty()) {
            LogMessage log;
            log.type = "#MTChunzuNone";
            log.from = player;
            log.arg = objectName();
            room->sendLog(log);
            player->peiyin(this);
            room->notifySkillInvoked(player, objectName());
            return false;
        }

        room->sendCompulsoryTriggerLog(player, this);

        DummyCard *dummy = new DummyCard();
        dummy->deleteLater();
        for (int i = 0; i < num; i++) {
            if (used_ids.isEmpty() || player->isDead()) break;
            room->fillAG(used_ids, player);
            int id = room->askForAG(player, used_ids, false, objectName(), "@mtchunzu");
            room->clearAG(player);
            used_ids.removeAll(id);
            got << id;
            dummy->addSubcard(id);
        }
        player->setTag("MTChunzuGotIds", got);
        player->obtainCard(dummy);
        return false;
    }
};

class MTChunzuRecord : public TriggerSkillV2
{
public:
    MTChunzuRecord() : TriggerSkillV2("#mtchunzu")
    {
        events << CardFinished;
        frequency = Limited;
        global = true;
    }

    // Every player's used physical cards are recorded for a later Chunzu.
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !use.card || use.card->isKindOf("SkillCard") || use.card->isVirtualCard()
            || !use.card->getSkillName().isEmpty()) return true;
        QVariantList used = player->getTag("MTChunzuUsedIds").toList();
        int id = use.card->getEffectiveId();
        if (used.contains(id)) return true;
        used << id;
        player->setTag("MTChunzuUsedIds", used);
        return true;
    }
};

class MTBishi : public TriggerSkillV2
{
public:
    MTBishi() : TriggerSkillV2("mtbishi")
    {
        events << CardUsed;
        frequency = Compulsory;
        waked_skills = "#mtbishi";
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->isKindOf("SkillCard") || !use.card->hasFlag("mtbishiSecond")) return result;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isAlive() && p->hasSkill(objectName()) && (use.from == p || use.to.contains(p)))
                result[p] << objectName();
        }
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(p, this);
        p->drawCards(1, objectName());
        if (p->getKingdom() != "qun") return false;
        QString mark = QString("&mtbishi-Self%1Clear").arg(int(Player::RoundStart));
        room->addPlayerMark(p, mark);
        return false;
    }
};

class MTBishiDis : public DistanceSkillV2
{
public:
    MTBishiDis() : DistanceSkillV2("#mtbishi")
    {
        setHolderSelector(CorrectSkill_Secondary);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        QString mark = QString("&mtbishi-Self%1Clear").arg(int(Player::RoundStart));
        const int n = ctx.secondary ? ctx.secondary->getMark(mark) : 0;
        return n > 0 ? CorrectSkillResult::useAmount(n) : CorrectSkillResult::noEffect();
    }
};

class MTChushi : public TriggerSkillV2
{
public:
    MTChushi() : TriggerSkillV2("mtchushi")
    {
        events << EventPhaseStart;
        waked_skills = "olkanpo,bazhen";
    }

    // Bazhen lent by Chushi returns at the lender's next turn, even after Chushi is lost.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || player->isDead() || player->getPhase() != Player::RoundStart) return true;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            ServerPlayer *t = p->getTag("MTChushi").value<ServerPlayer *>();
            if (!t || t != player) continue;
            p->removeTag("MTChushi");

            LogMessage log;
            log.type = "#ZhafuEffect";
            log.from = p;
            log.arg = objectName();
            room->sendLog(log);

            if (p->hasSkill("bazhen", true))
                room->detachSkillFromPlayer(p, "bazhen");
        }
        return true;
    }

    static QList<ServerPlayer *> richer(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> targets;
        int hand = player->getHandcardNum();
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getHandcardNum() > hand)
                targets << p;
        }
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
            && !richer(room, player).isEmpty()
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
    {
        return player->askForSkillInvoke(this);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        player->peiyin(this);

        QList<ServerPlayer *> targets = richer(room, player);
        if (targets.isEmpty()) return false;

        QList<ServerPlayer *> players;
        QStringList player_names;
        foreach (ServerPlayer *p, targets) {
            if (player->isDead()) return false;
            if (p->isDead()) continue;
            const Card *c = room->askForCard(p, "..", "@mtchushi-give:" + player->objectName(), QVariant::fromValue(player), Card::MethodNone, player);
            if (!c) continue;
            players << p;
            player_names << p->objectName();
            if (p->isAlive() && player->isAlive())
                room->giveCard(p, player, c, objectName());
        }

        if (player->isDead()) return false;
        if (player->getKingdom() == "shu") {
            room->acquireNextTurnSkills(player, QString(), "olkanpo");
            foreach (ServerPlayer *p, players) {
                if (p->isDead() || p->hasSkill("bazhen", true)) continue;
                p->setTag("MTChushi", QVariant::fromValue(player));
                p->acquireSkill("bazhen");
            }
        } else {
            QString choice = "kanpo";
            if (!players.isEmpty())
                choice = room->askForChoice(player, objectName(), "kanpo+bazhen", player_names.join("+"));

            if (choice == "kanpo")
                room->acquireNextTurnSkills(player, QString(), "olkanpo");
            else {
                foreach (ServerPlayer *p, players) {
                    if (p->isDead() || p->hasSkill("bazhen", true)) continue;
                    p->setTag("MTChushi", QVariant::fromValue(player));
                    room->acquireSkill(p, "bazhen");
                }
            }
        }
        return false;
    }
};

class MTJijing : public TriggerSkillV2
{
public:
    MTJijing() : TriggerSkillV2("mtjijing")
    {
        events << EventPhaseStart << Pindian;
    }

    static bool canInvoke(ServerPlayer *p)
    {
        int n = 1;
        if (p->hasSkill("mtbiancai") && p->getKingdom() == "shu")
            n++;
        return p->getMark("mtjijing_lun") < n;
    }

    static bool reclaims(ServerPlayer *p, const Card *card)
    {
        if (!p || !card || p->isDead() || p->getMark("mtjijing-Clear") <= 0) return false;
        return p->getTag("mtjijing_list").toString().split("+").contains(QString::number(card->getEffectiveId()));
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player) return result;
        if (event == EventPhaseStart) {
            if (player->isDead() || player->getPhase() != Player::Start) return result;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->isAlive() && p->hasSkill(objectName()) && canInvoke(p))
                    result[p] << objectName();
            }
        } else {
            PindianStruct *pindian = data.value<PindianStruct *>();
            if (reclaims(pindian->from, pindian->from_card) && pindian->from->hasSkill(objectName()))
                result[pindian->from] << objectName();
            if (reclaims(pindian->to, pindian->to_card) && pindian->to->hasSkill(objectName()))
                result[pindian->to] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *, ServerPlayer *p, SkillContext &) const override
    {
        if (event == Pindian) return true;
        return canInvoke(p) && p->askForSkillInvoke(this);
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            p->peiyin(this);

            room->addPlayerMark(p, "mtjijing-Clear");
            room->addPlayerMark(p, "mtjijing_lun");

            QString mtjijing = p->getTag("mtjijing_list").toString();
            if (!mtjijing.isEmpty()) {
                QList<int> ids = ListS2I(mtjijing.split("+"));
                foreach (int id, p->handCards()) {
                    if (!ids.contains(id)) continue;
                    room->setCardTip(id, "mtjijing");
                }
            }

            p->drawCards(1, objectName());
            return false;
        }

        PindianStruct *pindian = ctx.original_data->value<PindianStruct *>();
        room->sendCompulsoryTriggerLog(p, this);
        if (p == pindian->from && room->getCardPlace(pindian->from_card->getEffectiveId()) == Player::PlaceTable)
            room->obtainCard(p, pindian->from_card);
        else if (p == pindian->to && room->getCardPlace(pindian->to_card->getEffectiveId()) == Player::PlaceTable)
            room->obtainCard(p, pindian->to_card);
        return false;
    }
};

class MTJijingRecord : public TriggerSkillV2
{
public:
    MTJijingRecord() : TriggerSkillV2("#mtjijing")
    {
        events << EventPhaseChanging << CardsMoveOneTime << EventAcquireSkill;
        global = true;
    }

    // Tracks the cards every player gains this turn, for whoever holds Jijing.
    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive) return true;
            foreach (ServerPlayer *p, room->getAllPlayers(true)) {
                p->removeTag("mtjijing_list");
                foreach (int id, p->handCards() + p->getEquipsId())
                    room->setCardTip(id, "-mtjijing");
            }
        } else if (!player) {
            return true;
        } else if (triggerEvent == CardsMoveOneTime) {
            if (!room->hasCurrent(true)) return true;

            QStringList mtjijinglist;
            QString mtjijing = player->getTag("mtjijing_list").toString();
            if (!mtjijing.isEmpty())
                mtjijinglist = mtjijing.split("+");

            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (!room->getTag("FirstRound").toBool() && move.to == player && move.to_place == Player::PlaceHand) {
                foreach (int id, move.card_ids) {
                    if (player->hasSkill("mtjijing", true) && player->getMark("mtjijing-Clear") > 0)
                        room->setCardTip(id, "mtjijing");
                    QString str = QString::number(id);
                    if (mtjijinglist.contains(str)) continue;
                    mtjijinglist << str;
                }
                player->setTag("mtjijing_list", mtjijinglist.join("+"));
            }
        } else if (triggerEvent == EventAcquireSkill) {
            if (data.toString() != "mtjijing" || !player->hasSkill("mtjijing", true) || player->getMark("mtjijing-Clear") <= 0) return true;
            QString mtjijing = player->getTag("mtjijing_list").toString();
            if (mtjijing.isEmpty()) return true;
            QList<int> ids = ListS2I(mtjijing.split("+"));
            foreach (int id, player->handCards()) {
                if (!ids.contains(id)) continue;
                room->setCardTip(id, "mtjijing");
            }
        }
        return true;
    }
};

class MTBiancai : public TriggerSkillV2
{
public:
    MTBiancai() : TriggerSkillV2("mtbiancai")
    {
        events << EventPhaseStart << CardUsed;
        waked_skills = "#mtbiancai";
    }

    // A won pindian makes that type of card unanswerable by the loser, even if Biancai is lost.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardUsed || !player || player->isDead()) return true;
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->isKindOf("SkillCard")) return true;
        int id = use.card->getTypeId();
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (player->getMark(QString("mtbiancai_win_%1_%2-Clear").arg(id).arg(p->objectName())) > 0)
                use.no_respond_list << p->objectName();
        }
        data = QVariant::fromValue(use);
        return true;
    }

    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (player->canPindian(p))
                targets << p;
        }
        return targets;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Play || !player->canPindian() || player->getKingdom() != "shu"
            || candidates(room, player).isEmpty()) return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *t = room->askForPlayerChosen(player, candidates(room, player), objectName(), "@mtbiancai", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &, ServerPlayer *t) const override
    {
        player->peiyin(this);

        PindianStruct *pindian = player->PinDian(t, objectName());

        if (pindian->success) {
            int from_id = pindian->from_card->getTypeId(), to_id = pindian->to_card->getTypeId();
            room->setPlayerMark(player, QString("mtbiancai_win_%1_%2-Clear").arg(from_id).arg(t->objectName()), 1);
            room->setPlayerMark(player, QString("mtbiancai_win_%1_%2-Clear").arg(to_id).arg(t->objectName()), 1);
        } else {
            QString from_type = pindian->from_card->getType(), to_type = pindian->to_card->getType();
            room->setPlayerMark(player, QString("mtbiancai_notwin_%1-Clear").arg(from_type), 1);
            room->setPlayerMark(player, QString("mtbiancai_notwin_%1-Clear").arg(to_type), 1);
        }
        return false;
    }
};

class MTBiancaiLimit : public CardLimitSkill
{
public:
    MTBiancaiLimit() : CardLimitSkill("#mtbiancai")
    {
    }

    QString limitList(const Player *) const
    {
        return "use";
    }

    QString limitPattern(const Player *target) const
    {
        QStringList trs;
        foreach (QString mark, target->getMarkNames()) {
            if (!mark.startsWith("mtbiancai_notwin_") || target->getMark(mark) < 1) continue;
            QStringList marks = mark.split("_");
            if (marks.length() != 3) continue;
            QString type = marks.last().split("-").first();
            trs << type;
        }
        return trs.join(",");
    }
};

class MTChenxiao : public TriggerSkillV2
{
public:
    MTChenxiao() : TriggerSkillV2("mtchenxiao")
    {
        events << EventPhaseStart;
        waked_skills = "#mtchenxiao";
    }

    static QString failMark()
    {
        return "&mtchenxiao-Self" + QString::number((int)Player::RoundStart) + "Clear";
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Play) return result;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->isAlive() && p->hasSkill(objectName()) && p->getKingdom() == "jin" && p->canPindian(player)
                && p->getMark(failMark()) <= 0)
                result[p] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *p, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        return player->isAlive() && p->canPindian(player) && p->askForSkillInvoke(this, player);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        p->peiyin(this);

        if (p->pindian(player, objectName())) {
            room->setPlayerMark(player, QString("MTChenxiao_%1-Clear").arg(p->objectName()), 1);
            room->setPlayerMark(p, "mtjijing_lun", 0);
        } else
            room->setPlayerMark(p, failMark(), 1);
        return false;
    }
};

class MTChenxiaoProhibit : public ProhibitSkill
{
public:
    MTChenxiaoProhibit() : ProhibitSkill("#mtchenxiao")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return from->getMark(QString("MTChenxiao_%1-Clear").arg(to->objectName())) > 0 && !card->isKindOf("SkillCard");
    }
};

class MTZhuluVS : public ViewAsSkillV2
{
public:
    MTZhuluVS() : ViewAsSkillV2("mtzhulu")
    {
    }

    // The Slash made from every hand card, as Zhulu uses it.
    static Slash *handSlash(const Player *player)
    {
        Slash *slash = new Slash(Card::SuitToBeDecided, -1);
        slash->setSkillName("mtzhulu");
        slash->addSubcards(player->handCards());
        return slash;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@mtzhulu") && !request.initiator->isKongcheng();
    }

    // Only the players shown to this turn may be targeted.
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets,
                         const Player *to_select) const override
    {
        if (!to_select || request.initiator->isKongcheng()) return false;
        Slash *slash = handSlash(request.initiator);
        const bool ok = !request.initiator->isCardLimited(slash, Card::MethodUse, true)
            && slash->targetFilter(targets, to_select, request.initiator) && to_select->getMark("mtzhuluTarget-Clear") > 0;
        delete slash;
        return ok;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return !targets.isEmpty();
    }

    TargetEffectMode targetEffectMode() const override
    {
        return WholeTargetGroup;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "MTZhuluCard";
    }

    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *from = ctx.invoker;
        if (!from || from->isDead() || from->isKongcheng()) return FinishSkill;
        Slash *slash = handSlash(from);
        slash->deleteLater();
        if (from->isCardLimited(slash, Card::MethodUse, true)) return FinishSkill;
        from->getRoom()->useCard(CardUseStruct(slash, from, targets));
        return FinishSkill;
    }
};

class MTZhulu : public TriggerSkillV2
{
public:
    MTZhulu() : TriggerSkillV2("mtzhulu")
    {
        events << CardFinished << EventPhaseChanging;
        view_as_skill = new MTZhuluVS;
        waked_skills = "#mtzhulu-slash-ndl,#mtzhulu";
    }

    static QList<ServerPlayer *> showable(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> players;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!p->isKongcheng())
                players << p;
        }
        return players;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        if (event == CardFinished) {
            if (player->getPhase() != Player::Play || player->getMark("mtzhuluWuxiao-Clear") > 0) return TriggerList();
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || use.card->isKindOf("SkillCard") || showable(room, player).isEmpty()) return TriggerList();
        } else {
            if (player->isKongcheng() || data.value<PhaseChangeStruct>().to != Player::NotActive) return TriggerList();
            bool flag = false;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getMark("mtzhuluTarget-Clear") > 0) {
                    flag = true;
                    break;
                }
            }
            if (!flag) return TriggerList();
            Slash *slash = MTZhuluVS::handSlash(player);
            const bool limited = player->isCardLimited(slash, Card::MethodUse, true);
            delete slash;
            if (limited) return TriggerList();
        }
        return TriggerList{{player, QStringList{objectName()}}};
    }

    // At turn end the Slash is the invocation; declining it never invoked Zhulu.
    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging)
            return room->askForUseCard(player, "@@mtzhulu", "@mtzhulu");
        ServerPlayer *t = room->askForPlayerChosen(player, showable(room, player), objectName(), "@mtzhulu-show", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &, ServerPlayer *t) const override
    {
        player->peiyin(this);

        room->setPlayerMark(t, "mtzhuluTarget-Clear", 1);
        if (t->isKongcheng()) return false;
        int id = room->askForCardChosen(player, t, "h", objectName());

        /*QVariantList ids = t->getTag("MTZhuluShow").toList();
        if (!ids.contains(QVariant(id))) {
            ids << id;
            t->setTag("MTZhuluShow", ids);
        }*/

        room->setPlayerMark(t, QString("MTZhuluShow_%1-Clear").arg(id), 1);
        room->showCard(t, id);

        QString suit = Sanguosha->getCard(id)->getSuitString() + "_char";
        QStringList suits;
        foreach (QString mark, player->getMarkNames()) {
            if (!mark.startsWith("&mtzhuluSuit+") || !mark.endsWith("-PlayClear") || player->getMark(mark) < 1) continue;
            QStringList marks = mark.split("+");
            foreach (QString s, marks) {
                if (s == "&mtzhuluSuit") continue;
                QString ss = s.split("-").first();
                if (!suits.contains(ss))
                    suits << ss;
            }
            room->setPlayerMark(player, mark, 0);
        }

        if (suits.contains(suit)) {
            room->setPlayerMark(player, "&mtzhuluSuit+" + suits.join("+") + "-PlayClear", 1);
            room->setPlayerMark(player, "mtzhuluWuxiao-Clear", 1);
        } else {
            suits << suit;
            room->setPlayerMark(player, "&mtzhuluSuit+" + suits.join("+") + "-PlayClear", 1);
            player->drawCards(1, objectName());
        }
        return false;
    }
};

class MTZhuluDamage : public TriggerSkillV2
{
public:
    MTZhuluDamage() : TriggerSkillV2("#mtzhulu")
    {
        events << Damage;
        frequency = Compulsory;
    }

    static QList<int> shownCards(ServerPlayer *to)
    {
        QList<int> ids;
        foreach (int id, to->handCards()) {
            if (to->getMark(QString("MTZhuluShow_%1-Clear").arg(id)) > 0)
                ids << id;
        }
        return ids;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const DamageStruct d = data.value<DamageStruct>();
        if (!d.card || !d.card->isKindOf("Slash") || !d.card->getSkillNames().contains("mtzhulu")) return TriggerList();
        if (d.chain || d.transfer || !d.to || d.to->isKongcheng() || shownCards(d.to).isEmpty()) return TriggerList();
        ServerPlayer *user = room->getCardUser(d.card);
        if (!user || user->isDead() || !user->hasSkill(objectName())) return TriggerList();
        return TriggerList{{user, QStringList{objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *user, SkillContext &ctx) const override
    {
        const DamageStruct d = ctx.original_data->value<DamageStruct>();
        const QList<int> ids = shownCards(d.to);
        if (ids.isEmpty()) return false;
        DummyCard dummy(ids);
        room->obtainCard(user, &dummy, objectName());
        return false;
    }
};

class MTZhuluNoDistanceLimit : public TargetModSkillV2
{
public:
    MTZhuluNoDistanceLimit() : TargetModSkillV2("#mtzhulu-slash-ndl")
    {
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType == TargetModSkill::DistanceLimit && ctx.card && ctx.card->getSkillName() == "mtzhulu"
            && ctx.primary && ctx.primary->hasSkill("mtzhulu"))
            return CorrectSkillResult::useAmount(999);
        return CorrectSkillResult::noEffect();
    }
};

class MTZhengwang : public TriggerSkillV2
{
public:
    MTZhengwang() : TriggerSkillV2("mtzhengwang")
    {
        events << EventPhaseStart << EventPhaseEnd << DamageDone;
        waked_skills = "#mtzhengwang";
    }

    static bool lordTurnEnds(Room *room, ServerPlayer *player)
    {
        return player && player->getRole() == "lord" && player->getPhase() == Player::NotActive
            && !room->getTag("Global_ExtraTurn" + player->objectName()).toBool();
    }

    // The lord's damage count is consumed at every lord turn end; damage spends the bonus.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseStart) {
            if (!lordTurnEnds(room, player)) return true;
            player->setTag("MTZhengwangPending", player->getMark("MTZhengwangDamage-Keep"));
            room->setPlayerMark(player, "MTZhengwangDamage-Keep", 0);
        } else if (event == DamageDone) {
            DamageStruct d = data.value<DamageStruct>();
            if (d.damage >= 1 && d.from)
                room->removePlayerMark(d.from, "&mtzhengwangDamage-Clear", d.damage);
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart) {
            if (!lordTurnEnds(room, player)) return result;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->isAlive() && p->hasSkill(objectName()) && p->getKingdom() == "jin")
                    result[p] << objectName();
            }
        } else if (event == EventPhaseEnd) {
            if (player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
                && player->getMark("&mtzhengwangDamage-Clear") >= 1 && !player->isKongcheng())
                result[player] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *, ServerPlayer *p, SkillContext &ctx) const override
    {
        if (event != EventPhaseStart) return true;
        const int mark = ctx.invoker->getTag("MTZhengwangPending").toInt();
        return p->askForSkillInvoke(this, QString("mtzhengwang:%1:%2").arg(ctx.invoker->objectName()).arg(mark));
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        if (event == EventPhaseEnd) {
            room->sendCompulsoryTriggerLog(p, this);
            p->throwAllHandCards();
            return false;
        }
        const int mark = ctx.invoker->getTag("MTZhengwangPending").toInt();
        p->peiyin(this);

        room->setPlayerMark(p, "&mtzhengwangDamage-Clear", mark + 1);
        p->drawCards(2, objectName());
        p->gainAnExtraTurn();
        return false;
    }
};

class MTZhengwangRecord : public TriggerSkillV2
{
public:
    MTZhengwangRecord() : TriggerSkillV2("#mtzhengwang")
    {
        events << DamageDone;
        global = true;
    }

    // Damage the lord deals in its own normal turns.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        DamageStruct d = data.value<DamageStruct>();
        if (d.damage < 1) return true;
        ServerPlayer *from = d.from;
        if (!from) return true;
        if (from->getRole() != "lord" || from->getPhase() == Player::NotActive) return true;
        if (room->getTag("Global_ExtraTurn" + from->objectName()).toBool()) return true;
        room->addPlayerMark(from, "MTZhengwangDamage-Keep", d.damage);
        return true;
    }
};

class MTZhuizunVS : public ViewAsSkillV2
{
public:
    MTZhuizunVS() : ViewAsSkillV2("mtzhuizun", 2)
    {
        expand_pile = "mtzhuizunde";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getPile("mtzhuizunde").length() > 1;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ViewAsSkillV2::canSelectCard(request, card) && request.initiator
            && !request.selectedCardIds.contains(card->getEffectiveId())
            && request.initiator->getPile("mtzhuizunde").contains(card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.length() == 2 && replaySelection(this, request);
    }

    TargetMode targetMode() const override
    {
        return NoTarget;
    }

    bool willThrowSelectedCards() const override
    {
        return false;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "MTZhuizunCard";
    }

    // The two "de" leave the pile as the price.
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        ServerPlayer *source = ctx.initiator;
        if (!source || !cardSelectionFeasible(request)) return false;
        DummyCard dummy(request.selectedCardIds);
        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, source->objectName(), QString(), "mtzhuizun", "");
        room->throwCard(&dummy, reason, NULL);
        return true;
    }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || source->isDead()) return FinishSkill;
        Room *room = source->getRoom();

        QStringList choices;
        if (!source->hasSkill("tenyearrende1", true))
            choices << "tenyearrende1";
        if (!source->hasSkill("olsishu1", true))
            choices << "olsishu1";
        if (choices.isEmpty()) return FinishSkill;

        QStringList skills = source->getTag("MTZhuizunSkills").toStringList();
        QString choice = room->askForChoice(source, "mtzhuizun", choices.join("+"));
        choice.chop(1);
        if (!skills.contains(choice)) {
            skills << choice;
            source->setTag("MTZhuizunSkills", skills);
        }

        room->acquireSkill(source, choice);
        return FinishSkill;
    }
};

class MTZhuizun : public TriggerSkillV2
{
public:
    MTZhuizun() : TriggerSkillV2("mtzhuizun")
    {
        events << EventPhaseStart;
        view_as_skill = new MTZhuizunVS;
        waked_skills = "#mtzhuizun";
    }

    // The turn player's hand cards whose suits are not yet among p's "de".
    static QStringList offerable(ServerPlayer *player, ServerPlayer *p)
    {
        QList<Card::Suit> suits;
        foreach (int id, p->getPile("mtzhuizunde")) {
            Card::Suit suit = Sanguosha->getCard(id)->getSuit();
            if (!suits.contains(suit))
                suits << suit;
        }

        QStringList ids;
        foreach (int id, player->handCards()) {
            if (!suits.contains(Sanguosha->getCard(id)->getSuit()))
                ids << QString::number(id);
        }
        return ids;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::RoundStart || player->isKongcheng()) return result;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isAlive() && p->hasSkill(objectName()) && p->getKingdom() == "qun" && !offerable(player, p).isEmpty())
                result[p] << objectName();
        }
        return result;
    }

    // The turn player decides whether to offer a card to p.
    bool cost(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (player->isDead() || player->isKongcheng()) return false;
        const QStringList ids = offerable(player, p);
        if (ids.isEmpty()) return false;
        const Card *c = room->askForCard(player, ids.join(","), "@mtzhuizun:" + p->objectName(), QVariant::fromValue(p), Card::MethodNone);
        if (!c) return false;
        ctx.extra_data = c->getEffectiveId();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        LogMessage log;
        log.type = player == p ? "#InvokeSkill" : "#InvokeOthersSkill";
        log.arg = objectName();
        log.from = player;
        log.to << p;
        room->sendLog(log);
        p->peiyin(this);
        room->notifySkillInvoked(p, objectName());

        if (p->isAlive())
            p->addToPile("mtzhuizunde", Sanguosha->getCard(ctx.extra_data.toInt()));
        player->drawCards(1, objectName());
        return false;
    }
};

class MTZhuizunEffect : public TriggerSkillV2
{
public:
    MTZhuizunEffect() : TriggerSkillV2("#mtzhuizun")
    {
        events << RoundEnd;
        frequency = Compulsory;
    }

    // Skills gained by Zhuizun last until the end of the round, even if Zhuizun is gone.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            QStringList skills = p->getTag("MTZhuizunSkills").toStringList();
            if (skills.isEmpty()) continue;
            p->removeTag("MTZhuizunSkills");
            QStringList loses;
            foreach (QString sk, skills) {
                if (!loses.contains("-" + sk) && p->hasSkill(sk, true))
                    loses << "-" + sk;
            }
            room->handleAcquireDetachSkills(p, loses.join("|"));
        }
        return true;
    }
};

class MTZhanhua : public TriggerSkillV2
{
public:
    MTZhanhua() : TriggerSkillV2("mtzhanhua")
    {
        events << EventPhaseStart << Dying;
        waked_skills = "#mtzhanhua";
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Play || player->getMark("MTZhanhuaEventPhaseStart") > 0) return TriggerList();
        } else if (data.value<DyingStruct>().who != player || player->getMark("MTZhanhuaDying") > 0) {
            return TriggerList();
        }
        return TriggerList{{player, QStringList{objectName()}}};
    }

    // The recorded highest values are refreshed before the offer, as they appear in its prompt.
    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (event == EventPhaseStart) {
            int num = player->getTag("MTZhanhuaHandcardNum").toInt(), hand = player->getHandcardNum();
            if (hand > num) {
                num = hand;
                player->setTag("MTZhanhuaHandcardNum", hand);
                player->setSkillDescriptionSwap(objectName(), "%arg2", QString::number(hand));
                room->changeTranslation(player, objectName(), 1);
            }
            return player->askForSkillInvoke(this, QString("handcardnum:%1").arg(num + 1));
        }

        int max = player->getTag("MTZhanhuaMaxHp").toInt(), maxhp = player->getMaxHp();
        if (maxhp > max) {
            max = maxhp;
            player->setTag("MTZhanhuaMaxHp", maxhp);
            player->setSkillDescriptionSwap(objectName(), "%arg3", QString::number(maxhp));
            room->changeTranslation(player, objectName(), 1);
        }

        int hp = player->getTag("MTZhanhuaHp").toInt(), hpp = player->getHp();
        if (hpp > hp) {
            hp = hpp;
            player->setTag("MTZhanhuaHp", hpp);
            player->setSkillDescriptionSwap(objectName(), "%arg4", QString::number(hpp));
            room->changeTranslation(player, objectName(), 1);
        }

        max++;
        hp++;
        hp = qMin(max, hp);
        return player->askForSkillInvoke(this, QString("dying:%1:%2").arg(max).arg(hp));
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        player->peiyin(this);
        if (event == EventPhaseStart) {
            const int num = player->getTag("MTZhanhuaHandcardNum").toInt() + 1;
            room->setPlayerMark(player, "MTZhanhuaEventPhaseStart", 1);
            player->drawCards(num - player->getHandcardNum(), objectName());
            return false;
        }

        const int max = player->getTag("MTZhanhuaMaxHp").toInt() + 1;
        const int hp = qMin(max, player->getTag("MTZhanhuaHp").toInt() + 1);
        room->setPlayerMark(player, "MTZhanhuaDying", 1);
        room->setPlayerProperty(player, "maxhp", max);
        //room->setPlayerProperty(player, "hp", hp);
        room->recover(player, RecoverStruct(objectName(), player, hp - player->getHp()));
        return false;
    }
};

class MTZhanhuaRecord : public TriggerSkillV2
{
public:
    MTZhanhuaRecord() : TriggerSkillV2("#mtzhanhua")
    {
        events << DamageCaused << CardsMoveOneTime << GameReady << MaxHpChanged << HpChanged;
        global = true;
    }

    // Damage is raised after ordinary damage-caused skills; the records update before them.
    bool usesEventPriority() const override
    {
        return true;
    }

    int getPriority(TriggerEvent event) const override
    {
        if (event == DamageCaused)
            return TriggerSkill::getPriority(event) - 1;
        return TriggerSkill::getPriority(event) + 1;
    }

    // Every player's highest damage, hand size, maximum health and health are recorded.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (event == DamageCaused) {
            DamageStruct damage = data.value<DamageStruct>();
            int d = player->getTag("MTZhanhuaDamage").toInt(), dd = damage.damage;
            if (dd > d) {
                player->setTag("MTZhanhuaDamage", dd);
                player->setSkillDescriptionSwap("mtzhanhua", "%arg1", QString::number(dd));
                room->changeTranslation(player, "mtzhanhua", 1);
            }
        } else if (event == GameReady) {
            int d = player->getTag("MTZhanhuaDamage").toInt(), num = player->getTag("MTZhanhuaHandcardNum").toInt(),
                max = player->getTag("MTZhanhuaMaxHp").toInt(), hp = player->getTag("MTZhanhuaHp").toInt();
            num = qMax(num, player->getHandcardNum());
            max = qMax(num, player->getMaxHp());
            hp = qMax(num, player->getHp());

            player->setTag("MTZhanhuaHandcardNum", num);
            player->setTag("MTZhanhuaMaxHp", max);
            player->setTag("MTZhanhuaHp", hp);

            player->setSkillDescriptionSwap("mtzhanhua", "%arg1", QString::number(d));
            player->setSkillDescriptionSwap("mtzhanhua", "%arg2", QString::number(num));
            player->setSkillDescriptionSwap("mtzhanhua", "%arg3", QString::number(max));
            player->setSkillDescriptionSwap("mtzhanhua", "%arg4", QString::number(hp));
            room->changeTranslation(player, "mtzhanhua", 1);
        } else if (event == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to == player && move.to_place == Player::PlaceHand) {
                int num = player->getTag("MTZhanhuaHandcardNum").toInt(), hand = player->getHandcardNum();
                if (hand > num) {
                    player->setTag("MTZhanhuaHandcardNum", hand);
                    player->setSkillDescriptionSwap("mtzhanhua", "%arg2", QString::number(hand));
                    room->changeTranslation(player, "mtzhanhua", 1);
                }
            }
        } else if (event == MaxHpChanged) {
            int max = player->getTag("MTZhanhuaMaxHp").toInt(), maxhp = player->getMaxHp();
            if (maxhp > max) {
                player->setTag("MTZhanhuaMaxHp", maxhp);
                player->setSkillDescriptionSwap("mtzhanhua", "%arg3", QString::number(maxhp));
                room->changeTranslation(player, "mtzhanhua", 1);
            }
        } else if (event == HpChanged) {
            int hp = player->getTag("MTZhanhuaHp").toInt(), hpp = player->getMaxHp();
            if (hpp > hp) {
                player->setTag("MTZhanhuaHp", hpp);
                player->setSkillDescriptionSwap("mtzhanhua", "%arg4", QString::number(hpp));
                room->changeTranslation(player, "mtzhanhua", 1);
            }
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == DamageCaused && player && player->isAlive() && player->hasSkill(objectName())
            && player->getMark("MTZhanhuaDamageCaused") <= 0
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const int d = qMax(damage.damage, player->getTag("MTZhanhuaDamage").toInt());
        player->setTag("MTZhanhuaDamageCaused", *ctx.original_data);
        bool invoke = player->askForSkillInvoke("mtzhanhua", QString("damage:%1:%2").arg(damage.to->objectName()).arg(d + 1));
        player->removeTag("MTZhanhuaDamageCaused");
        return invoke;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        player->peiyin("mtzhanhua");
        room->setPlayerMark(player, "MTZhanhuaDamageCaused", 1);
        const int d = qMax(damage.damage, player->getTag("MTZhanhuaDamage").toInt()) + 1;
        player->setTag("MTZhanhuaDamage", d);
        player->setSkillDescriptionSwap("mtzhanhua", "%arg1", QString::number(d));
        room->changeTranslation(player, "mtzhanhua", 1);

        damage.damage = d;
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

MTHongwuCard::MTHongwuCard()
{
    mute = true;
}

bool MTHongwuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    const Card *c = Sanguosha->getCard(subcards.first());
    Card *card = Sanguosha->cloneCard(c);
    card->addSubcard(c);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetFilter(targets, to_select, Self);
}

bool MTHongwuCard::targetFixed() const
{
    const Card *c = Sanguosha->getCard(subcards.first());
    Card *card = Sanguosha->cloneCard(c);
    card->addSubcard(c);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetFixed();
}

bool MTHongwuCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    const Card *c = Sanguosha->getCard(subcards.first());
    Card *card = Sanguosha->cloneCard(c);
    card->addSubcard(c);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetsFeasible(targets, Self);
}

void MTHongwuCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *from = card_use.from;
    const Card *c = Sanguosha->getCard(subcards.first());
    room->useCard(CardUseStruct(c, from, card_use.to));
}

// Stays a legacy view-as skill: the turn player, who does not own Hongwu, answers @@mthongwu.
class MTHongwuVS :public OneCardViewAsSkill
{
public:
    MTHongwuVS() :OneCardViewAsSkill("mthongwu")
    {
        response_pattern = "@@mthongwu";
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->isDamageCard() && Self->getHandcards().contains(to_select) && to_select->isAvailable(Self);
    }

    const Card *viewAs(const Card *originalCard) const
    {
        //MTHongwuCard *card = new MTHongwuCard;  //防止重铸，不然直接return originalCard了;
        //card->addSubcard(originalCard);
        return originalCard;
    }
};

class MTHongwu : public TriggerSkillV2
{
public:
    MTHongwu() : TriggerSkillV2("mthongwu")
    {
        events << EventPhaseStart << EventPhaseChanging;
        view_as_skill = new MTHongwuVS;
        waked_skills = "#mthongwu";
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player) return result;
        if (event == EventPhaseStart) {
            if (player->isDead() || player->getPhase() != Player::RoundStart) return result;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->isAlive() && p->hasSkill(objectName()) && p->getMark("hongwuWuxiao_lun") <= 0)
                    result[p] << objectName();
            }
        } else {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return result;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->isAlive() && p->hasSkill(objectName())
                    && !p->getTag("MTHongwu_" + player->objectName()).toString().isEmpty())
                    result[p] << objectName();
            }
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *, ServerPlayer *p, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging) return true;
        ServerPlayer *player = ctx.invoker;
        return player->isAlive() && p->askForSkillInvoke(this, player);
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (event == EventPhaseStart) {
            p->peiyin(this);

            QList<ServerPlayer *> targets;
            targets << p << player;
            room->sortByActionOrder(targets);
            room->drawCards(targets, 1, objectName());

            Card::Suit suit = room->askForSuit(player, objectName());
            QString suit_str = Card::Suit2String(suit);
            p->setTag("MTHongwu_" + player->objectName(), suit_str);

            LogMessage log;
            log.type = "#ChooseSuit";
            log.from = player;
            log.arg = suit_str;
            room->sendLog(log, player);
            room->setPlayerCardLimitation(player, "use,response,discard", QString(".|%1|.|.").arg(suit_str), true);
            return false;
        }

        QString suit = p->getTag("MTHongwu_" + player->objectName()).toString();
        if (suit.isEmpty()) return false;

        Card::Suit suitt = room->askForSuit(p, objectName());
        QString suit_str = Card::Suit2String(suitt);
        bool b = suit == suit_str;

        LogMessage log;
        log.type = b ? "#MTHongwuRight" : "#MTHongwuWrong";
        log.from = p;
        log.arg = suit_str;
        log.arg2 = suit;
        room->sendLog(log);

        if (player->isDead()) return false;

        if (b) {
            DummyCard *dummy = new DummyCard;
            dummy->deleteLater();
            QList<int> jilei_list;

            foreach (const Card *c, player->getCards("he")) {
                if (c->isDamageCard()) continue;
                int id = c->getEffectiveId();
                if (player->canDiscard(player, id))
                    dummy->addSubcard(c);
                else
                    jilei_list << id;
            }

            if (!jilei_list.isEmpty()) {
                log.type = "$JileiShowAllCards";
                log.from = player;
                log.card_str = ListI2S(jilei_list).join("+");
                room->sendLog(log);
                JsonArray gongxinArgs;
                gongxinArgs << player->objectName() << false << JsonUtils::toJsonArray(jilei_list);
                room->doBroadcastNotify(QSanProtocol::S_COMMAND_SHOW_ALL_CARDS, gongxinArgs);
                QVariant data = log.card_str;
                room->getThread()->trigger(ShowCards, room, player, data);
            }

            if (dummy->subcardsLength() > 0)
                room->throwCard(dummy, objectName(), player);
        } else {
            room->setPlayerMark(p, "hongwuWuxiao_lun", 1);
            while (player->isAlive() && !player->isKongcheng()) {
                const Card *c = room->askForUseCard(player, "@@mthongwu", "@mthongwu");
                if (!c) break;
            }
        }
        return false;
    }
};


MaotuPackage::MaotuPackage()
    : Package("maotu")
{
    General *mt_wenhui = new General(this, "mt_wenhui", "wei", 3);
    mt_wenhui->addSkill(new MTLiaoshi);
    mt_wenhui->addSkill(new MTLiaoshiChoose);
    mt_wenhui->addSkill(new MTTongyi);

    General *mt_xiahouba = new General(this, "mt_xiahouba", "wei", 4);
    mt_xiahouba->addSkill(new MTXianzheng);
    mt_xiahouba->addSkill(new MTNianchou);
    mt_xiahouba->addSkill(new MTNianchouTargetMod);

    General *mt_zhugeshang = new General(this, "mt_zhugeshang", "shu", 4);
    mt_zhugeshang->addSkill(new MTJieli);
    mt_zhugeshang->addSkill(new MTJieliTargetMod);
    mt_zhugeshang->addSkill(new MTFuyi);
    mt_zhugeshang->addSkill(new MTFuyiDamage);
    mt_zhugeshang->addSkill(new MTFuyiTurn);

    General *mt_luoxian = new General(this, "mt_luoxian", "shu", 4);
    mt_luoxian->addSkill(new MTZhongyi);

    General *mt_zhaoshuang = new General(this, "mt_zhaoshuang", "wu", 3);
    mt_zhaoshuang->addSkill(new MTWeiqie);
    mt_zhaoshuang->addSkill(new MTGuanda);

    General *mt_weizhao = new General(this, "mt_weizhao", "wu", 3);
    mt_weizhao->addSkill(new MTZhilie);
    mt_weizhao->addSkill(new MTChuanjiu);

    General *mt_liubei = new General(this, "mt_liubei", "qun", 3);
    mt_liubei->addSkill(new MTDianpei);
    mt_liubei->addSkill(new MTRenyi);
    mt_liubei->addSkill(new MTRenyiTargetMod);

    General *mt_zhurong = new General(this, "mt_zhurong", "qun", 4, false);
    mt_zhurong->addSkill(new MTFeiren);
    mt_zhurong->addSkill(new MTFeirenTargetMod);
    mt_zhurong->addSkill(new MTFuzhan);

    General *mt_simayan = new General(this, "mt_simayan$", "jin", 4);
    mt_simayan->addSkill(new MTRenyu);
    mt_simayan->addSkill(new MTFengshang);
    mt_simayan->addSkill(new MTFengshangKeep);
    mt_simayan->addSkill(new MTJiawei);

    General *mt_wangjun = new General(this, "mt_wangjun", "jin", 4);
    mt_wangjun->addSkill(new MTGuzhao);
    mt_wangjun->addSkill(new MTGuzhaoTargetMod);

    General *mt_shenzhouyu = new General(this, "mt_shenzhouyu", "god", 4);
    mt_shenzhouyu->addSkill(new MTGuqu);
    mt_shenzhouyu->addSkill(new MTLunhuan);
    mt_shenzhouyu->addSkill(new MTLunhuanDamage);
    mt_shenzhouyu->addSkill("yingzi");

    General *mt_shencaopi = new General(this, "mt_shencaopi", "god", 3);
    mt_shencaopi->addSkill(new MTJiye);
    mt_shencaopi->addSkill(new MTZhihe);
    mt_shencaopi->addSkill(new MTWenqi);

    General *mt_zhugedan = new General(this, "mt_zhugedan", "wei+wu", 4);
    mt_zhugedan->addSkill(new MTYanyi);
    mt_zhugedan->addSkill(new MTJishi);
    mt_zhugedan->addSkill(new MTJishiTargetMod);
    mt_zhugedan->addSkill(new MTYitao);

    General *mt_mengda = new General(this, "mt_mengda", "wei+shu", 4);
    mt_mengda->addSkill(new MTJuyuan);
    mt_mengda->addSkill(new MTJuyuanFlag);
    mt_mengda->addSkill(new MTJuyuanProhibit);
    mt_mengda->addSkill(new MTFupan);

    General *mt_zhangyan = new General(this, "mt_zhangyan", "wei+qun", 4);
    mt_zhangyan->addSkill(new MTFeiyan);
    mt_zhangyan->addSkill(new MTFeiyanEffect);

    General *mt_liuling = new General(this, "mt_liuling", "wei+jin", 3);
    mt_liuling->addSkill(new MTJiukuang);
    mt_liuling->addSkill(new MTJiukuangTMD);
    mt_liuling->addSkill(new MTZongqing);
    mt_liuling->addSkill(new MTZongqingDis);

    General *mt_zhangwen = new General(this, "mt_zhangwen", "wu+shu", 3);
    mt_zhangwen->addSkill(new MTZanzhang);
    mt_zhangwen->addSkill(new MTHongya);

    General *mt_sunfuren = new General(this, "mt_sunfuren", "wu+qun", 3, false);
    mt_sunfuren->addSkill(new MTYinglve);
    mt_sunfuren->addSkill(new MTXianding);
    mt_sunfuren->addSkill(new MTXiandingRecord);

    General *mt_luji = new General(this, "mt_luji", "wu+jin", 3);
    mt_luji->addSkill(new MTWanghe);
    mt_luji->addSkill(new MTChunzu);
    mt_luji->addSkill(new MTChunzuRecord);

    General *mt_zhugejun = new General(this, "mt_zhugejun", "shu+qun", 3);
    mt_zhugejun->addSkill(new MTBishi);
    mt_zhugejun->addSkill(new MTBishiDis);
    mt_zhugejun->addSkill(new MTChushi);

    General *mt_limi = new General(this, "mt_limi", "shu+jin", 3);
    mt_limi->addSkill(new MTJijing);
    mt_limi->addSkill(new MTJijingRecord);
    mt_limi->addSkill(new MTBiancai);
    mt_limi->addSkill(new MTBiancaiLimit);
    mt_limi->addSkill(new MTChenxiao);
    mt_limi->addSkill(new MTChenxiaoProhibit);

    General *mt_liuyuan = new General(this, "mt_liuyuan", "qun+jin", 4);
    mt_liuyuan->addSkill(new MTZhulu);
    mt_liuyuan->addSkill(new MTZhuluDamage);
    mt_liuyuan->addSkill(new MTZhuluNoDistanceLimit);
    mt_liuyuan->addSkill(new MTZhengwang);
    mt_liuyuan->addSkill(new MTZhengwangRecord);
    mt_liuyuan->addSkill(new MTZhuizun);
    mt_liuyuan->addSkill(new MTZhuizunEffect);

    General *mt_shendiaochan = new General(this, "mt_shendiaochan", "god", 3, false);
    mt_shendiaochan->addSkill(new MTZhanhua);
    mt_shendiaochan->addSkill(new MTZhanhuaRecord);
    mt_shendiaochan->addSkill(new MTHongwu);

    addMetaObject<MTYinglveCard>();
    addMetaObject<MTHongwuCard>();
}
ADD_PACKAGE(Maotu)
