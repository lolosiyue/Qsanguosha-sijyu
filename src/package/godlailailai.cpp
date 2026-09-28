#include "godlailailai.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "wrapped-card.h"


class Xiongshou : public TriggerSkillV2
{
public:
    Xiongshou() : TriggerSkillV2("xiongshou") { events << DamageCaused << TurnOver; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == TurnOver) return {{player, {objectName()}}};
        const DamageStruct damage = data.value<DamageStruct>();
        return damage.from == player && damage.by_user && damage.card && damage.card->isKindOf("Slash") && damage.to
            && damage.to->getHp() < player->getHp() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets << (event == TurnOver ? owner : ctx.original_data->value<DamageStruct>().to); return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == TurnOver) return target == ctx.invoker;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (target != damage.to) return false;
        room->sendCompulsoryTriggerLog(owner, this);
        LogMessage log;
        log.type = "#xiongshou"; log.from = owner; log.arg = QString::number(damage.damage);
        log.arg2 = QString::number(damage.damage + getEffectiveAmount(ctx)); log.arg3 = objectName();
        room->sendLog(log);
        target->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
        return false;
    }
};

class XiongshouBf: public DistanceSkillV2 {
public:
    XiongshouBf(): DistanceSkillV2("#xiongshoubf") {
        setBaseAmount(-1);
        setHolderSelector(CorrectSkill_Primary);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override {
        return context.getPrimary() && context.getPrimary()->hasSkill("xiongshou")
            ? CorrectSkillResult::useAmount(context.getCurrentAmount())
            : CorrectSkillResult::noEffect();
    }
};

class Wuzang : public TriggerSkillV2
{
public:
    Wuzang() : TriggerSkillV2("wuzang") { events << EventPhaseStart; frequency = Compulsory; setBaseAmount(5); }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Draw ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.manual_effect = true; return skillEffect(event, room, owner, ctx, owner); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        target->drawCards(qMax(target->getHp() / 2, getEffectiveAmount(ctx)), objectName());
        return true; // This executed effect replaces the original draw phase.
    }
};

class WuzangZ: public MaxCardsSkillV2 {
public:
    WuzangZ(): MaxCardsSkillV2("#wuzang") {
        setBaseAmount(0);
        setHolderSelector(CorrectSkill_Primary);
    }

    CorrectSkillResult getFixedValue(const CorrectSkillContext &context) const override {
        return context.getPrimary() && context.getPrimary()->hasSkill("wuzang")
            ? CorrectSkillResult::useAmount(context.getCurrentAmount())
            : CorrectSkillResult::noEffect();
    }
};

class Xiangde : public TriggerSkillV2
{
public:
    Xiangde() : TriggerSkillV2("xiangde") { events << DamageCaused; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return damage.to && damage.to->isAlive() && damage.to->hasSkill(objectName()) && damage.from && damage.from != damage.to
            && damage.from->getWeapon() ? TriggerList{{damage.to, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override { ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target != ctx.original_data->value<DamageStruct>().to) return false;
        room->sendCompulsoryTriggerLog(owner, this);
        target->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
        return false;
    }
};

class Yinzei : public TriggerSkillV2
{
public:
    Yinzei() : TriggerSkillV2("yinzei") { events << Damage; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return damage.to && damage.to->isAlive() && damage.to->hasSkill(objectName()) && damage.to->isKongcheng()
            && damage.from && damage.from != damage.to && damage.to->canDiscard(damage.from, "he")
            ? TriggerList{{damage.to, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets << ctx.original_data->value<DamageStruct>().from; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            QList<int> ids;
            for (const Card *card : target->getCards("he")) if (owner->canDiscard(target, card->getEffectiveId())) ids << card->getEffectiveId();
            if (ids.isEmpty()) break;
            room->throwCard(ids.at(qsanRandomBounded(ids.size())), objectName(), target, owner);
        }
        return false;
    }
};

class Zhue : public TriggerSkillV2
{
public:
    Zhue() : TriggerSkillV2("zhue")
    {
        events << Damage;
        global = true;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.from || !damage.from->isAlive()) return result;
        foreach (ServerPlayer *owner, room->findPlayersBySkillName(objectName()))
            if (owner != player) result.insert(owner, {objectName()});
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ServerPlayer *p = ctx.owner;
        ServerPlayer *eventPlayer = ctx.invoker;
        if (p && p != eventPlayer && damage.from && damage.from->isAlive()) {
                room->broadcastSkillInvoke(objectName());
                room->sendCompulsoryTriggerLog(p, objectName());
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, p->objectName(), damage.from->objectName());
                QList<ServerPlayer *> players;
                players << p << damage.from;
                room->sortByActionOrder(players);
                ctx.targets = players;
                ctx.manual_effect = true;
                foreach (ServerPlayer *target, ctx.targets)
                    skillEffect(Damage, room, p, ctx, target);
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override {
        if (target) target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }

};

class Futai : public TriggerSkillV2
{
public:
    Futai() : TriggerSkillV2("futai")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *target, QVariant &) const override {
        return target && target->isAlive() && target->hasSkill(objectName()) && target->getPhase() == Player::RoundStart
            ? TriggerList{{target, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        ServerPlayer *target = ctx.owner;
        if (target->getPhase() == Player::RoundStart) {
            ctx.targets.clear();
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (p->isWounded()) {
                    ctx.targets << p;
                    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, target->objectName(), p->objectName());
                }
            }
            ctx.manual_effect = true;
            foreach (ServerPlayer *p, ctx.targets)
                skillEffect(EventPhaseStart, room, target, ctx, p);
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override {
        if (ctx.owner && target && target->isWounded())
            ctx.owner->getRoom()->recover(target, RecoverStruct(objectName(), ctx.owner,
                                                                 getEffectiveAmount(ctx)));
        return false;
    }
};

class FutaiLimit : public CardLimitSkill
{
public:
    FutaiLimit() : CardLimitSkill("#futai-limit")
    {
    }

    QString limitList(const Player *) const
    {
        return "use";
    }

    QString limitPattern(const Player *target) const
    {		
		foreach (const Player *p, target->getAliveSiblings()) {
			if (!p->hasFlag("CurrentPlayer") && p->hasSkill("futai"))
				return "Peach";
		}
		return "";
    }
};

class Yandu : public TriggerSkillV2
{
public:
    Yandu() : TriggerSkillV2("yandu") { events << EventPhaseStart; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::NotActive) return {};
        const QVariantMap page = room->queryActualDamage({{"turn_id", room->historyScopes().value("turn_id")}, {"from", player->objectName()}, {"limit", 1}});
        if (!page.value("complete").toBool() || !page.value("items").toList().isEmpty()) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != player && owner->canGet(player, "he")) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.invoker; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive() && owner->canGet(target, "he"); ++i) {
            const int id = room->askForCardChosen(owner, target, "he", objectName(), false, Card::MethodGet);
            if ((!target->handCards().contains(id) && !target->getEquipsId().contains(id)) || !owner->canGet(target, id)) break;
            const CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, owner->objectName(), target->objectName(), objectName(), "");
            room->obtainCard(owner, Sanguosha->getCard(id), reason, room->getCardPlace(id) != Player::PlaceHand);
        }
        return false;
    }
};

static QStringList mingwanDamageTargets(Room *room, const Player *source, bool *complete)
{
    *complete = false;
    QStringList targets;
    QVariantMap filter{{"turn_id", room->historyScopes().value("turn_id")}, {"from", source->objectName()}, {"limit", 128}};
    while (true) {
        const QVariantMap page = room->queryActualDamage(filter);
        if (!page.value("complete").toBool()) return {};
        for (const QVariant &value : page.value("items").toList()) {
            const QVariantMap damage = value.toMap().value("data").toMap();
            const QVariantMap card = damage.value("card").toMap();
            const QString target = damage.value("to").toString();
            if (!card.isEmpty() && card.value("type").toInt() != Card::TypeSkill && !target.isEmpty() && !targets.contains(target)) targets << target;
        }
        if (!page.value("has_more").toBool()) { *complete = true; return targets; }
        filter.insert("watermark", page.value("watermark"));
        filter.insert("after", page.value("next_after"));
    }
}

class Mingwan : public TriggerSkillV2
{
public:
    Mingwan() : TriggerSkillV2("mingwan")
    { events << Damage << CardUsed << CardResponded << TurnStart << EventAcquireSkill; frequency = Compulsory; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != Damage && event != TurnStart && event != EventAcquireSkill) return false;
        const QList<ServerPlayer *> players = event == TurnStart ? room->getAllPlayers(true) : QList<ServerPlayer *>{player};
        for (ServerPlayer *owner : players) {
            if (!owner) continue;
            bool complete = false;
            const QStringList targets = mingwanDamageTargets(room, owner, &complete);
            // Public projection only; server prohibition and activation always re-query the journal.
            room->setPlayerProperty(owner, "MingwanDamageTargets", complete ? targets : QStringList());
            room->setPlayerProperty(owner, "MingwanDamageHistoryKnown", complete);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if ((event != CardUsed && event != CardResponded) || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card
            : data.value<CardResponseStruct>().m_isUse ? data.value<CardResponseStruct>().m_card : nullptr;
        if (!card || card->isKindOf("SkillCard")) return {};
        bool complete = false;
        const QStringList targets = mingwanDamageTargets(room, player, &complete);
        return complete && !targets.isEmpty() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override { ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    { room->sendCompulsoryTriggerLog(owner, this); target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
};

class MingwanZ : public ProhibitSkill
{
public:
    MingwanZ() : ProhibitSkill("#mingwan") {}
    bool isProhibited(const Player *from, const Player *to, const Card *, const QList<const Player *> &) const override
    {
        if (!from || !to || from == to || !from->hasSkill("mingwan")) return false;
        QStringList targets;
        if (const auto *source = qobject_cast<const ServerPlayer *>(from)) {
            bool complete = false;
            targets = mingwanDamageTargets(source->getRoom(), source, &complete);
            // Unknown history cannot establish that the first damage has not happened yet.
            if (!complete) return true;
        } else {
            if (!from->property("MingwanDamageHistoryKnown").toBool()) return true;
            targets = from->property("MingwanDamageTargets").toStringList();
        }
        return !targets.isEmpty() && !targets.contains(to->objectName());
    }
};

class Nitai : public TriggerSkillV2
{
public:
    Nitai() : TriggerSkillV2("nitai") { events << DamageInflicted; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && damage.damage > 0
            && (player->hasFlag("CurrentPlayer") || damage.nature == DamageStruct::Fire) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override { ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (target != damage.to) return false;
        room->sendCompulsoryTriggerLog(owner, this);
        if (owner->hasFlag("CurrentPlayer")) return target->damageRevises(*ctx.original_data, -damage.damage);
        target->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
        return false;
    }
};

class Luanchang : public TriggerSkillV2
{
public:
    Luanchang() : TriggerSkillV2("luanchang") { events << EventPhaseStart; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(objectName()) && (player->getPhase() == Player::RoundStart || player->getPhase() == Player::NotActive) ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.choice = owner->getPhase() == Player::RoundStart ? "savage_assault" : "archery_attack"; ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            Card *card = Sanguosha->cloneCard(ctx.choice);
            if (!card) break;
            card->setSkillName("_" + objectName());
            CardUseStruct use;
            use.setOwnedCard(card);
            use.from = target;
            if (!card->isAvailable(target)) break;
            room->sendCompulsoryTriggerLog(owner, this);
            room->useCard(use);
        }
        return false;
    }
};

class Tanyu : public TriggerSkillV2
{
public:
    Tanyu() : TriggerSkillV2("tanyu") { events << EventPhaseStart << EventPhaseChanging; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::Discard) return {{player, {objectName()}}};
        if (event != EventPhaseStart || player->getPhase() != Player::Finish) return {};
        for (ServerPlayer *other : room->getOtherPlayers(player)) if (other->getHandcardNum() > player->getHandcardNum()) return {};
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override { ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        if (event == EventPhaseChanging) target->skip(Player::Discard);
        else room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), owner));
        return false;
    }
};

class Cangmu: public TriggerSkillV2 {
public:
    Cangmu(): TriggerSkillV2("cangmu") {
        events << DrawNCards;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override {
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override { ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override {
        if (target != ctx.invoker) return false;
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(player, objectName());
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num = room->alivePlayerCount() * getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class Jicai : public TriggerSkillV2
{
public:
    Jicai() : TriggerSkillV2("jicai")
    {
        events << HpRecover;
        global = true;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        foreach (ServerPlayer *owner, room->findPlayersBySkillName(objectName()))
            if (owner->isAlive()) result.insert(owner, {objectName()});
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        RecoverStruct recover = ctx.original_data->value<RecoverStruct>();
        ServerPlayer *p = ctx.owner;
        if (p) {
            room->sendCompulsoryTriggerLog(p, this);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, p->objectName(), ctx.invoker->objectName());
            QList<ServerPlayer *> players;
            players << p << ctx.invoker;
            room->sortByActionOrder(players);
            ctx.targets = players;
            ctx.manual_effect = true;
            foreach (ServerPlayer *target, ctx.targets)
                skillEffect(HpRecover, room, p, ctx, target);
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override {
        if (target) target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class Yaoshou: public DistanceSkillV2 {
public:
    Yaoshou(): DistanceSkillV2("yaoshou") {
        setBaseAmount(-2);
        setHolderSelector(CorrectSkill_Primary);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override {
        return context.getPrimary() && context.getPrimary()->hasSkill(objectName())
            ? CorrectSkillResult::useAmount(context.getCurrentAmount())
            : CorrectSkillResult::noEffect();
    }
};

class Fengdong : public InvaliditySkill
{
public:
    Fengdong() : InvaliditySkill("fengdong")
    {
    }

    bool isSkillValid(const Player *player, const Skill *skill) const
    {
        if (skill->getFrequency(player)!=Skill::Compulsory){
			foreach (const Player *p, player->getAliveSiblings()) {
				if (p->hasFlag("CurrentPlayer")&&p->hasSkill(objectName()))
					return false;
			}
		}
        return true;
    }
};

class BossXunyou : public TriggerSkillV2
{
public:
    BossXunyou() : TriggerSkillV2("boss_xunyou") { events << EventPhaseChanging; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || data.value<PhaseChangeStruct>().from != Player::NotActive) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) if (owner != player) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        QList<int> candidates;
        for (ServerPlayer *other : room->getOtherPlayers(owner))
            for (const Card *card : other->getCards("hej")) if (owner->canGet(other, card->getEffectiveId())) candidates << card->getEffectiveId();
        qsanShuffle(candidates);
        QVariantList ids;
        for (int id : candidates) {
            if (ids.size() >= getEffectiveAmount(ctx)) break;
            ServerPlayer *target = room->getCardOwner(id);
            if (!target) continue;
            ids << id;
            if (!ctx.targets.contains(target)) ctx.targets << target;
        }
        ctx.extra_data = ids;
        return !ids.isEmpty();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        for (const QVariant &value : ctx.extra_data.toList()) {
            const int id = value.toInt();
            if (room->getCardOwner(id) != target || !owner->canGet(target, id)) continue;
            const Player::Place place = room->getCardPlace(id);
            if (place != Player::PlaceHand && place != Player::PlaceEquip && place != Player::PlaceDelayedTrick) continue;
            const Card *card = Sanguosha->getCard(id);
            const CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, owner->objectName(), target->objectName(), objectName(), "");
            room->obtainCard(owner, card, reason, place != Player::PlaceHand);
            if (owner->isAlive() && card->isKindOf("EquipCard") && owner->handCards().contains(id) && card->isAvailable(owner))
                room->useCard(CardUseStruct(card, owner, QList<ServerPlayer *>()));
        }
        return false;
    }
};

class Sipu : public TriggerSkillV2
{
public:
    Sipu() : TriggerSkillV2("sipu")
    {
        events << PreCardUsed << CardResponded << EventPhaseStart << EventPhaseChanging << EventAcquireSkill;
        frequency = Compulsory;
        global = true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return false;
        const bool leavingPlay = event == EventPhaseChanging && data.value<PhaseChangeStruct>().from == Player::Play;
        const int count = !leavingPlay && player->getPhase() == Player::Play
            ? room->countHistoryCards(player, "phase", QString(), false, true) : -1;
        // This public projection mirrors the room facts; it is never an independent usage counter.
        room->setPlayerProperty(player, "SipuUsedCards", count);
        return false;
    }
};

class SipuBf : public CardLimitSkill
{
public:
    SipuBf() : CardLimitSkill("#sipubf") {}
    QString limitList(const Player *) const override { return "use,response"; }
    QString limitPattern(const Player *target) const override
    {
        if (!target) return QString();
        const ServerPlayer *server = qobject_cast<const ServerPlayer *>(target);
        for (const Player *owner : target->getAliveSiblings()) {
            if (owner->getPhase() != Player::Play || !owner->hasSkill("sipu")) continue;
            const ServerPlayer *serverOwner = qobject_cast<const ServerPlayer *>(owner);
            const int count = server && serverOwner
                ? server->getRoom()->countHistoryCards(serverOwner, "phase", QString(), false, true)
                : owner->property("SipuUsedCards").isValid() ? owner->property("SipuUsedCards").toInt() : -1;
            // Keep responses unavailable until the authoritative count proves the window has ended.
            if (count <= 2) return ".";
        }
        return QString();
    }
};

class Duqu : public FilterSkill
{
public:
    Duqu() : FilterSkill("duqu")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->isKindOf("Peach");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->setSkillName(objectName());/*
        WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
        card->takeOver(slash);*/
        return slash;
    }
};

static void applyGodPoison(Room *room, const SkillContext &ctx, ServerPlayer *target, int amount)
{
    if (!ctx.owner || !target || amount <= 0) return;
    QVariantList receipts = target->getTag("GodPoisonReceipts").toList();
    bool merged = false;
    for (QVariant &value : receipts) {
        QVariantMap receipt = value.toMap();
        if (receipt.value("owner").toString() != ctx.sourceRef.ownerObjectName
            || receipt.value("skill").toString() != ctx.sourceRef.key.skillName
            || receipt.value("instance").toInt() != ctx.sourceRef.key.instanceID) continue;
        receipt.insert("count", receipt.value("count").toInt() + amount);
        value = receipt; merged = true; break;
    }
    if (!merged) receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
        {"instance", ctx.sourceRef.key.instanceID}, {"count", amount}};
    target->setTag("GodPoisonReceipts", receipts);
    room->addPlayerMark(target, "&boss_shendu+#" + ctx.sourceRef.ownerObjectName, amount);
}

class DuquBf : public TriggerSkillV2
{
public:
    DuquBf() : TriggerSkillV2("#duqubf")
    { events << Damaged << EventPhaseChanging; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = event == Damaged ? data.value<DamageStruct>() : DamageStruct();
        return event == Damaged && player && player->isAlive() && player->hasSkill("duqu")
            && damage.to == player && damage.from && damage.from != player && damage.from->isAlive()
            ? TriggerList{{player, {objectName() + "->" + damage.from->objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseChanging) return false;
        if (!player || !player->isAlive() || data.value<PhaseChangeStruct>().from != Player::NotActive) return true;
        QStringList owners;
        const QVariantList receipts = player->getTag("GodPoisonReceipts").toList();
        for (const QVariant &value : receipts) {
            const QVariantMap receipt = value.toMap();
            const QString name = receipt.value("owner").toString();
            if (owners.contains(name) || receipt.value("count").toInt() <= 0) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(name, true);
            if (!owner) continue;
            owners << name;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.initiator = owner; ctx.invoker = player;
            ctx.sourceRef = SkillInstanceRef(name, SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            // One continuation per poison owner; consuming the first grant must not enqueue a second aggregate.
            ctx.instanceID = 0;
            ctx.amount = 0;
            for (const QVariant &item : receipts)
                if (item.toMap().value("owner").toString() == name) ctx.amount += item.toMap().value("count").toInt();
            ctx.targets << player; ctx.original_data = &data; ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override
    { return ctx.activationRef.isValid() ? ctx.owner : ctx.invoker; }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.invoker) return false;
        for (const QVariant &value : ctx.invoker->getTag("GodPoisonReceipts").toList())
            if (value.toMap().value("owner").toString() == ctx.sourceRef.ownerObjectName
                && value.toMap().value("count").toInt() > 0) return true;
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == Damaged) { applyGodPoison(room, ctx, target, getEffectiveAmount(ctx)); return false; }
        // Already-applied poison still resolves after its granting skill or owner is gone.
        const int amount = getEffectiveAmount(ctx);
        const Card *discard = amount > 0 && target->getCardCount() >= amount
            ? room->askForDiscard(target, "duqu", amount, amount, true, true, "duqu0:" + QString::number(amount)) : nullptr;
        if (!discard && amount > 0) room->loseHp(target, amount, true, owner, "duqu");
        QVariantList remaining;
        bool consumed = false;
        for (const QVariant &value : ctx.invoker->getTag("GodPoisonReceipts").toList()) {
            QVariantMap receipt = value.toMap();
            if (!consumed && receipt.value("owner").toString() == ctx.sourceRef.ownerObjectName
                && receipt.value("count").toInt() > 0) {
                receipt.insert("count", receipt.value("count").toInt() - 1);
                consumed = true;
            }
            if (receipt.value("count").toInt() > 0) remaining << receipt;
        }
        ctx.invoker->setTag("GodPoisonReceipts", remaining);
        if (consumed) room->removePlayerMark(ctx.invoker, "&boss_shendu+#" + ctx.sourceRef.ownerObjectName);
        return false;
    }
};

class Jiushou: public MaxCardsSkillV2 {
public:
    Jiushou(): MaxCardsSkillV2("jiushou") {
        setBaseAmount(9);
        setHolderSelector(CorrectSkill_Primary);
    }

    CorrectSkillResult getFixedValue(const CorrectSkillContext &context) const override {
        return context.getPrimary() && context.getPrimary()->hasSkill(objectName())
            ? CorrectSkillResult::useAmount(context.getCurrentAmount())
            : CorrectSkillResult::noEffect();
    }
};

class JiushouBf : public TriggerSkillV2
{
public:
    JiushouBf() : TriggerSkillV2("#jiushoubf") { events << EventPhaseStart << EventPhaseChanging; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill("jiushou")) return {};
        const Player::Phase phase = event == EventPhaseStart ? player->getPhase() : data.value<PhaseChangeStruct>().to;
        return (event == EventPhaseStart ? phase == Player::Play : phase == Player::Draw || phase == Player::NotActive)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override { ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(owner, "jiushou");
        if (event == EventPhaseChanging && ctx.original_data->value<PhaseChangeStruct>().to == Player::Draw) target->skip(Player::Draw);
        else {
            const int count = target->getMaxCards() - target->getHandcardNum();
            if (count > 0) target->drawCards(count, "jiushou");
        }
        return false;
    }
};

class Echou : public TriggerSkillV2
{
public:
    Echou() : TriggerSkillV2("echou") { events << CardUsed; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.from || !use.from->isAlive() || !use.card || !(use.card->isKindOf("Peach") || use.card->isKindOf("Analeptic"))) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != use.from) result.insert(owner, {objectName() + "->" + use.from->objectName()});
        return result;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { applyGodPoison(room, ctx, target, getEffectiveAmount(ctx)); return false; }
};

class Bingxian : public TriggerSkillV2
{
public:
    Bingxian() : TriggerSkillV2("bingxian") { events << EventPhaseChanging; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || data.value<PhaseChangeStruct>().to != Player::NotActive
            || room->countHistoryCards(player, "turn", "Slash", false, true) != 0) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) if (owner != player) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.invoker; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int i = 0; i < getEffectiveAmount(ctx) && owner->isAlive() && target->isAlive(); ++i) {
            Slash *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName("_" + objectName());
            CardUseStruct use;
            use.setOwnedCard(slash);
            use.from = owner;
            use.to << target;
            if (!owner->canUse(slash, target)) break;
            room->sendCompulsoryTriggerLog(owner, this);
            room->useCard(use);
        }
        return false;
    }
};

class Juyuan: public TargetModSkillV2 {
public:
    Juyuan(): TargetModSkillV2("juyuan") {
        setBaseAmount(1);
        setHolderSelector(CorrectSkill_Primary);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override {
        if (context.getModType() != TargetModSkill::ExtraTarget || !context.getPrimary()
            || context.getPrimary()->getPhase() != Player::Play
            || context.getPrimary()->getHp() >= context.getStateValue("hp", 0).toInt()
            || !context.getPrimary()->hasSkill(objectName()))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(context.getCurrentAmount());
    }
};

class JuyuanBf : public TriggerSkillV2 {
public:
    JuyuanBf() : TriggerSkillV2("#juyuanbf") {
        events << EventPhaseChanging;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override {
        return player && player->isAlive() && player->hasSkill("juyuan")
            && data.value<PhaseChangeStruct>().to == Player::NotActive
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const SkillInstance *helper = owner->findSkillInstance(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
        if (!helper) return false;
        const SkillInstanceRef parent = helper->parentRef;
        ServerPlayer *source = room->findPlayerByObjectName(parent.ownerObjectName, true);
        if (source && parent.key.skillName == "juyuan" && source->hasSkillInstance("juyuan", parent.key.instanceID))
            source->setSkillInstanceCorrectStateValue("juyuan", parent.key.instanceID, "hp", target->getHp());
        return false;
    }
};

class BossXushi : public TriggerSkillV2
{
public:
    BossXushi() : TriggerSkillV2("boss_xushi") { events << EventPhaseEnd << TurnedOver; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && (event == TurnedOver ? player->faceUp() : player->getPhase() == Player::Play)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets = event == TurnedOver ? room->getOtherPlayers(owner) : QList<ServerPlayer *>{owner}; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        if (event == TurnedOver) room->damage(DamageStruct(objectName(), owner, target, (1 + qsanRandomBounded(2)) * getEffectiveAmount(ctx)));
        else target->turnOver();
        return false;
    }
};

class BossZhaohuo : public TriggerSkillV2
{
public:
    BossZhaohuo() : TriggerSkillV2("boss_zhaohuo")
    { events << ConfirmDamage << DamageForseen << EventPhaseChanging; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        if (event == EventPhaseChanging)
            return data.value<PhaseChangeStruct>().from == Player::NotActive ? TriggerList{{player, {objectName()}}} : TriggerList();
        const DamageStruct damage = data.value<DamageStruct>();
        const bool applies = event == ConfirmDamage ? damage.from == player && damage.nature != DamageStruct::Fire
            : damage.to == player && damage.nature == DamageStruct::Fire;
        return applies ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.targets = event == EventPhaseChanging ? room->getOtherPlayers(owner)
            : QList<ServerPlayer *>{ctx.original_data->value<DamageStruct>().to};
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event != DamageForseen) return false;
        ctx.manual_effect = true;
        return skillEffect(event, room, owner, ctx, ctx.targets.first());
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == EventPhaseChanging) {
            const QString reason = objectName() + ":" + QString::number(ctx.executionID);
            target->addEquipsNullified("Armor", reason);
            QStringList reasons = target->getTag("ZhaohuoArmorReasons").toStringList();
            reasons << reason;
            target->setTag("ZhaohuoArmorReasons", reasons);
            return false;
        }
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (target != damage.to) return false;
        room->sendCompulsoryTriggerLog(owner, this);
        if (event == DamageForseen) return damage.nature == DamageStruct::Fire;
        LogMessage log; log.type = "#boss_zhaohuo"; log.from = damage.from; log.arg = "fire_nature";
        room->sendLog(log);
        damage.nature = DamageStruct::Fire;
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

class ZhaohuoBf : public TriggerSkillV2
{
public:
    ZhaohuoBf() : TriggerSkillV2("#boss_zhaohuobf") { events << EventPhaseChanging; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        // Cleanup is driven by applied effects, so losing the grant cannot strand nullification.
        for (ServerPlayer *target : room->getAllPlayers(true)) {
            const QStringList reasons = target->getTag("ZhaohuoArmorReasons").toStringList();
            target->removeTag("ZhaohuoArmorReasons");
            for (const QString &reason : reasons) target->removeEquipsNullified("Armor", reason);
        }
        return false;
    }
};

class Honglian : public TriggerSkillV2
{
public:
    Honglian() : TriggerSkillV2("honglian") { events << EventPhaseChanging; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        return player && player->isAlive() && player->hasSkill(objectName())
            && (change.from == Player::NotActive || change.to == Player::Discard)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "damage") {
            room->damage(DamageStruct(objectName(), owner, target, getEffectiveAmount(ctx), DamageStruct::Fire));
            return false;
        }
        if (ctx.original_data->value<PhaseChangeStruct>().to == Player::Discard) {
            for (const Card *card : target->getHandcards()) if (card->isRed()) room->ignoreCards(target, card);
            return false;
        }
        const int wanted = qsanRandomBounded(4);
        DummyCard cards;
        for (int id : room->getDrawPile())
            if (cards.subcardsLength() < wanted && Sanguosha->getCard(id)->isRed()) cards.addSubcard(id);
        const int count = cards.subcardsLength();
        if (count > 0) room->obtainCard(target, &cards);
        QList<ServerPlayer *> candidates = room->getOtherPlayers(target), selected;
        for (int i = 0; i < 3 - count && !candidates.isEmpty(); ++i) selected << candidates.takeAt(qsanRandomBounded(candidates.size()));
        room->sortByActionOrder(selected);
        ctx.choice = "damage";
        for (ServerPlayer *victim : selected) skillEffect(event, room, owner, ctx, victim);
        return false;
    }
};

class BossYanyu : public TriggerSkillV2
{
public:
    BossYanyu() : TriggerSkillV2("boss_yanyu")
    { events << EventPhaseChanging; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || data.value<PhaseChangeStruct>().from != Player::NotActive) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != player) result.insert(owner, {objectName() + "->" + player->objectName()});
        return result;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "damage") {
            room->damage(DamageStruct(objectName(), owner, target, getEffectiveAmount(ctx), DamageStruct::Fire));
            return false;
        }
        for (int i = 0; i < 3 && target->isAlive(); ++i) {
            JudgeStruct judge; judge.who = target; judge.reason = objectName(); judge.good = false; judge.pattern = ".|red";
            room->judge(judge);
            if (!judge.isBad()) break;
            ctx.choice = "damage";
            skillEffect(event, room, owner, ctx, target);
            ctx.choice.clear();
        }
        return false;
    }
};

class Jielve : public TriggerSkillV2
{
public:
    Jielve() : TriggerSkillV2("jielve") { events << Damage; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && damage.from == player
            && damage.to && damage.to != player && player->canGet(damage.to, "hej")
            ? TriggerList{{player, {objectName() + "->" + damage.to->objectName()}}} : TriggerList();
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "loseHp") {
            room->loseHp(HpLostStruct(target, 1, objectName(), owner));
            return false;
        }
        DummyCard cards;
        for (const QString &zone : QStringList{"h", "e", "j"})
            for (int i = 0; i < getEffectiveAmount(ctx); ++i) {
                QList<int> disabled = cards.getSubcards();
                bool available = false;
                for (const Card *card : target->getCards(zone)) {
                    if (!owner->canGet(target, card->getEffectiveId())) disabled << card->getEffectiveId();
                    else if (!disabled.contains(card->getEffectiveId())) available = true;
                }
                if (!available) break;
                const int id = room->askForCardChosen(owner, target, zone, objectName(), false, Card::MethodGet, disabled);
                if (id < 0 || disabled.contains(id) || room->getCardOwner(id) != target || !owner->canGet(target, id)) break;
                cards.addSubcard(id);
            }
        DummyCard obtainable;
        // Later selection callbacks may have moved an earlier card out of the original recipient's zones.
        for (int id : cards.getSubcards()) {
            const Player::Place place = room->getCardPlace(id);
            if (room->getCardOwner(id) == target && owner->canGet(target, id)
                && (place == Player::PlaceHand || place == Player::PlaceEquip || place == Player::PlaceDelayedTrick))
                obtainable.addSubcard(id);
        }
        if (obtainable.subcardsLength() > 0) {
            room->obtainCard(owner, &obtainable, CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, owner->objectName()), false);
            ctx.choice = "loseHp";
            skillEffect(event, room, owner, ctx, owner);
        }
        return false;
    }
};

class Longying : public TriggerSkillV2
{
public:
    Longying() : TriggerSkillV2("longying") { events << EventPhaseStart; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        ServerPlayer *lord = room->getLord();
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
            && player->getHp() > 0 && lord && lord->isAlive() && lord->isWounded()
            ? TriggerList{{player, {objectName() + "->" + lord->objectName()}}} : TriggerList();
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &) const override
    { if (owner->getHp() <= 0) return false; room->loseHp(HpLostStruct(owner, 1, objectName(), owner)); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->recover(target, RecoverStruct(objectName(), owner, getEffectiveAmount(ctx)));
        if (target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class Fangong : public TriggerSkillV2
{
public:
    Fangong() : TriggerSkillV2("fangong") { events << CardFinished; global = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.from || !use.from->isAlive() || !use.card || use.card->getTypeId() == Card::TypeSkill) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != use.from && use.to.contains(owner)) result.insert(owner, {objectName() + "->" + use.from->objectName()});
        return result;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &, ServerPlayer *target) const override
    {
        // The event's card user is the counterattack target; callback player denotes the V2 owner.
        room->askForUseSlashTo(owner, target, QString("@fangong-slash:%1").arg(target->objectName()), false);
        return false;
    }
};

class Huying : public TriggerSkillV2
{
public:
    Huying() : TriggerSkillV2("huying") { events << EventPhaseStart; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        ServerPlayer *lord = room->getLord();
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
            && lord && lord->isAlive() && lord != player
            ? TriggerList{{player, {objectName() + "->" + lord->objectName()}}} : TriggerList();
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "loseHp") { room->loseHp(HpLostStruct(target, 1, objectName(), owner)); return false; }
        QList<int> ids;
        for (const Card *card : owner->getHandcards()) if (card->isKindOf("Slash")) ids << card->getEffectiveId();
        if (!ids.isEmpty() && room->askForYiji(owner, ids, objectName(), false, false, true, getEffectiveAmount(ctx),
            QList<ServerPlayer *>{target}, CardMoveReason(), "@huying-distribute", true)) return false;
        ctx.choice = "loseHp";
        skillEffect(event, room, owner, ctx, owner);
        if (!target->isAlive()) return false;
        QList<int> slashes;
        for (int id : room->getDrawPile()) if (Sanguosha->getCard(id)->isKindOf("Slash")) slashes << id;
        DummyCard cards;
        for (int i = 0; i < getEffectiveAmount(ctx) && !slashes.isEmpty(); ++i) cards.addSubcard(slashes.takeAt(qsanRandomBounded(slashes.size())));
        if (cards.subcardsLength() > 0) room->obtainCard(target, &cards);
        return false;
    }
};

class BossTunjun : public TriggerSkillV2
{
public:
    BossTunjun() : TriggerSkillV2("boss_tunjun") { events << RoundStart; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getMaxHp() > 1
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->loseMaxHp(target, getEffectiveAmount(ctx));
        if (target->isAlive()) target->drawCards(target->getMaxHp(), objectName());
        return false;
    }
};

class Jiaoxia: public MaxCardsSkillV2 {
public:
    Jiaoxia(): MaxCardsSkillV2("jiaoxia") {
        setHolderSelector(CorrectSkill_AllHolders);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override {
        const Player *target = context.getPrimary();
        const Player *owner = context.getHolder();
        if (!target || !owner || !owner->hasSkill(objectName())
            || (owner != target && !owner->isYourFriend(target)))
            return CorrectSkillResult::noEffect();
        int amount = 0;
        foreach (const Card *card, target->getHandcards())
            if (card->isBlack()) ++amount;
        return CorrectSkillResult::useAmount(amount * context.getCurrentAmount());
    }
};

class BossFengying: public ProhibitSkill {
public:
    BossFengying(): ProhibitSkill("boss_fengying")
	{
    }

    bool isProhibited(const Player *from, const Player *to, const Card *, const QList<const Player *> &) const{
        if (!from->isYourFriend(to)){
			bool can = false;
			foreach (const Player *p, to->getAliveSiblings()) {
				if (p->isYourFriend(to)) {
					if (to->getHp()>=p->getHp()) return false;
					can = true;
				}
			}
			if (can){
				if (to->hasSkill(objectName())) return true;
				foreach (const Player *p, to->getAliveSiblings()) {
					if (p->hasSkill(objectName())&&p->isYourFriend(to))
						return true;
				}
			}
		}
        return false;
    }
};

KuangxiCard::KuangxiCard() {
    setSkillName("kuangxi");
}

bool KuangxiCard::targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const{
    return Self != to_select;
}

void KuangxiCard::onEffect(CardEffectStruct &effect) const{
    effect.from->getRoom()->damage(DamageStruct("kuangxi", effect.from, effect.to));
    effect.from->getRoom()->loseHp(effect.from);
}

class KuangxiViewAsSkill : public ViewAsSkillV2
{
public:
    KuangxiViewAsSkill() : ViewAsSkillV2("kuangxi") {}
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *owner = ctx.owner ? ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true) : nullptr;
        return owner && ref.isValid() && !owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "blocked").toBool();
    }
    // This quota is consumed by the resulting QuitDying, not by each activation.
    void addUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *owner = ctx.owner ? ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true) : nullptr;
        if (owner && ref.isValid()) owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "blocked", true);
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *owner = ctx.owner ? ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true) : nullptr;
        if (owner && ref.isValid()) owner->removeSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "blocked");
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            || request.initiator->getHp() <= 0 || !request.activationRef.isValid()) return false;
        for (const Player *owner : request.initiator->getAliveSiblings(true))
            if (owner->objectName() == request.activationRef.ownerObjectName)
                return !owner->getSkillInstanceStateValue(request.activationRef.key.skillName,
                    request.activationRef.key.instanceID, "blocked").toBool();
        return false;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    { return request.initiator && selected.isEmpty() && candidate && candidate->isAlive() && candidate != request.initiator; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    { return targets.size() == 1; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new ActiveSkillCard;
        card->setActiveSkill(this);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "KuangxiCard"; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.invoker || ctx.invoker->getHp() <= 0 || !isUsable(ctx)) return false;
        room->loseHp(HpLostStruct(ctx.invoker, 1, objectName(), ctx.invoker));
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !target || !target->isAlive() || !ctx.use_card) return ContinueEffects;
        const SkillInstanceRef ref = getUsageRef(ctx);
        // Keep the immutable quota source on this use, even if the damage actor changes.
        ctx.use_card->setTag("KuangxiSource", QVariantMap{{"owner", ref.ownerObjectName},
            {"skill", ref.key.skillName}, {"instance", ref.key.instanceID}});
        DamageStruct damage(ctx.use_card, ctx.invoker, target, getEffectiveAmount(ctx));
        damage.reason = objectName();
        ctx.invoker->getRoom()->damage(damage);
        return ContinueEffects;
    }
};

class Kuangxi : public TriggerSkillV2
{
public:
    Kuangxi() : TriggerSkillV2("kuangxi")
    { events << QuitDying << EventPhaseChanging; global = true; view_as_skill = new KuangxiViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to == Player::NotActive)
                for (ServerPlayer *owner : room->getAllPlayers(true))
                    for (int id : owner->getSkillInstanceIds(objectName())) {
                        SkillContext ctx; ctx.owner = owner;
                        ctx.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(objectName(), id));
                        ctx.sourceRef = ctx.activationRef;
                        view_as_skill->resetUsage(ctx);
                    }
            return false;
        }
        const DyingStruct dying = data.value<DyingStruct>();
        if (!dying.damage || dying.damage->getReason() != objectName() || dying.damage->chain
            || dying.damage->transfer || !dying.damage->card) return false;
        const QVariantMap receipt = dying.damage->card->getTag("KuangxiSource").toMap();
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        const int id = receipt.value("instance").toInt();
        if (owner && owner->getSkillInstanceIds(objectName()).contains(id)) {
            SkillContext ctx; ctx.owner = owner;
            ctx.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(objectName(), id));
            ctx.sourceRef = ctx.activationRef;
            view_as_skill->addUsage(ctx);
        }
        return false;
    }
};

class Baoying : public TriggerSkillV2
{
public:
    Baoying() : TriggerSkillV2("baoying")
    { frequency = Limited; events << Dying; limit_mark = "@baoying"; global = true; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->getMark(limit_mark) > 0; }
    void addUsage(const SkillContext &ctx) const override
    { if (ctx.owner) ctx.owner->getRoom()->removePlayerMark(ctx.owner, limit_mark); }
    void resetUsage(const SkillContext &ctx) const override
    { if (ctx.owner) ctx.owner->getRoom()->setPlayerMark(ctx.owner, limit_mark, 1); }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        ServerPlayer *who = data.value<DyingStruct>().who;
        if (!who || !who->isAlive() || who->getHp() >= 1) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner->isYourFriend(who) && owner->getMark(limit_mark) > 0)
                result.insert(owner, {objectName() + "->" + who->objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return isUsable(ctx) && ctx.owner->askForSkillInvoke(this, *ctx.original_data); }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive() && target->getHp() < getEffectiveAmount(ctx))
            room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx) - target->getHp()));
        return false;
    }
};

class Yangwu : public TriggerSkillV2
{
public:
    Yangwu() : TriggerSkillV2("yangwu")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Start
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (player->getPhase() == Player::Start) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(player, objectName());
            ctx.targets = room->getOtherPlayers(player);
            foreach (ServerPlayer *p, ctx.targets)
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), p->objectName());
            ctx.manual_effect = true;
            foreach (ServerPlayer *p, ctx.targets)
                skillEffect(EventPhaseStart, room, player, ctx, p);
            ctx.choice = "loseHp";
            skillEffect(EventPhaseStart, room, player, ctx, player);
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override {
        if (ctx.choice == "loseHp") { room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.owner)); return false; }
        if (ctx.owner && target)
            room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        return false;
    }
};

class Yanglie : public TriggerSkillV2
{
public:
    Yanglie() : TriggerSkillV2("yanglie")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Start
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (player->getPhase() == Player::Start) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(player, objectName());
            ctx.targets = room->getOtherPlayers(player);
            foreach (ServerPlayer *p, ctx.targets)
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), p->objectName());
            ctx.manual_effect = true;
            foreach (ServerPlayer *p, ctx.targets)
                skillEffect(EventPhaseStart, room, player, ctx, p);
            ctx.choice = "loseHp";
            skillEffect(EventPhaseStart, room, player, ctx, player);
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override {
        if (ctx.choice == "loseHp") { room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.owner)); return false; }
        for (int i = 0; ctx.owner && target && i < getEffectiveAmount(ctx) && ctx.owner->canGet(target, "hej"); ++i) {
            const int id = room->askForCardChosen(ctx.owner, target, "hej", objectName());
            if (id < 0 || room->getCardOwner(id) != target || !ctx.owner->canGet(target, id)) break;
            const CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, ctx.owner->objectName());
            room->obtainCard(ctx.owner, Sanguosha->getCard(id), reason, false);
        }
        return false;
    }
};

class Ruiqi : public TriggerSkillV2
{
public:
    Ruiqi() : TriggerSkillV2("ruiqi") { events << DrawNCards; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive())
            for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
                if (owner->isYourFriend(player)) result.insert(owner, {objectName() + "->" + player->objectName()});
        return result;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target != ctx.invoker) return false;
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num += getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class Jingqi: public DistanceSkillV2 {
public:
    Jingqi(): DistanceSkillV2("jingqi") {
        setBaseAmount(-1);
        setHolderSelector(CorrectSkill_AllHolders);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override {
        const Player *from = context.getPrimary();
        const Player *to = context.getSecondary();
        const Player *owner = context.getHolder();
        if (!from || !to || !owner || !owner->hasSkill(objectName())
            || !owner->isYourFriend(from) || owner->isYourFriend(to))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(context.getCurrentAmount());
    }
};

class Mojun : public TriggerSkillV2
{
public:
    Mojun() : TriggerSkillV2("mojun") { events << Damage; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || damage.from != player || !damage.card || !damage.card->isKindOf("Slash")
            || !damage.to || damage.to->hasFlag("Global_DebutFlag") || damage.chain || damage.transfer) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner->isYourFriend(player)) result.insert(owner, {objectName() + "->" + player->objectName()});
        return result;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
        JudgeStruct judge; judge.who = target; judge.reason = objectName(); judge.good = true; judge.pattern = ".|black";
        room->judge(judge);
        if (judge.isGood()) {
            QList<ServerPlayer *> friends;
            for (ServerPlayer *player : room->getAlivePlayers()) if (target->isYourFriend(player)) friends << player;
            room->sortByActionOrder(friends);
            ctx.choice = "draw";
            for (ServerPlayer *player : friends) skillEffect(event, room, owner, ctx, player);
        }
        return false;
    }
};

class Moqu : public TriggerSkillV2
{
public:
    Moqu() : TriggerSkillV2("moqu")
    { events << EventPhaseChanging << Damaged; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player) return result;
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (event == EventPhaseChanging ? owner->getHandcardNum() < owner->getHp()
                : owner != player && owner->isYourFriend(player) && owner->canDiscard(owner, "h"))
                result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == EventPhaseChanging) target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        else room->askForDiscard(target, objectName(), getEffectiveAmount(ctx), getEffectiveAmount(ctx), false, false);
        return false;
    }
};

class GodBladeSkill : public WeaponSkillV2
{
public:
    GodBladeSkill() : WeaponSkillV2("god_blade", "god_blade") { events << TargetSpecified; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !WeaponSkillV2::triggerable(player) || !use.card || !use.card->isKindOf("Slash") || !use.card->isRed()) return {};
        QStringList targets;
        for (ServerPlayer *target : use.to) if (target->isAlive()) targets << target->objectName();
        return targets.isEmpty() ? TriggerList() : TriggerList{{player, {objectName() + "->" + targets.join('+')}}};
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.to.contains(target)) return false;
        room->sendCompulsoryTriggerLog(owner, this);
        // Target identity remains stable even when earlier targets have been declined or removed.
        if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName();
        ctx.original_data->setValue(use);
        return false;
    }
};

GodBlade::GodBlade(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("god_blade");
}

class GodDiagramSkill: public ArmorSkillV2 {
public:
    GodDiagramSkill(): ArmorSkillV2("god_diagram", "god_diagram") {
        events << CardEffected;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override {
        CardEffectStruct effect = data.value<CardEffectStruct>();
        return player && ArmorSkillV2::triggerable(player) && effect.card && effect.card->isKindOf("Slash")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override { ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override {
        if (target != ctx.original_data->value<CardEffectStruct>().to) return false;
        CardEffectStruct effect = ctx.original_data ? ctx.original_data->value<CardEffectStruct>() : CardEffectStruct();
        if (!player || !effect.card) return false;
        if (effect.card->isKindOf("Slash")) {
            LogMessage log;
            log.type = "#ArmorNullify";
            log.from = player;
            log.arg = objectName();
            log.arg2 = effect.card->objectName();
            player->getRoom()->sendLog(log);

            room->setEmotion(player, "armor/"+objectName());
            //effect.to->setFlags("Global_NonSkillNullify");
            return true;
        }
		return false;
    }
};

GodDiagram::GodDiagram(Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("god_diagram");
}

class GodPaoSkill: public ProhibitSkill {
public:
    GodPaoSkill(): ProhibitSkill("god_pao") {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const{
        return from != to && card->isNDTrick() && to->hasArmorEffect("god_pao");
    }
};

GodPao::GodPao(Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("god_pao");
}

class GodQinSkill: public WeaponSkillV2 {
public:
    GodQinSkill(): WeaponSkillV2("god_qin", "god_qin") {
        events << ConfirmDamage;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override {
        DamageStruct damage = data.value<DamageStruct>();
        return player && WeaponSkillV2::triggerable(player) && damage.nature != DamageStruct::Fire
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets << (ctx.original_data->value<DamageStruct>().to); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override {
        if (target != ctx.original_data->value<DamageStruct>().to) return false;
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.nature != DamageStruct::Fire) {
			room->sendCompulsoryTriggerLog(player, objectName());
            room->setEmotion(player, "weapon/"+objectName());
			damage.nature = DamageStruct::Fire;
            if (ctx.original_data) *ctx.original_data = QVariant::fromValue(damage);
		}
		return false;
    }
};

GodQin::GodQin(Suit suit, int number)
    : Weapon(suit, number, 4)
{
    setObjectName("god_qin");
}

class GodHalberdsSkill: public TargetModSkillV2 {
public:
    GodHalberdsSkill(): TargetModSkillV2("#god_halberd") {
        setBaseAmount(999);
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override {
        return context.getModType() == TargetModSkill::ExtraTarget && context.getPrimary()
            && context.getPrimary()->hasWeapon("god_halberd")
            ? CorrectSkillResult::useAmount(context.getCurrentAmount())
            : CorrectSkillResult::noEffect();
    }
};

GodHalberd::GodHalberd(Suit suit, int number)
    : Weapon(suit, number, 4)
{
    setObjectName("god_halberd");
}

class GodHalberdSkill: public WeaponSkillV2 {
public:
    GodHalberdSkill(): WeaponSkillV2("god_halberd", "god_halberd") {
        events << ConfirmDamage;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override {
        DamageStruct damage = data.value<DamageStruct>();
        return player && WeaponSkillV2::triggerable(player) && damage.card && damage.card->isKindOf("Slash")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets << (ctx.original_data->value<DamageStruct>().to); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override {
        if (target != ctx.original_data->value<DamageStruct>().to) return false;
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
		if (damage.card && damage.card->isKindOf("Slash")) {
			room->setEmotion(player, "weapon/"+objectName());
			LogMessage log;
			log.type = "#xiongshou";
			log.from = damage.from;
			log.arg = QString::number(damage.damage);
			damage.damage += getEffectiveAmount(ctx);
			log.arg2 = QString::number(damage.damage);
			log.arg3 = objectName();
			room->sendLog(log);
			room->notifySkillInvoked(player, objectName());
			if (ctx.original_data) *ctx.original_data = QVariant::fromValue(damage);
		}
		return false;
    }
};

class GodHalberdSkillBf: public WeaponSkillV2 {
public:
    GodHalberdSkillBf(): WeaponSkillV2("#god_halberdbf", "god_halberd") {
        events << DamageComplete;
        global = true;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *target, QVariant &data) const override {
        DamageStruct damage = data.value<DamageStruct>();
        if (!target || !target->isAlive() || !damage.card || !damage.card->isKindOf("Slash")
            || !damage.from || !damage.from->hasWeapon("god_halberd"))
            return TriggerList();
        Q_UNUSED(room);
        return TriggerList{{damage.from, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets << ctx.original_data->value<DamageStruct>().to; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
		if (damage.card && damage.card->isKindOf("Slash") && damage.from && damage.from->hasWeapon("god_halberd")) {
			room->recover(target, RecoverStruct("god_halberd", damage.from, getEffectiveAmount(ctx)));
		}
		return false;
    }
};

class GodHatSkill: public TreasureSkillV2 {
public:
    GodHatSkill(): TreasureSkillV2("god_hat", "god_hat") {
        events << DrawNCards;
        setBaseAmount(2);
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override {
		DrawStruct draw = data.value<DrawStruct>();
		return player && TreasureSkillV2::triggerable(player) && draw.reason == "draw_phase"
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets << (owner); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override {
        if (target != ctx.invoker) return false;
		DrawStruct draw = ctx.original_data->value<DrawStruct>();
		room->sendCompulsoryTriggerLog(player, objectName());
        room->setEmotion(player, "treasure/"+objectName());
		draw.num += getEffectiveAmount(ctx);
        if (ctx.original_data) *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

GodHat::GodHat(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("god_hat");
}

class GodHatBuff: public MaxCardsSkillV2 {
public:
    GodHatBuff(): MaxCardsSkillV2("#god_hat") {
        setBaseAmount(-1);
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override {
        return context.getPrimary() && context.getPrimary()->hasTreasure("god_hat")
            ? CorrectSkillResult::useAmount(context.getCurrentAmount())
            : CorrectSkillResult::noEffect();
    }
};

class GodSwordSkill : public WeaponSkillV2
{
public:
    GodSwordSkill() : WeaponSkillV2("god_sword", "god_sword") { events << TargetSpecified << CardFinished; frequency = Compulsory; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != CardFinished) return false;
        const Card *card = data.value<CardUseStruct>().card;
        if (!card) return false;
        const QVariantList receipts = card->getTag("GodSwordRestrictions").toList();
        const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        QVariantList remaining;
        for (const QVariant &value : receipts) if (value.toMap().value("use_event").toLongLong() != useEvent) remaining << value;
        card->setTag("GodSwordRestrictions", remaining);
        for (const QVariant &value : receipts) {
            if (value.toMap().value("use_event").toLongLong() != useEvent) continue;
            const QVariantMap receipt = value.toMap();
            ServerPlayer *target = room->findPlayerByObjectName(receipt.value("target").toString(), true);
            if (target) room->removePlayerCardLimitationByReason(target, receipt.value("reason").toString());
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return event == TargetSpecified && player && use.from == player && WeaponSkillV2::triggerable(player)
            && use.card && use.card->isKindOf("Slash") ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = ctx.original_data->value<CardUseStruct>().to; return !ctx.targets.isEmpty(); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const Card *card = ctx.original_data->value<CardUseStruct>().card;
        if (!card) return false;
        const QString reason = objectName() + ':' + QString::number(ctx.executionID);
        QVariantList receipts = card->getTag("GodSwordRestrictions").toList();
        const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        receipts << QVariantMap{{"target", target->objectName()}, {"reason", reason}, {"use_event", useEvent}};
        card->setTag("GodSwordRestrictions", receipts);
        // Cleanup consumes this exact reason even when the weapon/source/target list changes before CardFinished.
        room->setPlayerCardLimitation(target, "use,response", ".|.|.|hand", true, reason);
        target->addQinggangTag(card);
        room->sendCompulsoryTriggerLog(owner, this);
        room->setEmotion(owner, "weapon/" + objectName());
        return false;
    }
};

GodSword::GodSword(Suit suit, int number)
    : Weapon(suit, number, 2)
{
    setObjectName("god_sword");
}

class GodHorseSkill : public DistanceSkillV2
{
public:
    GodHorseSkill() : DistanceSkillV2("god_horse")
    { setBaseAmount(1); setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        const Player *from = context.getPrimary(), *to = context.getSecondary();
        if (!from || !to) return CorrectSkillResult::noEffect();
        const auto camp = [](const Player *player) {
            return player->getRole() == "lord" ? QString("loyalist") : player->getRole();
        };
        int count = 0;
        for (const Player *owner : from->getAliveSiblings(true))
            if (owner != to && owner->hasDefensiveHorse(objectName()) && camp(owner) != "renegade"
                && camp(owner) == camp(to) && camp(owner) != camp(from)) ++count;
        return count > 0 ? CorrectSkillResult::useAmount(count * context.getCurrentAmount()) : CorrectSkillResult::noEffect();
    }
};

GodHorse::GodHorse(Suit suit, int number)
    : DefensiveHorse(suit, number, +1)
{
    setObjectName("god_horse");
}




class GodDoubleSwordSkill : public WeaponSkillV2
{
public:
    GodDoubleSwordSkill() : WeaponSkillV2("god_double_sword", "god_double_sword")
    {
        events << TargetSpecified;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        CardUseStruct use = data.value<CardUseStruct>();
        TriggerList result;
        if (!player || !WeaponSkillV2::triggerable(player) || !use.card
            || !(use.card->isKindOf("ThunderSlash") || use.card->isKindOf("FireSlash")))
            return result;
        QStringList targets;
        foreach (ServerPlayer *to, use.to) targets << to->objectName();
        if (!targets.isEmpty()) result.insert(player, {objectName() + "->" + targets.join("+")});
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override {
        return ctx.owner && !ctx.targets.isEmpty() && ctx.owner->askForSkillInvoke(this, ctx.targets.first());
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
        room->setEmotion(owner, "weapon/" + objectName());
        if (target->canDiscard(target, "he") && room->askForCard(target, ".", "god_double_card0:" + owner->objectName(), *ctx.original_data)) return false;
        ctx.choice = "draw";
        skillEffect(event, room, owner, ctx, owner);
        return false;
    }
};

GodDoubleSword::GodDoubleSword(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("god_double_sword");
}

class GodDeerSkill: public EquipSkillV2 {
public:
    GodDeerSkill(): EquipSkillV2("god_deer", "god_deer") {
        events << DamageCaused;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *player) const override { return player && player->hasOffensiveHorse(objectName()); }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override {
        DamageStruct damage = data.value<DamageStruct>();
        return player && player->hasOffensiveHorse(objectName()) && damage.nature != DamageStruct::Normal
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets << ctx.original_data->value<DamageStruct>().to; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override {
        if (target != ctx.original_data->value<DamageStruct>().to) return false;
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
		if (damage.nature != DamageStruct::Normal) {
			room->setEmotion(player, "horse/"+objectName());
			LogMessage log;
			log.type = "#xiongshou";
			log.from = damage.from;
			log.arg = QString::number(damage.damage);
            damage.damage += getEffectiveAmount(ctx);
            log.arg2 = QString::number(damage.damage);
			log.arg3 = objectName();
			room->sendLog(log);
			room->notifySkillInvoked(player, objectName());
			if (ctx.original_data) *ctx.original_data = QVariant::fromValue(damage);
		}
		return false;
    }
};

GodDeer::GodDeer(Suit suit, int number)
    : OffensiveHorse(suit, number, -1)
{
    setObjectName("god_deer");
}

class GodBowSkill : public WeaponSkillV2
{
public:
    GodBowSkill() : WeaponSkillV2("god_bow", "god_bow") { events << CardsMoveOneTime; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return player && WeaponSkillV2::triggerable(player) && move.from == player
            && player->getPhase() == Player::Play && move.from_places.count(Player::PlaceHand) >= 2
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getOtherPlayers(owner))
            if (owner->canDiscard(target, "he")) candidates << target;
        const int count = ctx.original_data->value<CardsMoveOneTimeStruct>().from_places.count(Player::PlaceHand);
        ServerPlayer *target = room->askForPlayerChosen(owner, candidates, objectName(),
            "god_bow0:" + QString::number(count), true, true);
        if (!target) return false;
        ctx.targets << target;
        ctx.extra_data = count;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->setEmotion(owner, "weapon/" + objectName());
        DummyCard discard;
        for (int i = 0; i < ctx.extra_data.toInt() * getEffectiveAmount(ctx); ++i) {
            QList<int> disabled = discard.getSubcards();
            bool available = false;
            for (const Card *card : target->getCards("he")) {
                const int id = card->getEffectiveId();
                if (!owner->canDiscard(target, id)) disabled << id;
                else if (!disabled.contains(id)) available = true;
            }
            if (!available) break;
            const int id = room->askForCardChosen(owner, target, "he", objectName(), false, Card::MethodDiscard, disabled);
            if (id < 0 || disabled.contains(id) || room->getCardOwner(id) != target || !owner->canDiscard(target, id)) break;
            discard.addSubcard(id);
        }
        DummyCard payable;
        // Only discard the cards still owned by the selected recipient after all choice callbacks finish.
        for (int id : discard.getSubcards()) {
            const Player::Place place = room->getCardPlace(id);
            if (room->getCardOwner(id) == target && owner->canDiscard(target, id)
                && (place == Player::PlaceHand || place == Player::PlaceEquip)) payable.addSubcard(id);
        }
        if (payable.subcardsLength() > 0) room->throwCard(&payable, target, owner);
        return false;
    }
};

GodBow::GodBow(Suit suit, int number)
    : Weapon(suit, number, 9)
{
    setObjectName("god_bow");
}

class GodAxeViewAsSkill : public ViewAsSkillV2
{
public:
    GodAxeViewAsSkill() : ViewAsSkillV2("god_axe", 2) { response_pattern = "@god_axe"; }
    bool isEquipSkill() const override { return true; }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    { return targets.isEmpty(); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@god_axe"
            && request.initiator->hasWeapon("god_axe")
            && (request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.size() < 2
            && !request.selectedCardIds.contains(card->getEffectiveId()) && !card->hasFlag("using")
            && request.initiator->getCards("he").contains(card)
            && !(card->isEquipped() && card->objectName() == objectName())
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 2) return false;
        ActiveSkillRequest selection = request; selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (!canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new DummyCard(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }
};

class GodAxeSkill : public WeaponSkillV2
{
public:
    GodAxeSkill() : WeaponSkillV2("god_axe", "god_axe")
    { events << TargetSpecified; view_as_skill = new GodAxeViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && WeaponSkillV2::triggerable(player) && use.from == player && use.card
            && use.card->getTypeId() != Card::TypeSkill && use.to.size() == 1 && player->getPhase() == Player::Play
            ? TriggerList{{player, {objectName() + "->" + use.to.first()->objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (ctx.targets.isEmpty() || owner->getCardCount() < 2) return false;
        const Card *card = room->askForCard(owner, "@god_axe", "god_axe0:" + ctx.targets.first()->objectName(),
            *ctx.original_data, Card::MethodNone, nullptr, false, objectName());
        if (!card || card->subcardsLength() != 2) return false;
        QVariantList ids;
        for (int id : card->getSubcards()) ids << id;
        ctx.extra_data = ids;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        QList<int> ids;
        for (const QVariant &value : ctx.extra_data.toList()) {
            const int id = value.toInt();
            const Card *card = Sanguosha->getCard(id);
            if (!card || ids.contains(id) || !owner->getCards("he").contains(card)
                || (card->isEquipped() && card->objectName() == objectName()) || !owner->canDiscard(owner, id)) return false;
            ids << id;
        }
        if (ids.size() != 2) return false;
        DummyCard discard(ids);
        room->throwCard(&discard, owner, owner);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->setEmotion(owner, "weapon/" + objectName());
        const QString reason = objectName() + ":" + QString::number(ctx.executionID);
        room->setPlayerCardLimitation(target, "use,response", ".", true, reason);
        target->addEquipsNullified("Armor", reason);
        QStringList reasons = target->getTag("GodAxeRestrictions").toStringList();
        reasons << reason;
        target->setTag("GodAxeRestrictions", reasons);
        return false;
    }
};

class GodAxeSkillBf : public TriggerSkillV2
{
public:
    GodAxeSkillBf() : TriggerSkillV2("#god_axebf") { events << EventPhaseChanging; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        // Applied restrictions survive losing the weapon, and only their own reasons are removed.
        for (ServerPlayer *target : room->getAllPlayers(true)) {
            const QStringList reasons = target->getTag("GodAxeRestrictions").toStringList();
            target->removeTag("GodAxeRestrictions");
            for (const QString &reason : reasons) {
                room->removePlayerCardLimitationByReason(target, reason);
                target->removeEquipsNullified("Armor", reason);
            }
        }
        return false;
    }
};

GodAxe::GodAxe(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("god_axe");
}

class GodEdictSkill : public TreasureSkillV2
{
public:
    GodEdictSkill() : TreasureSkillV2("god_edict", "god_edict") { events << CardsMoveOneTime; global = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        ServerPlayer *current = room->getCurrent();
        if (room->getTag("FirstRound").toBool() || !move.to || move.card_ids.isEmpty() || !current
            || current->getPhase() == Player::NotActive || move.to == current || move.to_place != Player::PlaceHand) return result;
        const qint64 eventId = room->historyParent(room->currentHistoryEventId(), "move_cards", true).value("id").toLongLong();
        if (!eventId) return result;
        QVariantMap filter{{"turn_id", room->historyScopes().value("turn_id")}, {"to", move.to->objectName()}, {"limit", 128}};
        bool firstGain = false;
        while (true) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (!page.value("error").toString().isEmpty() || !page.value("complete").toBool()) return result;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap fact = value.toMap(), entry = fact.value("data").toMap();
                if (entry.value("to_place").toInt() != Player::PlaceHand) continue;
                // The first actual gain belongs to this move, regardless of when the treasure was equipped.
                firstGain = fact.value("event_id").toLongLong() == eventId
                    && move.card_ids.contains(entry.value("card_id").toInt());
                if (!firstGain) return result;
                break;
            }
            if (firstGain || !page.value("has_more").toBool()) break;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
        if (firstGain)
            for (ServerPlayer *owner : room->getAlivePlayers())
                if (owner != move.to && TreasureSkillV2::triggerable(owner))
                    result.insert(owner, {objectName() + "->" + move.to->objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { return !ctx.targets.isEmpty() && owner->askForSkillInvoke(this, ctx.targets.first()); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->setEmotion(owner, "treasure/" + objectName());
        const QVariant toData = QVariant::fromValue(target);
        const Card *card = room->askForCard(owner, "^GodEdict", "god_edict0:" + target->objectName(), toData, Card::MethodNone);
        if (card) room->giveCard(owner, target, card, objectName());
        else if (!target->isNude()) {
            card = room->askForExchange(target, objectName(), getEffectiveAmount(ctx), getEffectiveAmount(ctx), true,
                "god_edict1:" + owner->objectName());
            if (card) room->giveCard(target, owner, card, objectName());
        }
        return false;
    }
};

GodEdict::GodEdict(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("god_edict");
}

class GodHeaddressSkill : public TreasureSkillV2
{
public:
    GodHeaddressSkill() : TreasureSkillV2("god_headdress", "god_headdress") { events << EventPhaseEnd; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && TreasureSkillV2::triggerable(player) && player->getPhase() == Player::Play
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(this)) return false;
        ctx.targets << owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->setEmotion(owner, "treasure/" + objectName());
        const Card *card = room->askForCard(target, "^GodHeaddress", "god_headdress0:", *ctx.original_data, Card::MethodNone);
        if (card) target->addToPile(objectName(), card);
        else target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class GodHeaddressBf : public TreasureSkillV2
{
protected:
    bool usesEventSource(const SkillContext &ctx) const override
    {
        return ctx.owner && ctx.original_data && ctx.current_event == EventPhaseChanging
            && ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive
            && !ctx.owner->getPile("god_headdress").isEmpty();
    }
public:
    GodHeaddressBf() : TreasureSkillV2("#god_headdressbf", "god_headdress")
    { events << EventPhaseChanging; global = true; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (data.value<PhaseChangeStruct>().to == Player::NotActive)
            for (ServerPlayer *owner : room->getAlivePlayers())
                if (!owner->getPile("god_headdress").isEmpty()) result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &, ServerPlayer *target) const override
    {
        // The pile is the committed receipt and remains returnable after the treasure leaves play.
        const QList<int> ids = owner->getPile("god_headdress");
        if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards, false); }
        return false;
    }
};

GodHeaddress::GodHeaddress(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("god_headdress");
}

GodShip::GodShip(Suit suit, int number)
    : OffensiveHorse(suit, number, -2)
{
    setObjectName("god_ship");
}

void GodShip::onUninstall(ServerPlayer *player) const
{
	OffensiveHorse::onUninstall(player);
	if(!player->isAlive()) return;
	Room*room = player->getRoom();
	QList<ServerPlayer *> tos;
	foreach (ServerPlayer *to, room->getAlivePlayers()) {
		foreach (const Card*c, to->getCards("ej")) {
			if(c->getNumber()>1&&c->getNumber()<11&&!player->canDiscard(to,c->getId()))
				continue;
			tos << to;
			break;
		}
	}
	ServerPlayer *p = room->askForPlayerChosen(player,tos,objectName(),"god_ship0:",true,true);
	if(p){
		QList<int> ids;
		foreach (const Card*c, p->getCards("ej")) {
			if(c->getNumber()>1&&c->getNumber()<11&&!player->canDiscard(p,c->getId()))
				ids << c->getId();
			else if(c->getId()==getId())
				ids << c->getId();
		}
		int id = room->askForCardChosen(player,p,"ej",objectName(),false,Card::MethodNone,ids);
		if(id>-1){
			const Card*c = Sanguosha->getCard(id);
			if(c->getNumber()>1&&c->getNumber()<11)
				room->throwCard(c,objectName(),p,player);
			else
				player->obtainCard(c);
		}
	}
}


GodSlash::GodSlash(Suit suit, int number)
    : NatureSlash(suit, number, DamageStruct::God)
{
    setObjectName("_god_slash");
    damage_card = true;
    single_target = true;
}

GodNihilo::GodNihilo(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("god_nihilo");
}

bool GodNihilo::isAvailable(const Player *player) const
{
	QList<const Player *> targets = player->getAliveSiblings();
	targets << player;
	foreach (const Player *p, targets) {
		if(targetFilter(QList<const Player *>(), p, player))
			return SingleTargetTrick::isAvailable(player);
	}
	return false;
}

bool GodNihilo::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
	if (to_select->getKingdom() == "god" || to_select->getHandcardNum() <= qMin(to_select->getMaxHp(), 5)) {
		if (Self->isProhibited(to_select, this)) return false;
		if (targets.isEmpty()) return to_select == Self;
		return targets.length() <= Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this);
	}
	return false;
}

bool GodNihilo::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const{
    return Self->getKingdom() == "god" || Self->getHandcardNum() <= qMin(Self->getMaxHp(), 5) || targets.length() > 0;
}

void GodNihilo::onUse(Room *room, CardUseStruct &card_use) const{
    CardUseStruct use = card_use;
    if (use.to.isEmpty()) use.to << use.from;
    SingleTargetTrick::onUse(room, use);
}

void GodNihilo::onEffect(CardEffectStruct &effect) const{
    effect.to->drawCards(effect.to->getKingdom()=="god"?qMin(effect.to->getMaxHp(),5):qMin(effect.to->getMaxHp(),5)-effect.to->getHandcardNum(),objectName());
}

GodFlower::GodFlower(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("god_flower");
}

bool GodFlower::isAvailable(const Player *player) const
{
	foreach (const Player *p, player->getAliveSiblings()) {
		if(targetFilter(QList<const Player *>(), p, player))
			return SingleTargetTrick::isAvailable(player);
	}
	return false;
}

bool GodFlower::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
    return to_select != Self && targets.length() <= Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this)
	&& !Self->isProhibited(to_select, this);
}

void GodFlower::onEffect(CardEffectStruct &effect) const{
	QList<ServerPlayer *> targets;
    Room *room = effect.to->getRoom();
    foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if (effect.to->canSlash(p, false))
			targets << p;
    }
	if (!room->askForUseSlashTo(effect.to, targets, "@god_flower:"+effect.from->objectName(),true,false,false,effect.from,this)
		&& effect.to->getCardCount()>0 && effect.from->isAlive()) {
		const Card *card = room->askForExchange(effect.to, objectName(), 2, 2, true, "@god_flower0:"+effect.from->objectName());
		if(card) room->giveCard(effect.to, effect.from, card, objectName());
	}
}

GodSpeel::GodSpeel(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("god_speel");
}

bool GodSpeel::isAvailable(const Player *player) const
{
	foreach (const Player *p, player->getAliveSiblings()) {
		if(targetFilter(QList<const Player *>(), p, player))
			return SingleTargetTrick::isAvailable(player);
	}
	return false;
}

bool GodSpeel::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
	return to_select != Self && targets.length() <= Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this)
	&& !Self->isProhibited(to_select, this);
}

void GodSpeel::onEffect(CardEffectStruct &effect) const{
	QStringList sks,skills = effect.to->getTag("god_speelSkills").toStringList();
	Room *room = effect.to->getRoom();
	foreach(const Skill *skill, effect.to->getVisibleSkillList())
		if (!skill->isAttachedLordSkill()) sks << skill->objectName();
	if (sks.isEmpty()) return;
	QString sk = sks.at(qsanRandomBounded(sks.length()));
	if (!skills.contains(sk)){
		skills << sk;
		effect.to->setTag("god_speelSkills", skills);
		room->addPlayerMark(effect.to,"Qingcheng"+sk);
		room->setPlayerMark(effect.to,"&god_speel+:+"+sk,1);
	}
}

GodlailailaiPackage::GodlailailaiPackage()
    : Package("Godlailailai")
{
    General *zhuyin = new General(this, "zhuyin", "qun", 4, true, true);
    zhuyin->addSkill(new Xiongshou);
    zhuyin->addSkill(new XiongshouBf);
    related_skills.insert("xiongshou", "#xiongshoubf");

    General *hundun = new General(this, "hundun", "qun", 25, false, true);
    hundun->addSkill("xiongshou");
    hundun->addSkill(new Wuzang);
    hundun->addSkill(new WuzangZ);
    related_skills.insert("wuzang", "#wuzang");
    hundun->addSkill(new Xiangde);
    hundun->addRelateSkill("yinzei");

    General *qiongqi = new General(this, "qiongqi", "qun", 25, true, true);
	qiongqi->setStartHp(20);
    qiongqi->addSkill("xiongshou");
    qiongqi->addSkill(new Zhue);
    qiongqi->addSkill(new Futai);
    qiongqi->addSkill(new FutaiLimit);
    related_skills.insert("futai", "#futai-limit");
    qiongqi->addRelateSkill("yandu");

    General *taowu = new General(this, "taowu", "qun", 20, true, true);
    taowu->addSkill("xiongshou");
    taowu->addSkill(new Mingwan);
    taowu->addSkill(new MingwanZ);
    related_skills.insert("mingwan", "#mingwan");
    taowu->addSkill(new Nitai);
    taowu->addRelateSkill("luanchang");

    General *taotie = new General(this, "taotie", "qun", 25, false, true);
    taotie->addSkill("xiongshou");
    taotie->addSkill(new Tanyu);
    taotie->addSkill(new Cangmu);
    taotie->addRelateSkill("jicai");

    General *yingzhao = new General(this, "yingzhao", "qun", 30, true, true);
	yingzhao->setStartHp(25);
    yingzhao->addSkill(new Yaoshou);
    yingzhao->addSkill(new Fengdong);
    yingzhao->addSkill(new BossXunyou);
    yingzhao->addRelateSkill("sipu");
    related_skills.insert("sipu", "#sipubf");

    General *xiangliu = new General(this, "xiangliu", "qun", 25, false, true);
    xiangliu->addSkill("yaoshou");
    xiangliu->addSkill(new Duqu);
    xiangliu->addSkill(new DuquBf);
    xiangliu->addSkill(new Jiushou);
    xiangliu->addSkill(new JiushouBf);
    xiangliu->addRelateSkill("echou");
    related_skills.insert("duqu", "#duqubf");
    related_skills.insert("jiushou", "#jiushoubf");

    General *zhuyan = new General(this, "zhuyan", "qun", 30, true, true);
	zhuyan->setStartHp(25);
    zhuyan->addSkill("yaoshou");
    zhuyan->addSkill(new Bingxian);
    zhuyan->addSkill(new Juyuan);
    zhuyan->addSkill(new JuyuanBf);
    zhuyan->addRelateSkill("boss_xushi");
    related_skills.insert("juyuan", "#juyuanbf");

    General *bifang = new General(this, "bifang", "qun", 25, false, true);
    bifang->addSkill("yaoshou");
    bifang->addSkill(new BossZhaohuo);
    bifang->addSkill(new ZhaohuoBf);
    bifang->addSkill(new Honglian);
    bifang->addRelateSkill("boss_yanyu");
    related_skills.insert("boss_zhaohuo", "#boss_zhaohuobf");

    General *zhangji = new General(this, "godlai_zhangji", "qun", 4, true, true);
    zhangji->addSkill(new Mojun);
    zhangji->addSkill(new Jielve);

    General *longxiang = new General(this, "godlai_longxiang", "qun", 4, true, true);
    longxiang->addSkill(new Longying);

    General *fanchou = new General(this, "godlai_fanchou", "qun", 4, true, true);
    fanchou->addSkill("mojun");
    fanchou->addSkill(new Fangong);

    General *huben = new General(this, "godlai_huben", "qun", 5, true, true);
    huben->addSkill(new Huying);

    General *niufudongxie = new General(this, "godlai_niufudongxie", "qun", 4, true, true);
	niufudongxie->setGender(General::Sexless);
    niufudongxie->addSkill("mojun");
    niufudongxie->addSkill(new BossTunjun);
    niufudongxie->addSkill(new Jiaoxia);
   
    General *fengyao = new General(this, "godlai_fengyao", "qun", 3, false, true);
    fengyao->addSkill(new BossFengying);

    General *dongyue = new General(this, "godlai_dongyue", "qun", 4, true, true);
    dongyue->addSkill("mojun");
    dongyue->addSkill(new Kuangxi);

    General *baolve = new General(this, "godlai_baolve", "qun", 3, true, true);
    baolve->addSkill(new Baoying);

    General *lijue = new General(this, "godlai_lijue", "qun", 5, true, true);
    lijue->addSkill("mojun");
    lijue->addSkill(new Yangwu);

    General *guosi = new General(this, "godlai_guosi", "qun", 4, true, true);
    guosi->addSkill("mojun");
    guosi->addSkill(new Yanglie);

    General *feixiong_left = new General(this, "godlai_feixiong_left", "qun", 4, true, true);
    feixiong_left->addSkill(new Jingqi);

    General *feixiong_right = new General(this, "godlai_feixiong_right", "qun", 4, true, true);
    feixiong_right->addSkill(new Ruiqi);

    addMetaObject<KuangxiCard>();
    skills << new Yinzei << new Yandu << new Luanchang << new Jicai
	<< new Moqu << new Sipu << new SipuBf << new Echou << new BossXushi << new BossYanyu;

    Card *slash = new GodSlash(Card::NoSuit, 0);
    slash->addCharTag("GodStructEffect");
    QList<Card *> cards;
    cards << new GodBlade(Card::Spade, 5)
	      << new GodPao(Card::Spade, 9)
	      << new GodQin(Card::Diamond, 1)
	      << new GodDiagram(Card::Spade, 2)
	      << new GodDiagram(Card::Club, 2)
	      << new GodHorse(Card::Spade, 5)
	      << new GodHalberd(Card::Diamond, 12)
	      << new GodSword(Card::Spade, 6)
	      << new GodHat(Card::Club, 1)
		  << new GodDoubleSword(Card::Spade, 2)
		  << new GodDeer(Card::Heart, 13)
		  << new GodBow(Card::Heart, 5)
		  << new GodAxe(Card::Diamond, 5)
		  << new GodEdict(Card::Spade, 13)
		  << new GodHeaddress(Card::Club, 12)
		  << new GodShip(Card::Heart, 10)
          << slash
          << new GodNihilo(Card::Heart, 7)
          << new GodNihilo(Card::Heart, 8)
          << new GodNihilo(Card::Heart, 9)
          << new GodNihilo(Card::Heart, 11)
          << new GodFlower(Card::Club, 12)
          << new GodFlower(Card::Club, 13)
          << new GodSpeel(Card::Diamond, 7)
          << new GodSpeel(Card::Club, 5);

    foreach (Card *card, cards)
        card->setParent(this);

    skills << new GodDiagramSkill << new GodBladeSkill << new GodHalberdSkill << new GodHalberdsSkill
		<< new GodHorseSkill << new GodQinSkill << new GodHatSkill << new GodHatBuff << new GodSwordSkill
		<< new GodPaoSkill << new GodDoubleSwordSkill << new GodDeerSkill << new GodBowSkill << new GodAxeSkill
		<< new GodAxeSkillBf << new GodEdictSkill << new GodHeaddressSkill << new GodHeaddressBf << new GodHalberdSkillBf;
    related_skills.insert("god_hat", "#god_hat");
    related_skills.insert("god_axe", "#god_axebf");
    related_skills.insert("god_headdress", "#god_headdressbf");
    related_skills.insert("god_halberd", "#god_halberdbf");
}

ADD_PACKAGE(Godlailailai);
