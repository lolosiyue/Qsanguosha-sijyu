#include "joy.h"
#include "engine.h"
//#include "standard-generals.h"
#include "standard-generals.h"
#include "clientplayer.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include <QScopeGuard>

Shit::Shit(Suit suit, int number)
    :BasicCard(suit, number)
{
    setObjectName("shit");

    target_fixed = true;
    damage_card = true;
    single_target = true;
}

QString Shit::getSubtype() const
{
    return "disgusting_card";
}

void Shit::onUse(Room *room, CardUseStruct &use) const
{
    if (use.to.isEmpty()) use.to << use.from;
    BasicCard::onUse(room, use);
}

bool Shit::isAvailable(const Player *player) const
{
    return player->hasFlag("CurrentPlayer")&&!player->isProhibited(player,this)&&BasicCard::isAvailable(player);
}

void Shit::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
	LogMessage log;
	log.from = effect.to;
	log.type = "#ShitDamage";
	log.card_str = toString();
	switch (getSuit()) {
	case Card::Spade:
		room->sendLog(log);
		room->damage(DamageStruct(this, effect.to, effect.to, 1, DamageStruct::Thunder));
		break;
	case Card::Club:
		room->sendLog(log);
		room->damage(DamageStruct(this, effect.to, effect.to));
		break;
	case Card::Heart:
		log.type = "#ShitLostHp";
		room->sendLog(log);
		room->loseHp(HpLostStruct(effect.to, 1, "shit", effect.to));
		break;
	case Card::Diamond:
		room->sendLog(log);
		room->damage(DamageStruct(this, effect.to, effect.to, 1, DamageStruct::Fire));
		break;
	default:
		break;
	}
}

class ShitEffect : public TriggerSkillV2
{
public:
    ShitEffect() : TriggerSkillV2("shit_effect")
	{
        events << CardsMoveOneTime;
        frequency = Compulsory;
        global = true;
    }



    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &contexts) const override
	{
		if (event != CardsMoveOneTime || !player || !player->isAlive()
            || !player->hasFlag("CurrentPlayer")) return true;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        bool hasShit = false;
        if (move.from == player && move.from_places.contains(Player::PlaceHand)
			&& (move.to_place == Player::PlaceTable || move.to_place == Player::DiscardPile)) {
            for (int i = 0; i < move.card_ids.length(); ++i) {
                if (move.from_places.at(i) == Player::PlaceHand
                    && Sanguosha->getCard(move.card_ids.at(i))->isKindOf("Shit")) {
                    hasShit = true;
                    break;
                }
            }
        }
        if (hasShit) {
            SkillContext context;
            context.skill_name = objectName();
            context.owner = context.invoker = context.initiator = player;
            context.original_data = &data;
            context.current_event = event;
            contexts << context;
        }
        return true;
    }

    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		if (!ctx.owner || ctx.invoker != ctx.owner || !ctx.original_data
            || ctx.current_event != CardsMoveOneTime) return false;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        bool hasShit = false;
        for (int id : move.card_ids) {
            if (Sanguosha->getCard(id)->isKindOf("Shit")) {
                hasShit = true;
                break;
            }
        }
        return hasShit && move.from == ctx.owner && ctx.owner->isAlive()
            && ctx.owner->hasFlag("CurrentPlayer")
            && move.from_places.contains(Player::PlaceHand)
			&& (move.to_place == Player::PlaceTable || move.to_place == Player::DiscardPile);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
        if (!ctx.original_data || !ctx.invoker) return false;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        ServerPlayer *player = ctx.invoker;
        if (move.from == player && move.from_places.contains(Player::PlaceHand)
			&& (move.to_place == Player::PlaceTable || move.to_place == Player::DiscardPile)) {
            for (int i = 0; i < move.card_ids.length(); i++) {
                if(move.from_places.at(i)!=Player::PlaceHand) continue;
				const Card*shit = Sanguosha->getCard(move.card_ids.at(i));
                if (shit->isKindOf("Shit")&&player->isAlive()){
					room->useCard(CardUseStruct(shit,player));/*
					LogMessage log;
					log.from = player;
					log.type = "#ShitDamage";
					log.card_str = shit->toString();
					switch (shit->getSuit()) {
					case Card::Spade:
						log.type = "#ShitLostHp";
						room->sendLog(log);
						room->loseHp(HpLostStruct(player, 1, "shit", player));
						break;
					case Card::Heart:
						room->sendLog(log);
						room->damage(DamageStruct(shit, player, player, 1, DamageStruct::Fire));
						break;
					case Card::Club:
						room->sendLog(log);
						room->damage(DamageStruct(shit, player, player, 1, DamageStruct::Thunder));
						break;
					case Card::Diamond:
						room->sendLog(log);
						room->damage(DamageStruct(shit, player, player));
						break;
					default:
						break;
					}*/
				}
			}
        }
        return false;
    }

    int getPriority(TriggerEvent) const { return 1; }
};

// -----------  Deluge -----------------

Deluge::Deluge(Card::Suit suit, int number)
    :Disaster(suit, number)
{
    setObjectName("deluge");

    judge.pattern = ".|.|1,13";
    judge.good = false;
    judge.reason = objectName();
}

void Deluge::takeEffect(ServerPlayer *target) const
{
	QList<const Card *> cards = target->getCards("he");

	int n = qMin(cards.length(), target->aliveCount());
	if (n < 1) return;

	qsanShuffle(cards);
	QList<int> card_ids;
	Room *room = target->getRoom();
	foreach (const Card *card, cards.mid(0, n))
		card_ids << card->getId();
	room->moveCardsAtomic(CardsMoveStruct(card_ids,nullptr,Player::PlaceTable,CardMoveReason()),true);

	QList<ServerPlayer *> players = room->getOtherPlayers(target);
	players << target;
	room->fillAG(card_ids);
	foreach (ServerPlayer *p, players) {
		if (p->isAlive()) {
			room->getThread()->delay();
			int id = room->askForAG(p, card_ids, false, "deluge");
			room->takeAG(p, id);
			card_ids.removeOne(id);
			if(card_ids.isEmpty()) break;
		}
	}
	room->clearAG();
	room->throwCard(card_ids,"deluge",nullptr);
}

// -----------  Typhoon -----------------

Typhoon::Typhoon(Card::Suit suit, int number)
    :Disaster(suit, number)
{
    setObjectName("typhoon");

    judge.pattern = ".|diamond|2~9";
    judge.good = false;
    judge.reason = objectName();
}

void Typhoon::takeEffect(ServerPlayer *target) const
{
    Room *room = target->getRoom();
    foreach (ServerPlayer *p, room->getAllPlayers()) {
        if (target->distanceTo(p) == 1) {
            int n = qMin(6, p->getHandcardNum());
            if (n > 0) room->askForDiscard(p, objectName(), n, n);
            room->getThread()->delay();
        }
    }
}

// -----------  Earthquake -----------------

Earthquake::Earthquake(Card::Suit suit, int number)
    :Disaster(suit, number)
{
    setObjectName("earthquake");

    judge.pattern = ".|club|2~9";
    judge.good = false;
    judge.reason = objectName();
}

void Earthquake::takeEffect(ServerPlayer *target) const
{
    Room *room = target->getRoom();
    foreach (ServerPlayer *p, room->getAllPlayers()) {
        if (2 - target->distanceTo(p, p->getOffensiveHorse() ? -1 : 0) <= 1) {// ignore plus 1 horse
            p->throwAllEquips(objectName());
            room->getThread()->delay();
        }
    }
}

// -----------  Volcano -----------------

Volcano::Volcano(Card::Suit suit, int number)
    :Disaster(suit, number)
{
    setObjectName("volcano");
    damage_card = true;

    judge.pattern = ".|heart|2~9";
    judge.good = false;
    judge.reason = objectName();
}

void Volcano::takeEffect(ServerPlayer *target) const
{
    Room *room = target->getRoom();

    DamageStruct damage;
    damage.card = this;
    damage.damage = 2;
    damage.to = target;
    damage.nature = DamageStruct::Fire;
    room->damage(damage);

    foreach (ServerPlayer *player, room->getOtherPlayers(target)) {
        bool plus1Horse = (player->getOffensiveHorse() != nullptr);
        if (target->distanceTo(player, plus1Horse ? -1 : 0) == 1) {// ignore plus 1 horse
            DamageStruct damage;
            damage.card = this;
            damage.damage = 1;
            damage.to = player;
            damage.nature = DamageStruct::Fire;
            room->damage(damage);
        }
    }
}

// -----------  MudSlide -----------------
MudSlide::MudSlide(Card::Suit suit, int number)
    :Disaster(suit, number)
{
    setObjectName("mudslide");
    damage_card = true;

    judge.pattern = ".|black|1,13,4,7";
    judge.good = false;
    judge.reason = objectName();
}

void MudSlide::takeEffect(ServerPlayer *target) const
{
    int to_destroy = 4;
    Room *room = target->getRoom();
    foreach (ServerPlayer *player, room->getAllPlayers()) {
        QList<const Card *> equips = player->getEquips();
		room->getThread()->delay();
        if (equips.isEmpty()) {
            DamageStruct damage;
            damage.card = this;
            damage.to = player;
            room->damage(damage);
        } else {
			foreach (const Card *e, equips) {
                CardMoveReason reason(CardMoveReason::S_REASON_DISCARD, player->objectName(), "mudslide", "");
                room->throwCard(e, reason, player);
				to_destroy--;
				if (to_destroy<=0) return;
				room->getThread()->delay();
            }
        }
    }
}

class GrabPeach : public EquipSkillV2
{
public:
    GrabPeach() : EquipSkillV2("grab_peach", "monkey")
    {
        events << CardUsed;
        global = true;
    }
    bool triggerable(const ServerPlayer *owner) const override
    {
        const Card *horse = owner ? owner->getOffensiveHorse() : nullptr;
        return owner && owner->isAlive() && horse && horse->isKindOf("Monkey")
            && !owner->isEquipsNullified(horse);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardUsed || !player || player->getPhase() != Player::Play) return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->getTypeId() == Card::TypeSkill) return false;
        const int count = room->countHistoryCards(player, "phase");
        // Temporary legacy projection for Yongjue consumers; the journal owns the fact.
        if (count >= 0) {
            room->setPlayerMark(player, "yongjue-PlayClear", count);
            if (count == 1 && use.card->isKindOf("Slash")) room->setCardFlag(use.card, "yongjueBf" + player->objectName());
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (event != CardUsed || !actor || !use.card || !use.card->isKindOf("Peach") || use.to.isEmpty()) return result;
        for (ServerPlayer *owner : room->getOtherPlayers(actor))
            if (triggerable(owner)) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !triggerable(ctx.owner)
            || ctx.original_data->value<CardUseStruct>().to.isEmpty()
            || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.extra_data = ctx.owner->getOffensiveHorse()->getEffectiveId();
        ctx.targets << ctx.owner;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (!ctx.owner || !triggerable(ctx.owner) || ctx.owner->getOffensiveHorse()->getEffectiveId() != id
            || !ctx.owner->canDiscard(ctx.owner, id)) return false;
        room->throwCard(id, objectName(), ctx.owner);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.original_data) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || use.to.isEmpty()) return false;
        use.to.clear();
        *ctx.original_data = QVariant::fromValue(use);
        target->obtainCard(use.card);
        return false;
    }
};
Monkey::Monkey(Card::Suit suit, int number)
    :OffensiveHorse(suit, number)
{
    setObjectName("monkey");
}

class GaleShellSkill : public ArmorSkillV2
{
public:
    GaleShellSkill() :ArmorSkillV2("gale_shell", "gale_shell")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        return data.value<DamageStruct>().nature == DamageStruct::Fire
            ? ArmorSkillV2::triggerable(event, room, player, data) : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        ctx.targets << ctx.original_data->value<DamageStruct>().to;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        if (!ctx.original_data) return false;
		QVariant &data = *ctx.original_data;
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.nature == DamageStruct::Fire) {
            LogMessage log;
            log.type = "#GaleShellDamage";
            log.from = player;
            log.arg = QString::number(damage.damage);
            damage.damage += getEffectiveAmount(ctx);
            log.arg2 = QString::number(damage.damage);
            room->sendLog(log);

            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

GaleShell::GaleShell(Suit suit, int number) :Armor(suit, number)
{
    setObjectName("gale_shell");

    target_fixed = false;
}

bool GaleShell::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->distanceTo(to_select) <= 1 && to_select->hasEquipArea(1);
}

/*
1.rende
2.jizhi
3.jieyin
4.guose
5.kurou
*/

class FiveLinesVS : public ViewAsSkillV2
{
public:
    FiveLinesVS() : ViewAsSkillV2("five_lines") {}
    bool isEquipSkill() const override { return true; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    static QString stateKey(const Player *player, const SkillInstanceRef &source, const QString &field)
    {
        const Card *armor = player ? player->getArmor() : nullptr;
        const QString identity = source.isValid()
            ? QString("source_%1_%2_%3").arg(source.ownerObjectName, source.key.skillName).arg(source.key.instanceID)
            : armor && armor->objectName() == "five_lines" ? QString("card_%1").arg(armor->getEffectiveId()) : QString("property");
        return "five_lines_" + identity + "_" + field + (field == "jieyin" ? "-PlayClear" : "-Clear");
    }
    QString receiptKey(const SkillContext &ctx, const QString &field) const
    {
        const QString saved = ctx.extra_data.toMap().value(field + "_key").toString();
        return saved.isEmpty() ? stateKey(ctx.initiator, ctx.sourceRef, field) : saved;
    }
    static QString mode(int hp) { return hp <= 1 ? "rende" : hp == 3 ? "jieyin" : hp == 4 ? "guose" : hp > 4 ? "kurou" : QString(); }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.initiator) return false;
        const int hp = ctx.initiator->getHp();
        if (hp == 3) return ctx.initiator->getMark(receiptKey(ctx, "jieyin")) == 0;
        if (hp <= 1 && ctx.initiator->getRoom()->getMode() == "04_1v3")
            return ctx.initiator->getMark(receiptKey(ctx, "given")) < 2;
        return hp != 2;
    }
    void addUsage(const SkillContext &ctx) const override
    {
        if (!ctx.initiator) return;
        if (ctx.choice == "jieyin") ctx.initiator->getRoom()->addPlayerMark(ctx.initiator, receiptKey(ctx, "jieyin"));
        else if (ctx.choice == "rende" && ctx.use_card)
            ctx.initiator->getRoom()->addPlayerMark(ctx.initiator, receiptKey(ctx, "given"), ctx.use_card->subcardsLength());
    }
    void commitAccepted(SkillContext &ctx) const
    {
        if (!ctx.initiator || ctx.extra_data.toMap().value("committed").toBool()) return;
        if (ctx.choice.isEmpty()) ctx.choice = ctx.use_card ? ctx.use_card->getTag("FiveLinesMode").toString() : QString();
        QVariantMap receipt = ctx.extra_data.toMap();
        if (receipt.isEmpty() && ctx.use_card) receipt = ctx.use_card->getTag("FiveLinesReceipt").toMap();
        ctx.extra_data = receipt;
        receipt["given_key"] = receiptKey(ctx, "given"); receipt["jieyin_key"] = receiptKey(ctx, "jieyin");
        receipt["given"] = ctx.initiator->getMark(receipt.value("given_key").toString()); receipt["committed"] = true;
        ctx.extra_data = receipt;
        addUsage(ctx);
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || !player->hasArmorEffect(objectName())) return false;
        if (const auto *server = qobject_cast<const ServerPlayer *>(player)) {
            SkillContext ctx; ctx.owner = ctx.invoker = ctx.initiator = const_cast<ServerPlayer *>(server);
            ctx.activationRef = request.activationRef;
            return prepareEquipSource(server->getRoom(), ctx) && checkCustomUsage(ctx);
        }
        const int hp = player->getHp();
        if (hp == 2) return false;
        const QList<SkillInstanceRef> sources = player->viewAsEquipSources(objectName());
        if (sources.size() > 1) return true;
        const SkillInstanceRef source = sources.value(0);
        return hp != 3 || player->getMark(stateKey(player, source, "jieyin")) == 0;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *player = request.initiator;
        if (!player || !card || card->hasFlag("using") || request.selectedCardIds.contains(card->getEffectiveId())) return false;
        const int hp = player->getHp();
        if (hp <= 1) return player->handCards().contains(card->getEffectiveId());
        if (hp == 3) return request.selectedCardIds.size() < 2 && player->handCards().contains(card->getEffectiveId()) && !player->isJilei(card);
        if (hp == 4) return request.selectedCardIds.isEmpty() && card->getSuit() == Card::Diamond
            && (player->handCards().contains(card->getEffectiveId()) || player->getEquips().contains(card))
            && card != player->getArmor();
        return false;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        const int hp = request.initiator->getHp();
        const int count = request.selectedCardIds.size();
        if (!(hp <= 1 ? count > 0 : hp == 3 ? count == 2 : hp == 4 ? count == 1 : hp > 4 && count == 0)) return false;
        ActiveSkillRequest prefix = request; prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (!canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        if (!request.initiator || !target || !selected.isEmpty() || target == request.initiator) return false;
        const int hp = request.initiator->getHp();
        return hp <= 1 || (hp == 3 && target->isMale() && target->isWounded());
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return request.initiator && (request.initiator->getHp() > 4 ? targets.isEmpty() : targets.size() == 1); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *card = nullptr;
        if (request.initiator->getHp() == 4) {
            const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
            auto *converted = new Indulgence(material->getSuit(), material->getNumber());
            converted->setSkillName(objectName()); converted->addSubcard(material); card = converted;
        } else card = ViewAsSkillV2::createCard(request);
        if (!card) return nullptr;
        SkillContext frozen; frozen.activationRef = request.activationRef;
        if (const auto *server = qobject_cast<const ServerPlayer *>(request.initiator)) { frozen.owner = frozen.invoker = frozen.initiator = const_cast<ServerPlayer *>(server); prepareEquipSource(server->getRoom(), frozen); }
        card->setTag("FiveLinesMode", mode(request.initiator->getHp()));
        card->setTag("FiveLinesReceipt", QVariantMap{{"given_key", stateKey(request.initiator, frozen.sourceRef, "given")}, {"jieyin_key", stateKey(request.initiator, frozen.sourceRef, "jieyin")}});
        return card;
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    {
        const int hp = request.initiator ? request.initiator->getHp() : 0;
        return hp <= 1 ? "NosRendeCard" : hp == 3 ? "JieyinCard" : hp == 4 ? "Indulgence" : "NosKurouCard";
    }
    bool willThrowSelectedCards() const override { return false; }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (!ctx.use_card) return false;
        ctx.choice = ctx.use_card->getTag("FiveLinesMode").toString();
        ctx.extra_data = ctx.use_card->getTag("FiveLinesReceipt");
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || !ctx.invoker || mode(ctx.initiator->getHp()) != ctx.choice || !cardSelectionFeasible(request) || !checkCustomUsage(ctx)) return false;
        if (!ctx.sourceRef.isValid() && receiptKey(ctx, "given") != stateKey(ctx.initiator, ctx.sourceRef, "given")) return false;
        QVariantMap receipt = ctx.extra_data.toMap();
        if (ctx.choice == "rende") {
            const int old = ctx.initiator->getMark(receiptKey(ctx, "given"));
            if (room->getMode() == "04_1v3" && old + request.selectedCardIds.size() > 2) return false;
            receipt["given"] = old; receipt["committed"] = true; ctx.extra_data = receipt;
            addUsage(ctx);
        } else if (ctx.choice == "jieyin") {
            addUsage(ctx); // Commit before discard notifications can re-enter the equipment skill.
            receipt["committed"] = true; ctx.extra_data = receipt;
            room->throwCard(request.selectedCardIds, objectName(), ctx.initiator);
        } else if (ctx.choice == "kurou") room->loseHp(HpLostStruct(ctx.initiator, 1, objectName(), ctx.initiator));
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.invoker->isAlive()) return FinishSkill;
        if (ctx.use_card && ctx.use_card->getTypeId() != Card::TypeSkill) return ContinueEffects;
        if (ctx.choice.isEmpty() && ctx.use_card) ctx.choice = ctx.use_card->getTag("FiveLinesMode").toString();
        ctx.manual_effect = true;
        if (ctx.choice == "kurou") skillEffect(ctx, ctx.invoker);
        else if (ctx.choice == "jieyin") {
            skillEffect(ctx, ctx.invoker);
            for (ServerPlayer *target : ctx.targets) skillEffect(ctx, target);
        } else if (ctx.choice == "rende") {
            for (ServerPlayer *target : ctx.targets) skillEffect(ctx, target);
            const int old = ctx.extra_data.toMap().value("given").toInt();
            if (old < 2 && old + ctx.use_card->subcardsLength() >= 2) {
                ctx.choice = "recover";
                skillEffect(ctx, ctx.invoker);
            }
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.invoker) return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice == "rende") {
            if (!ctx.initiator || !ctx.use_card) return ContinueEffects;
            for (int id : ctx.use_card->getSubcards()) if (!ctx.initiator->handCards().contains(id) || room->getCardOwner(id) != ctx.initiator || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
            room->obtainCard(target, ctx.use_card,
                CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.initiator->objectName(), target->objectName(), objectName(), ""), false);
        }
        else if (ctx.choice == "kurou") target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        else room->recover(target, RecoverStruct(objectName(), ctx.invoker, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }
};

class FiveLinesSkill : public ArmorSkillV2
{
public:
    FiveLinesSkill() : ArmorSkillV2("five_lines", "five_lines")
    { events << CardUsed << EventSkillInvoking; global = true; view_as_skill = new FiveLinesVS; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return false;
        SkillContext active = data.value<SkillContext>();
        if (!active.bypass_cost || active.activationRef.key.skillName != objectName() || !active.use_card
            || active.use_card->getTypeId() != Card::TypeSkill) return false;
        // Bypassed payment still spends an accepted active use, before effects can be cancelled.
        static_cast<const FiveLinesVS *>(view_as_skill)->commitAccepted(active);
        data = QVariant::fromValue(active);
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *owner, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (event != CardUsed || !owner || owner->getHp() != 2 || !use.card || !use.card->isNDTrick()) return {};
        return ArmorSkillV2::triggerable(event, room, owner, data);
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};
FiveLines::FiveLines(Card::Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("five_lines");
}

void FiveLines::onInstall(ServerPlayer *player) const
{
    Armor::onInstall(player);
}
DisasterPackage::DisasterPackage()
    :Package("Disaster")
{
    QList<Card *> cards;

    cards << new Deluge(Card::Spade, 1)
        << new Typhoon(Card::Spade, 4)
        << new Earthquake(Card::Club, 10)
        << new Volcano(Card::Heart, 13)
        << new MudSlide(Card::Heart, 7);

    foreach(Card *card, cards)
        card->setParent(this);

    type = CardPack;
}

JoyPackage::JoyPackage()
    :Package("joy")
{
    QList<Card *> cards;

    cards << new Shit(Card::Club, 1)
    << new Shit(Card::Heart, 8)
    << new Shit(Card::Diamond, 13)
    << new Shit(Card::Spade, 10);

    foreach(Card *card, cards)
    card->setParent(this);

    type = CardPack;
    skills << new ShitEffect;
}

class YxSwordSkill : public WeaponSkillV2
{
public:
    YxSwordSkill() : WeaponSkillV2("yx_sword", "yx_sword") { events << DamageCaused; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (event != DamageCaused || !damage.card || !damage.card->isKindOf("Slash")) return {};
        return WeaponSkillV2::triggerable(event, room, actor, data);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !ctx.original_data) return false;
        const Card *weapon = nullptr;
        for (const Card *equip : owner->getEquips())
            if (equip->objectName() == objectName()) { weapon = equip; break; }
        if (!weapon) return false; // This skill must transfer its physical weapon.
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getOtherPlayers(owner))
            if (owner->inMyAttackRange(other)) candidates << other;
        if (candidates.isEmpty()) return false;
        // The tag is only a nested-safe legacy AI projection; ctx owns the damage and selection.
        const QVariant previous = room->getTag("YxSwordData");
        room->setTag("YxSwordData", *ctx.original_data);
        const auto restore = qScopeGuard([&] {
            if (previous.isValid()) room->setTag("YxSwordData", previous);
            else room->removeTag("YxSwordData");
        });
        ServerPlayer *target = room->askForPlayerChosen(owner, candidates, objectName(), "@yxsword-select", true, true);
        if (!target) return false;
        ctx.extra_data = weapon->getEffectiveId();
        ctx.targets << target;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (!ctx.owner || ctx.targets.isEmpty() || !ctx.targets.first()->isAlive()
            || room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceEquip
            || Sanguosha->getCard(id)->objectName() != objectName()) return false;
        room->moveCardTo(Sanguosha->getCard(id), ctx.owner, ctx.targets.first(), Player::PlaceHand,
            CardMoveReason(CardMoveReason::S_REASON_TRANSFER, ctx.owner->objectName(), objectName(), ""));
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.original_data) return false;
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        damage.from = target;
        *ctx.original_data = QVariant::fromValue(damage);
        return damage.to && damage.to->isDead();
    }
};
YxSword::YxSword(Suit suit, int number)
    :Weapon(suit, number, 3)
{
    setObjectName("yx_sword");
}

JoyEquipPackage::JoyEquipPackage()
    : Package("JoyEquip")
{
    (new Monkey(Card::Diamond, 5))->setParent(this);
    (new GaleShell(Card::Heart, 1))->setParent(this);
    (new YxSword(Card::Club, 9))->setParent(this);
    (new FiveLines(Card::Heart, 5))->setParent(this);

    type = CardPack;
    skills << new GaleShellSkill << new YxSwordSkill << new GrabPeach << new FiveLinesSkill;
}

ADD_PACKAGE(Joy)
ADD_PACKAGE(Disaster)
ADD_PACKAGE(JoyEquip)
