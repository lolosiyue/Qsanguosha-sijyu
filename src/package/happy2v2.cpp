#include "happy2v2.h"
//#include "settings.h"
#include "skill.h"
//#include "standard.h"
//#include "client.h"
//#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"

KuijiCard::KuijiCard()
{
    // Keep the serialized legacy card name for SmartAI/card-string fallback;
    // instance-aware submissions are rebuilt through KuijiVS below.
    setSkillName("kuiji");
    mute = true;
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
}

void KuijiCard::onUse(Room *room, CardUseStruct &card_use) const
{
    // Preserve the legacy card-string path for callers without a V2 instance.
    LogMessage log;
    log.type = "#InvokeSkill";
    log.from = card_use.from;
    log.arg = "kuiji";
    room->sendLog(log);
    room->broadcastSkillInvoke("kuiji");
    room->notifySkillInvoked(card_use.from, "kuiji");

    int id = subcards.first();
    const Card *card = Sanguosha->getCard(id);
    log.type = "$ShanzhuanViewAsPut";
    log.from = card_use.from;
    log.to << card_use.from;
    log.card_str = card->toString();

    SupplyShortage *supply_shortage = new SupplyShortage(card->getSuit(), card->getNumber());
    supply_shortage->setSkillName("kuiji");
    WrappedCard *c = Sanguosha->getWrappedCard(card->getId());
    c->takeOver(supply_shortage);
    room->broadcastUpdateCard(room->getAllPlayers(true), id, supply_shortage);

    log.arg = card->objectName();
    room->sendLog(log);

    CardMoveReason reason(CardMoveReason::S_REASON_PUT, card_use.from->objectName(), card_use.from->objectName(), "kuiji", "");
    room->moveCardTo(card, card_use.from, card_use.from, Player::PlaceDelayedTrick, reason, true);

    if (card_use.from->isDead()) return;
    card_use.from->drawCards(1, "kuiji");
    if (card_use.from->isDead()) return;

    QList<ServerPlayer *> enemies;
    foreach (ServerPlayer *p, room->getOtherPlayers(card_use.from)) {
        if (!card_use.from->isYourFriend(p))
            enemies << p;
    }
    if (enemies.isEmpty()) return;

    int hp = enemies.first()->getHp();
    foreach (ServerPlayer *p, enemies) {
        if (p->getHp() > hp)
            hp = p->getHp();
    }

    QList<ServerPlayer *> _enemies;
    foreach (ServerPlayer *p, enemies) {
        if (p->getHp() >= hp)
            _enemies << p;
    }
    if (_enemies.isEmpty()) return;

    ServerPlayer *enemy = room->askForPlayerChosen(card_use.from, _enemies, "kuiji", "@kuiji", true);
    if (!enemy) return;
    room->doAnimate(1, card_use.from->objectName(), enemy->objectName());
    room->damage(DamageStruct("kuiji", card_use.from, enemy, 2));
}

class KuijiVS : public ViewAsSkillV2
{
public:
    KuijiVS() : ViewAsSkillV2("kuiji", 1)
    {
        setPhaseName("Play");
    }

    // The activation instance owns its quota; the legacy card history remains
    // available to AI without coupling two instances of the same skill.
    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        return player && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && player->hasJudgeArea() && !player->containsTrick("supply_shortage");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || !request.selectedCardIds.isEmpty()
            || card->getEffectiveId() < 0 || card->hasFlag("using") || !card->isKindOf("BasicCard") || !card->isBlack())
            return false;
        const int id = card->getEffectiveId();
        return request.initiator->handCards().contains(id) || request.initiator->getEquipsId().contains(id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        if (request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }

    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "KuijiCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        ServerPlayer *source = ctx.initiator;
        if (!room || !source || !source->isAlive() || request.selectedCardIds.size() != 1)
            return false;
        if (!source->hasJudgeArea() || source->containsTrick("supply_shortage")
            || request.selectedCardIds.first() < 0)
            return false;
        const int id = request.selectedCardIds.first();
        const Card *card = Sanguosha->getCard(id);
        const Player::Place place = room->getCardPlace(id);
        if (!card || !card->isKindOf("BasicCard") || !card->isBlack()
            || room->getCardOwner(id) != source
            || (place != Player::PlaceHand && place != Player::PlaceEquip))
            return false;

        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = source;
        log.arg = objectName();
        room->sendLog(log);
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(source, objectName());

        log.type = "$ShanzhuanViewAsPut";
        log.to << source;
        log.card_str = card->toString();
        SupplyShortage *supply_shortage = new SupplyShortage(card->getSuit(), card->getNumber());
        supply_shortage->setSkillName(objectName());
        WrappedCard *wrapped = Sanguosha->getWrappedCard(card->getId());
        wrapped->takeOver(supply_shortage);
        room->broadcastUpdateCard(room->getAllPlayers(true), id, supply_shortage);
        log.arg = card->objectName();
        room->sendLog(log);

        CardMoveReason reason(CardMoveReason::S_REASON_PUT, source->objectName(),
                              source->objectName(), objectName(), "");
        room->moveCardTo(card, source, source, Player::PlaceDelayedTrick, reason, true);
        return room->getCardOwner(id) == source && room->getCardPlace(id) == Player::PlaceDelayedTrick;
    }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker ? ctx.invoker : ctx.initiator;
        if (!source || !source->isAlive()) return FinishSkill;
        Room *room = source->getRoom();
        ctx.manual_effect = true;
        ctx.choice = "draw";
        skillEffect(ctx, source);
        ctx.choice.clear();
        if (!source->isAlive()) return FinishSkill;

        QList<ServerPlayer *> enemies;
        foreach (ServerPlayer *player, room->getOtherPlayers(source))
            if (!source->isYourFriend(player)) enemies << player;
        if (enemies.isEmpty()) return ContinueEffects;

        int hp = enemies.first()->getHp();
        foreach (ServerPlayer *player, enemies) hp = qMax(hp, player->getHp());
        QList<ServerPlayer *> candidates;
        foreach (ServerPlayer *player, enemies)
            if (player->getHp() >= hp) candidates << player;
        if (candidates.isEmpty()) return ContinueEffects;

        ServerPlayer *enemy = room->askForPlayerChosen(source, candidates, objectName(), "@kuiji", true);
        if (!enemy) return ContinueEffects;
        ctx.targets.clear();
        ctx.targets << enemy;
        ctx.manual_effect = true;
        skillEffect(ctx, enemy);
        return ContinueEffects;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker ? ctx.invoker : ctx.initiator;
        if (!source || !source->isAlive() || !target || !target->isAlive()) return ContinueEffects;
        Room *room = source->getRoom();
        if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return ContinueEffects; }
        room->doAnimate(1, source->objectName(), target->objectName());
        if (!ctx.use_card) return ContinueEffects;
        const SkillInstanceRef ref = getUsageRef(ctx);
        ctx.use_card->setTag("KuijiSource", QVariantMap{{"owner", ref.ownerObjectName}, {"skill", ref.key.skillName},
            {"instance", ref.key.instanceID}, {"actor", source->objectName()}, {"amount", getEffectiveAmount(ctx)},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_instance", ctx.sourceRef.key.instanceID}});
        DamageStruct damage(ctx.use_card, source, target, getEffectiveAmount(ctx) * 2);
        damage.reason = objectName();
        room->damage(damage);
        return ContinueEffects;
    }
};

class Kuiji : public TriggerSkillV2
{
public:
    Kuiji() : TriggerSkillV2("kuiji")
    {
        events << Dying;
        global = true;
        view_as_skill = new KuijiVS;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const DyingStruct dying = data.value<DyingStruct>();
        if (!dying.who || !dying.damage || dying.damage->getReason() != objectName()
            || dying.damage->chain || dying.damage->transfer || !dying.damage->card) return true;
        const QVariantMap receipt = dying.damage->card->getTag("KuijiSource").toMap();
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        ServerPlayer *actor = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
        if (!owner || !actor || !actor->isAlive() || actor->isYourFriend(dying.who)) return true;
        SkillContext ctx;
        // The damage actor determines allies; provenance still names the exact activation source.
        ctx.skill_name = objectName(); ctx.owner = owner; ctx.initiator = actor; ctx.invoker = dying.who;
        ctx.instanceID = receipt.value("instance").toInt();
        ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
            SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
        ctx.amount = receipt.value("amount").toInt(); ctx.extra_data = receipt;
        ctx.original_data = &data; ctx.current_event = event;
        if (ctx.sourceRef.isValid()) contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const DyingStruct dying = ctx.original_data->value<DyingStruct>();
        // This continuation belongs to the exact damage-producing use, not every live Kuiji copy.
        return dying.damage && dying.damage->card && dying.damage->card->getTag("KuijiSource").toMap() == ctx.extra_data.toMap();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.initiator;
        if (!room || !source || !ctx.original_data) return false;
        const DyingStruct dying = ctx.original_data->value<DyingStruct>();
        if (!dying.who || source->isYourFriend(dying.who)) return false;

        QList<ServerPlayer *> friends;
        foreach (ServerPlayer *other, room->getAllPlayers())
            if (source->isYourFriend(other)) friends << other;
        if (friends.isEmpty()) return false;
        int hp = friends.first()->getHp();
        foreach (ServerPlayer *other, friends) hp = qMin(hp, other->getHp());
        QList<ServerPlayer *> candidates;
        foreach (ServerPlayer *other, friends)
            if (other->getHp() <= hp && other->getLostHp() > 0) candidates << other;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(source, candidates, objectName(), "@kuiji_recover");
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.initiator;
        if (!room || !source || !source->isAlive() || !target || !target->isAlive()) return false;
        room->doAnimate(1, source->objectName(), target->objectName());
        room->recover(target, RecoverStruct(objectName(), source, getEffectiveAmount(ctx)));
        return false;
    }
};

class HappyCuorui : public TriggerSkillV2
{
public:
    HappyCuorui() : TriggerSkillV2("happycuorui")
    {
        m_baseAmount = 2;
        events << EventPhaseStart;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Play && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.owner ? ctx.owner : ctx.invoker;
        if (!room || !source || !source->isAlive()) return false;
        QList<ServerPlayer *> friends;
        foreach (ServerPlayer *other, room->getAllPlayers())
            if (source->isYourFriend(other) && source->canDiscard(other, "hej")) friends << other;
        if (friends.isEmpty()) return false;
        ServerPlayer *friend_player = room->askForPlayerChosen(source, friends, objectName(),
                                                               "@happycuorui", true, true);
        if (!friend_player || !source->canDiscard(friend_player, "hej")) return false;
        const int id = room->askForCardChosen(source, friend_player, "hej", objectName(),
                                              false, Card::MethodDiscard);
        if (id < 0) return false;

        QVariantMap state;
        state.insert("friend", friend_player->objectName());
        state.insert("card_id", id);
        state.insert("color", cardColor(Sanguosha->getCard(id)));
        ctx.extra_data = state;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.owner ? ctx.owner : ctx.invoker;
        if (!room || !source || !source->isAlive()) return false;
        QVariantMap state = ctx.extra_data.toMap();
        ServerPlayer *friend_player = room->findPlayerByObjectName(state.value("friend").toString(), true);
        const int id = state.value("card_id").toInt();
        const Player::Place place = room->getCardPlace(id);
        if (!friend_player || id < 0 || room->getCardOwner(id) != friend_player
            || (place != Player::PlaceHand && place != Player::PlaceEquip && place != Player::PlaceDelayedTrick)
            || !source->canDiscard(friend_player, id)) return false;
        // Interceptors may change a filtered card after selection; snapshot the paid face before moving it.
        state.insert("color", cardColor(Sanguosha->getCard(id)));
        ctx.extra_data = state;
        room->throwCard(id, friend_player, source);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker ? ctx.invoker : ctx.owner;
        if (!room || !source || !source->isAlive()) return false;
        const QVariantMap state = ctx.extra_data.toMap();
        const int id = state.value("card_id").toInt();
        const QString color = state.value("color").toString();
        if (color.isEmpty()) return false;

        QList<ServerPlayer *> discard_targets = getDiscardTargets(room, source, color);
        QStringList choices;
        int hand = 0;
        foreach (ServerPlayer *player, room->getOtherPlayers(source)) {
            if (source->isYourFriend(player)) continue;
            hand += player->getHandcardNum();
        }
        if (!discard_targets.isEmpty()) choices << "discard";
        if (hand > 0) choices << "show";
        if (choices.isEmpty()) return false;

        const QString choice = room->askForChoice(source, objectName(), choices.join("+"), id);
        if (!choices.contains(choice)) return false;
        ctx.manual_effect = true;
        ctx.targets.clear();
        QVariantMap mutable_state = state;
        mutable_state.insert("branch", choice);
        mutable_state.insert("color", color);
        ctx.extra_data = mutable_state;

        if (choice == "discard") {
            const int amount = getEffectiveAmount(ctx);
            for (int i = 0; i < amount && source->isAlive(); ++i) {
                discard_targets = getDiscardTargets(room, source, color);
                if (discard_targets.isEmpty()) break;
                const bool optional = i > 0;
                const QString reason = optional ? "happycuorui2" : objectName();
                const QString prompt = QString("@happycuorui_discard%1:%2")
                    .arg(optional ? "2" : "").arg(color);
                ServerPlayer *target = room->askForPlayerChosen(source, discard_targets, reason, prompt, optional);
                if (!target) break;
                mutable_state.insert("optional", optional);
                mutable_state.insert("reason", reason);
                mutable_state.insert("prompt", prompt);
                ctx.extra_data = mutable_state;
                ctx.targets = {target};
                skillEffect(EventPhaseStart, room, source, ctx, target);
            }
        } else {
            QVariantList shown;
            const int amount = getEffectiveAmount(ctx);
            for (int i = 0; i < amount && source->isAlive(); ++i) {
                QList<ServerPlayer *> targets;
                foreach (ServerPlayer *player, room->getOtherPlayers(source)) {
                    if (source->isYourFriend(player) || player->isKongcheng()) continue;
                    bool unshown = false;
                    for (int handId : player->handCards()) if (!shown.contains(handId)) { unshown = true; break; }
                    if (unshown) targets << player;
                }
                if (targets.isEmpty()) break;
                ServerPlayer *target = room->askForPlayerChosen(source, targets, "happycuorui3", "@happycuorui_show");
                if (!target) break;
                mutable_state.insert("shown_ids", shown);
                ctx.extra_data = mutable_state;
                ctx.targets = {target};
                skillEffect(EventPhaseStart, room, source, ctx, target);
                mutable_state = ctx.extra_data.toMap();
                shown = mutable_state.value("shown_ids").toList();
            }

            mutable_state.insert("shown_ids", shown);
            mutable_state.insert("branch", "obtain");
            ctx.extra_data = mutable_state;
            skillEffect(EventPhaseStart, room, source, ctx, source);
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker ? ctx.invoker : ctx.owner;
        if (!room || !source || !source->isAlive() || !target || !target->isAlive()) return false;
        QVariantMap state = ctx.extra_data.toMap();
        const QString color = state.value("color").toString();
        if (color.isEmpty()) return false;
        if (state.value("branch").toString() == "obtain") {
            DummyCard cards;
            const QVariantMap owners = state.value("shown_owners").toMap();
            for (const QVariant &value : state.value("shown_ids").toList()) {
                const int id = value.toInt();
                ServerPlayer *holder = room->getCardOwner(id);
                // Revealing a card does not claim it if a nested effect has already moved it.
                if (holder && holder->objectName() == owners.value(QString::number(id)).toString()
                    && room->getCardPlace(id) == Player::PlaceHand && cardColor(Sanguosha->getCard(id)) == color
                    && source->canGet(holder, id)) cards.addSubcard(id);
            }
            if (cards.subcardsLength() > 0) room->obtainCard(target, &cards, true);
            return false;
        }
        room->doAnimate(1, source->objectName(), target->objectName());
        if (state.value("branch").toString() == "discard") {
            if (!source->canDiscard(target, "e")) return false;
            QList<int> disabled_ids;
            foreach (const Card *card, target->getCards("e"))
                if (cardColor(card) != color || !source->canDiscard(target, card->getEffectiveId()))
                    disabled_ids << card->getEffectiveId();
            const int id = room->askForCardChosen(source, target, "e", objectName(),
                                                  false, Card::MethodDiscard, disabled_ids);
            if (id >= 0 && !disabled_ids.contains(id) && room->getCardOwner(id) == target
                && room->getCardPlace(id) == Player::PlaceEquip && source->canDiscard(target, id)
                && cardColor(Sanguosha->getCard(id)) == color) room->throwCard(id, target, source);
        } else {
            QVariantList shown = state.value("shown_ids").toList();
            QList<int> hand;
            foreach (int id, target->handCards())
                if (!shown.contains(id)) hand << id;
            if (hand.isEmpty()) return false;
            const int id = hand.at(qsanRandomBounded(hand.length()));
            shown << id;
            state.insert("shown_ids", shown);
            QVariantMap owners = state.value("shown_owners").toMap();
            owners.insert(QString::number(id), target->objectName());
            state.insert("shown_owners", owners);
            ctx.extra_data = state;
            room->showCard(target, id);
        }
        return false;
    }

private:
    static QString cardColor(const Card *card)
    {
        if (card->isRed()) return "red";
        if (card->isBlack()) return "black";
        return "no_suit";
    }

    static QList<ServerPlayer *> getDiscardTargets(Room *room, ServerPlayer *source, const QString &color)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *player, room->getOtherPlayers(source)) {
            if (source->isYourFriend(player)) continue;
            foreach (const Card *card, player->getCards("e")) {
                if (cardColor(card) == color && source->canDiscard(player, card->getEffectiveId())) {
                    result << player;
                    break;
                }
            }
        }
        return result;
    }
};

Happy2v2Package::Happy2v2Package()
    : Package("Happy2v2")
{
    General *leitong = new General(this, "leitong", "shu", 4);
    leitong->addSkill(new Kuiji);

    General *wulan = new General(this, "wulan", "shu", 4);
    wulan->addSkill(new HappyCuorui);

    addMetaObject<KuijiCard>();
}

ADD_PACKAGE(Happy2v2)
