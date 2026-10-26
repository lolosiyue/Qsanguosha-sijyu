#include "card-use-service.h"

#include "card-lifetime-manager.h"
#include "card-movement-service.h"
#include "controller-input-diagnostics.h"
#include "engine.h"
#include "protocol.h"
#include "protocol/card-provenance-message.h"
#include "resolution-history.h"
#include "room.h"
#include "room-runtime.h"
#include "room-state.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "settings.h"
#include "skill.h"
#include "skill-execution-registry.h"
#include "standard.h"
#include "trigger-cascade-break.h"
#include "wrapped-card.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSet>

using namespace QSanProtocol;

CardUseService::CardUseService(Room &room)
    : m_room(room)
{
}

namespace {

QList<const Player *> selectedPlayers(const CardUseStruct &use)
{
    QList<const Player *> result;
    for (ServerPlayer *target : use.to)
        if (target) result << target;
    return result;
}

}

int getConvertedPhysicalCardId(const Card *card)
{
	if (card == nullptr || card->isVirtualCard() || !card->isModified())
		return -1;

	const int cardId = card->getEffectiveId();
	if (cardId < 0 || cardId >= Sanguosha->getCardCount())
		return -1;

	const Card *originalCard = Sanguosha->getEngineCard(cardId);
	if (originalCard == nullptr)
		return -1;
	if (originalCard->objectName() == card->objectName()
		&& !card->getSkillName(false).startsWith("_"))
		return -1;

	return cardId;
}

static bool hasGenericActiveSkillUsage(const ViewAsSkillV2 *skill)
{
	return skill && skill->getLimitScope() != Skill::Limit_None
		&& skill->getLimitScope() != Skill::Limit_Custom;
}

static bool hasViewAsSkillEffect(const Player *player, const QString &skillName)
{
	return player && player->getMark("ViewAsSkill_" + skillName + "Effect") > 0;
}

void CardUseService::slashEffect(const SlashEffectStruct&effect)
{
	QVariant data = QVariant::fromValue(effect);
	if (m_room.thread->trigger(SlashEffected, &m_room, effect.to, data)){
		if (effect.to->hasFlag("Global_NonSkillNullify"))
			effect.to->setFlags("-Global_NonSkillNullify");
		else
			m_room.setEmotion(effect.to, "skill_nullify");
		if (effect.slash)
			effect.to->removeQinggangTag(effect.slash);
	}
}

void CardUseService::slashResult(const SlashEffectStruct&effect, const Card*jink)
{
	SlashEffectStruct result_effect = effect;
	result_effect.jink = jink;
	QVariant data = QVariant::fromValue(result_effect);

	if (effect.slash&&effect.from&&effect.to)
		m_room.setCardFlag(effect.slash, QString("-NonSkillNullify_%1").arg(effect.to->objectName()));

	if (jink){
		if (effect.slash){
			effect.to->removeQinggangTag(effect.slash);
			if (effect.from&&effect.to)
				m_room.setCardFlag(effect.slash, QString("NonSkillNullify_%1").arg(effect.to->objectName()));
		}
		m_room.thread->trigger(SlashMissed, &m_room, effect.from, data);
	} else {
		if (effect.to->isAlive())
			m_room.thread->trigger(SlashHit, &m_room, effect.from, data);
	}
}

bool CardUseService::useNullified(const Card*use_card)
{
	return m_room.tag["UseHistory"+use_card->toString()].value<CardUseStruct>().nullified_list.contains("_ALL_TARGETS");
}

const Card*CardUseService::isCanceled(const CardEffectStruct&effect)
{
	if (effect.offset_num<1||effect.no_offset||effect.no_respond) return nullptr;
	if (effect.card->isKindOf("TrickCard")&&effect.card->isCancelable(effect)){
        // Card effects define pending offsets; the room does not decide their target scope.
        if (effect.to) {
            const QVariantMap pending = m_room.getTag(effect.card->toString() + "PendingNullification").toMap();
            const Card *offset = pending.value("card").value<const Card *>();
            if (offset && pending.value("targets").toStringList().contains(effect.to->objectName())) {
                LogMessage log;
                log.type = pending.value("log_type").toString();
                log.from = effect.from;
                log.to << effect.to;
                log.arg = effect.card->objectName();
                if (!log.type.isEmpty()) m_room.sendLog(log);
                return offset;
            }
        }
		effect.to->setTag("TrickEffectData", QVariant::fromValue(effect));
		return m_room.askForNullification(effect.card, effect.from, effect.to, true);
    } else if (effect.card->isKindOf("Slash")) {
		// Every standard Slash, including HEG deck instances, uses this Jink path.
		m_room.setTag("SlashData", QVariant::fromValue(effect));
		if (effect.offset_num==1){
			const Card*jink = m_room.askForUseCard(effect.to,"jink","slash-jink:"+effect.from->objectName(),-1,Card::MethodUse,true,effect.from,effect.card);
			if (jink&&!useNullified(jink))
				return jink;
		} else {
			//Card*jinks = Sanguosha->cloneCard("jink");
			//jinks->deleteLater();
			for (int i=effect.offset_num;i>0;i--){
				QString prompt = QString("@multi-jink%1:%2::%3").arg(i==effect.offset_num?"-start":"").arg(effect.from->objectName()).arg(i);
				const Card*jink = m_room.askForUseCard(effect.to,"jink",prompt,-1,Card::MethodUse,true,effect.from,effect.card);
				if (jink&&!useNullified(jink)){
					//jinks->addSubcard(jink);
					if (i==1) return jink;
				}else break;
			}
		}
	}
	return nullptr;
}

CardUseStruct CardUseService::getUseStruct(const Card*card)
{
	return m_room.tag["UseHistory"+card->toString()].value<CardUseStruct>();
}

void CardUseService::prepareTargetModSkillReveal(CardUseStruct &use) const
{
    use.targetModReveal = TargetModRevealState();
    // Authoritative/forced uses deliberately bypass the selection contract.
    if (!Config.EnableHegemony || !use.from || !use.m_validateTargets) return;
    use.targetModReveal.owner = use.from->objectName();
    // Include attached leaves on other holders, but admit only this user's
    // own concealed roots. Selector/pattern checks still run in the engine.
    for (const ServerPlayer *holder : m_room.getPlayers()) {
        for (const SkillInstance &instance : holder->getSkillInstances()) {
            if (!dynamic_cast<const TargetModSkillV2 *>(Sanguosha->getSkill(instance.skillName))
                || holder->isSkillInstanceEffectAvailable(instance.skillName, instance.instanceID)
                || !holder->isSkillInstanceEffectAvailable(instance.skillName, instance.instanceID, use.from))
                continue;
            const SkillInstanceRef ref(holder->objectName(), instance.key());
            const SkillInstanceRef root = m_room.resolveSkillInstanceRootRef(ref);
            if (!root.isValid() || root.ownerObjectName != use.from->objectName()) continue;
            use.targetModReveal.sources << ref;
            use.targetModReveal.roots << root;
        }
    }
}

void CardUseService::planTargetModSkillReveal(CardUseStruct &use) const
{
    TargetModRevealState &snapshot = use.targetModReveal;
    snapshot.options.clear();
    if (!use.from || !use.card || snapshot.sources.isEmpty()
        || snapshot.owner != use.from->objectName()) return;
    for (int mask = 0; mask < 4; ++mask) {
        QList<SkillInstanceRef> allowed;
        for (int i = 0; i < snapshot.sources.size(); ++i) {
            const SkillInstanceRef &ref = snapshot.sources.at(i);
            const SkillInstanceRef &root = snapshot.roots.at(i);
            if (m_room.resolveSkillInstanceRootRef(ref) != root || !m_room.canShowGeneralForSkill(ref)) continue;
            const SkillInstance *source = use.from->findSkillInstance(root.key.skillName, root.key.instanceID);
            if (source && source->source == SourceInnate && (source->bindHead & mask)) allowed << ref;
        }
        // Preserve the original selection order and pre-use history. onUse may
        // sort targets, turn Collateral pairs into killers, or clear Slash flags.
        TargetModSkillQueryScope scope(use.from, allowed, snapshot.historyKey, snapshot.useHistoryEventId);
        if (areCardTargetsLegal(use)
            && (m_room.m_runtime->state().getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_PLAY
                || use.card->isAvailable(use.from))) {
            snapshot.options << scope.contributors();
            if (mask == 0) break;
        }
    }
}

bool CardUseService::showRequiredTargetModSkillsV2(const CardUseStruct &use)
{
    if (!Config.EnableHegemony || !use.from || !use.card || use.skipSkillEffect
        || use.targetModReveal.sources.isEmpty()) return true;
    const TargetModRevealState &snapshot = use.targetModReveal;
    if (snapshot.owner != use.from->objectName()
        || snapshot.sources.size() != snapshot.roots.size()) return false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!use.from->isAlive()) return false;
        QList<QList<SkillInstanceRef>> best;
        int minimum = 3;
        for (const QList<SkillInstanceRef> &option : snapshot.options) {
            QList<SkillInstanceRef> toShow;
            QSet<int> requiredSlots;
            bool valid = true;
            for (const SkillInstanceRef &ref : option) {
                const int index = snapshot.sources.indexOf(ref);
                const ServerPlayer *holder = m_room.findPlayerByObjectName(ref.ownerObjectName, true);
                if (index < 0 || m_room.resolveSkillInstanceRootRef(ref) != snapshot.roots.at(index)
                    || !holder || !holder->isAlive()
                    || !holder->isSkillInstanceEffectAvailable(ref.key.skillName, ref.key.instanceID, use.from)
                    || !m_room.canShowGeneralForSkill(ref)) { valid = false; break; }
                const SkillInstanceRef &root = snapshot.roots.at(index);
                const SkillInstance *source = use.from->findSkillInstance(root.key.skillName, root.key.instanceID);
                if (!source || source->source != SourceInnate) { valid = false; break; }
                const bool shown = source->bindHead == 1 ? use.from->hasShownGeneral()
                    : source->bindHead == 2 && use.from->hasShownGeneral2();
                if (!shown && !requiredSlots.contains(source->bindHead)) {
                    requiredSlots << source->bindHead;
                    toShow << root;
                }
            }
            if (!valid) continue;
            if (toShow.isEmpty()) return true;
            if (toShow.size() < minimum) { minimum = toShow.size(); best.clear(); }
            if (toShow.size() == minimum) best << toShow;
        }
        if (best.isEmpty()) return false;
        SkillInstanceRef chosen = best.first().first();
        if (minimum == 1 && best.size() > 1) {
            QMap<QString, SkillInstanceRef> choices;
            for (const QList<SkillInstanceRef> &option : best) {
                const SkillInstanceRef &root = option.first();
                const SkillInstance *source = use.from->findSkillInstance(root.key.skillName, root.key.instanceID);
                choices.insert(source->bindHead == 1 ? "head" : "deputy", root);
            }
            if (choices.size() > 1) {
                const QString choice = m_room.askForChoice(use.from, "HegemonyReveal", choices.keys().join("+"),
                                                     QVariant::fromValue(use));
                if (!choices.contains(choice)) return false;
                chosen = choices.value(choice);
            }
        }
        // Recheck all exact contributors after each reveal; a newly acquired
        // same-name source cannot rescue a paid use whose original source died.
        if (!m_room.showGeneralForSkill(chosen)) return false;
    }
    return false;
}

bool CardUseService::areCardTargetsLegal(const CardUseStruct &use) const
{
	if (!use.card || !use.from) return false;
	if (!use.activationRef.key.skillName.isEmpty()) {
		const ViewAsSkillV2 *activeSkill = dynamic_cast<const ViewAsSkillV2 *>(
			Sanguosha->getViewAsSkill(use.activationRef.key.skillName));
		// resolveActiveSkillRequest() has already checked V2's independent target
		// contract; its proxy card intentionally rejects generic targetFilter().
		if (activeSkill && qobject_cast<const ActiveSkillCard *>(use.card->getRealCard())) return true;
	}

	QList<const Player *> selected;
	if (use.card->targetFixedFor(use.from))
		return use.to.isEmpty() && use.card->targetsFeasible(selected, use.from);

	foreach (ServerPlayer *target, use.to) {
		if (!target) return false;
		int maxVotes = 0;
		// Match client selection: Collateral reports capacity through maxVotes
		// even when targetFilter returns false. Count repeated target votes too.
		use.card->targetFilterFor(selected, target, use.from, maxVotes);
		if (maxVotes <= selected.count(target))
			return false;
		selected << target;
	}
	return use.card->targetsFeasibleFor(selected, use.from);
}

const Card *CardUseService::resolveActiveSkillRequest(ServerPlayer *player, const ViewAsSkillV2 *skill,
                                             const ActiveSkillRequest &request) const
{
	if (!player || !skill || request.initiator != player)
		return nullptr;
	const bool equipmentActivation = skill->isEquipSkill()
		&& request.activationRef.key.instanceID == 0;
	SkillContext equipmentSource;
	if (equipmentActivation) {
		equipmentSource.owner = player;
		equipmentSource.invoker = player;
		equipmentSource.initiator = player;
		if (!skill->prepareEquipSource(const_cast<Room *>(&m_room), equipmentSource)) return nullptr;
	} else if (!request.activationRef.isValid()) {
		return nullptr;
	}
    if (m_room.isAcceptedViewAsEffect(request.activationRef)
        && request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return nullptr;
	const bool hasActivationInstance = player->hasSkillInstance(
		request.activationRef.key.skillName, request.activationRef.key.instanceID);
	const bool continuesViewAsEffect = hasViewAsSkillEffect(player, skill->objectName());
	if (request.activationRef.ownerObjectName != player->objectName()
		|| request.activationRef.key.skillName != skill->objectName()
		|| (!equipmentActivation && !hasActivationInstance && !continuesViewAsEffect))
		return nullptr;
	if (!skill->canActivateRequest(request) || !skill->cardSelectionFeasible(request))
		return nullptr;
	{
		// Enforce the limit_scope quota before create; relying only on reserve is too late,
		// because cost runs before reserve and would prompt with askForChoice before failing.
		SkillContext usageCtx;
		usageCtx.invoker = player;
		usageCtx.owner = player;
		usageCtx.initiator = player;
		usageCtx.activationRef = request.activationRef;
        if (equipmentActivation) {
            usageCtx.sourceRef = equipmentSource.sourceRef;
            usageCtx.physicalEquipSource = equipmentSource.physicalEquipSource;
        }
		usageCtx.instanceID = request.activationRef.key.instanceID;
		if (!skill->isUsable(usageCtx))
			return nullptr;
	}
	QSet<int> selectable;
	foreach (const Card *owned, player->getCards("he"))
		selectable.insert(owned->getEffectiveId());
	if (skill->isResponseOrUse()) {
		foreach (int id, player->getHandPile())
			selectable.insert(id);
	}
	foreach (int id, skill->getExpandPileCardIds(player))
		selectable.insert(id);
	ActiveSkillRequest checked = request;
	checked.selectedCardIds.clear();
	foreach (int id, request.selectedCardIds) {
		// Removing on acceptance also rejects a forged request that repeats one card ID.
		if (!selectable.remove(id)) return nullptr;
		const Card *candidate = Sanguosha->getCard(id);
		if (!candidate || !skill->canSelectCard(checked, candidate)) return nullptr;
		checked.selectedCardIds << id;
	}
	const Card *card = skill->createCard(checked);
	if (!card) return nullptr;
	Card *mutableCard = const_cast<Card *>(card);
	mutableCard->setActivationSkill(checked.activationRef.key.skillName,
		checked.activationRef.key.instanceID);
	SkillInstanceRef sourceRef = equipmentActivation ? equipmentSource.sourceRef
		: m_room.resolveSkillInstanceRootRef(checked.activationRef);
	if (!sourceRef.isValid() && continuesViewAsEffect)
		sourceRef = checked.activationRef;
	if (!sourceRef.isValid() && !equipmentActivation) return nullptr;
	mutableCard->setSourceSkill(sourceRef.key.skillName, sourceRef.key.instanceID);

	// Ordinary cards use areCardTargetsLegal() at the client/AI submission gate.
	// Pure responses have no targets; server-created uses may supply authoritative
	// targets even for target-fixed cards (for example Peach during rescue).
	const bool proxy = qobject_cast<const ActiveSkillCard *>(card->getRealCard()) != nullptr;
	if (!proxy) return card;
	if (skill->targetMode() == ViewAsSkillV2::NoTarget)
		return request.selectedTargetNames.isEmpty() ? card : nullptr;
	QList<const Player *> selectedTargets;
	foreach (const QString &name, request.selectedTargetNames) {
		ServerPlayer *candidate = m_room.findPlayerByObjectName(name);
		if (!candidate || !skill->canSelectTarget(request, selectedTargets, candidate)) return nullptr;
		if (selectedTargets.contains(candidate)) {
			// Native vote cards explicitly expose repeat capacity; an ordinary
			// proxy still cannot repeat a target even if its skill omits that check.
			int maxVotes = 0;
			if (!card->targetFilter(selectedTargets, candidate, player, maxVotes)
				|| maxVotes <= selectedTargets.count(candidate)) return nullptr;
		}
		selectedTargets << candidate;
	}
	if (!skill->targetsFeasible(request, selectedTargets)) return nullptr;
	return card;
}

void CardUseService::notifyCardProvenance(const QString &kind, ServerPlayer *initiator, const Card *card,
                                const SkillInstanceRef &sourceRef, const SkillInstanceRef &activationRef)
{
	if (!initiator || !card) return;
	CardProvenanceMessage message;
	message.kind = kind;
	message.initiator = initiator->objectName();
	message.card = card->toString();
	message.sourceOwner = sourceRef.ownerObjectName;
	message.sourceSkill = sourceRef.key.skillName;
	message.sourceInstanceId = sourceRef.key.instanceID;
	message.activationOwner = activationRef.ownerObjectName;
	message.activationSkill = activationRef.key.skillName;
	message.activationInstanceId = activationRef.key.instanceID;
	m_room.doBroadcastNotify(S_COMMAND_CARD_PROVENANCE, message.toVariant());
}

bool CardUseService::useCard(const CardUseStruct&use, bool add_history)
{
	CardUseStruct new_use = use;
	return useCard(new_use, add_history);
}

bool CardUseService::useCard(CardUseStruct&use, bool add_history)
{
    QJsonObject evidence;
    if (!qEnvironmentVariable("QSAN_CONTROLLER_SERVER_TRACE").isEmpty()) {
        evidence.insert("player", use.from ? use.from->objectName() : QString());
        evidence.insert("card", use.card ? use.card->toString() : QString());
        QStringList targets;
        for (const auto *target : use.to) if (target) targets << target->objectName();
        evidence.insert("targets", QJsonArray::fromStringList(targets));
    }
    const bool result = m_room.useCardInternal(use, add_history, nullptr);
    if (!evidence.isEmpty()) {
        evidence.insert("accepted", result);
        writeControllerServerEvidence(QStringLiteral("native-card-resolution"), evidence);
    }
    return result;
}

bool CardUseService::useCardFromSkillEffect(const CardUseStruct &use, const SkillContext &accepted, bool add_history)
{
    CardUseStruct continuation = use;
    return useCardFromSkillEffect(continuation, accepted, add_history);
}

bool CardUseService::useCardFromSkillEffect(CardUseStruct &use, const SkillContext &accepted, bool add_history)
{
    // Only trusted server skill callbacks can select this entry. No wire/card field
    // enables it, and ordinary submitted conversions always take useCard().
    if (!use.card || !use.from || use.card->getTypeId() == Card::TypeSkill
        || accepted.is_canceled || (!(accepted.sourceRef.isValid() && accepted.activationRef.isValid())
            && !accepted.physicalEquipSource.isValid())
        || (accepted.current_event != EventSkillEffect && accepted.current_event != EventSkillEffectTarget))
        return false;
    const SkillContext frozen = accepted;
    return m_room.useCardInternal(use, add_history, &frozen);
}

bool CardUseService::physicalCardEffectContext(const Card *card, SkillContext &context) const
{
    if (!card || card->isVirtualCard() || card->getTypeId() == Card::TypeSkill) return false;
    const int id = card->getEffectiveId();
    if (id < 0 || id >= Sanguosha->getCardCount()) return false;
    const WrappedCard *wrapped = Sanguosha->getWrappedCard(id);
    // A copied ID or caller-supplied card is not the authoritative room wrapper.
    if (!wrapped || (card != wrapped && card != wrapped->getRealCard())) return false;
    const QVariantMap receipt = wrapped->appliedPhysicalEffectSource();
    if (receipt.isEmpty() || receipt.value("serial").toLongLong() <= 0 || receipt.value("card_id", -1).toInt() != id
        || receipt.value("name").toString() != wrapped->objectName()
        || receipt.value("class_name").toString() != wrapped->getClassName()
        || !receipt.contains("suit") || receipt.value("suit").toInt() != int(wrapped->getSuit())
        || !receipt.contains("number") || receipt.value("number").toInt() != wrapped->getNumber()) return false;
    const SkillInstanceRef source(receipt.value("source_owner").toString(),
        SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
    const SkillInstanceRef activation(receipt.value("activation_owner").toString(),
        SkillInstanceKey(receipt.value("activation_skill").toString(), receipt.value("activation_instance").toInt()));
    if (!source.isValid() || !activation.isValid()
        || wrapped->getSourceSkillName() != source.key.skillName
        || wrapped->getSourceSkillInstanceId() != source.key.instanceID
        || wrapped->getActivationSkillName() != activation.key.skillName
        || wrapped->getActivationSkillInstanceId() != activation.key.instanceID) return false;
    context = SkillContext();
    context.owner = m_room.findPlayerByObjectName(activation.ownerObjectName, true);
    if (!context.owner || !m_room.findPlayerByObjectName(source.ownerObjectName, true)) return false;
    context.sourceRef = source;
    context.activationRef = activation;
    context.instanceID = activation.key.instanceID;
    context.skill_name = activation.key.skillName;
    context.current_event = EventSkillEffect;
    return true;
}

bool CardUseService::setPhysicalCardEffectSource(int cardId, const SkillContext &accepted)
{
    if (cardId < 0 || cardId >= Sanguosha->getCardCount() || accepted.is_canceled
        || !accepted.sourceRef.isValid() || !accepted.activationRef.isValid()
        || (accepted.current_event != EventSkillEffect && accepted.current_event != EventSkillEffectTarget)) return false;
    WrappedCard *wrapped = Sanguosha->getWrappedCard(cardId);
    if (!wrapped || wrapped->getTypeId() == Card::TypeSkill || wrapped->hasFlag("using")
        || m_room.getCardPlace(cardId) == Player::PlaceUnknown) return false;
    const qint64 serial = m_room.getTag("AppliedPhysicalCardSequence").toLongLong() + 1;
    if (serial <= 0) return false;
    m_room.setTag("AppliedPhysicalCardSequence", serial);
    const QVariantMap receipt{{"serial", serial}, {"card_id", cardId}, {"name", wrapped->objectName()},
        {"class_name", wrapped->getClassName()}, {"suit", int(wrapped->getSuit())}, {"number", wrapped->getNumber()},
        {"source_owner", accepted.sourceRef.ownerObjectName}, {"source_skill", accepted.sourceRef.key.skillName},
        {"source_instance", accepted.sourceRef.key.instanceID}, {"activation_owner", accepted.activationRef.ownerObjectName},
        {"activation_skill", accepted.activationRef.key.skillName}, {"activation_instance", accepted.activationRef.key.instanceID}};
    for (Card *part : QList<Card *>{wrapped, const_cast<Card *>(wrapped->getRealCard())}) {
        part->setSourceSkill(accepted.sourceRef.key.skillName, accepted.sourceRef.key.instanceID);
        part->setActivationSkill(accepted.activationRef.key.skillName, accepted.activationRef.key.instanceID);
        part->m_appliedPhysicalEffectSource = receipt;
    }
    return true;
}

bool CardUseService::restorePhysicalCardEffectSource(int cardId, const QVariantMap &receipt)
{
    if (!m_room.m_takeoverRestoring || cardId < 0 || cardId >= Sanguosha->getCardCount()) return false;
    WrappedCard *wrapped = Sanguosha->getWrappedCard(cardId);
    if (!wrapped) return false;
    wrapped->m_appliedPhysicalEffectSource = receipt;
    SkillContext restored;
    if (!receipt.isEmpty() && !physicalCardEffectContext(wrapped, restored)) {
        wrapped->m_appliedPhysicalEffectSource.clear();
        return false;
    }
    Card *inner = const_cast<Card *>(wrapped->getRealCard());
    inner->setSourceSkill(wrapped->getSourceSkillName(), wrapped->getSourceSkillInstanceId());
    inner->setActivationSkill(wrapped->getActivationSkillName(), wrapped->getActivationSkillInstanceId());
    inner->m_appliedPhysicalEffectSource = receipt;
    return true;
}

Room::SkillCardMaterialLocations CardUseService::skillCardMaterialLocations(const Card *card) const
{
    Room::SkillCardMaterialLocations locations;
    if (!card) return locations;
    const QList<int> ids = card->isVirtualCard() ? card->getSubcards() : QList<int>() << card->getEffectiveId();
    for (int id : ids) {
        ServerPlayer *owner = m_room.getCardOwner(id);
        const Player::Place place = m_room.getCardPlace(id);
        locations.insert(id, {owner, place,
            owner && place == Player::PlaceSpecial ? owner->getPileName(id) : QString()});
    }
    return locations;
}

bool CardUseService::skillEffectCardMaterialsValid(const Card *card,
    const Room::SkillCardMaterialLocations *admittedLocations) const
{
    if (!card || card->getTypeId() == Card::TypeSkill) return false;
    const QList<int> ids = card->isVirtualCard() ? card->getSubcards() : QList<int>() << card->getEffectiveId();
    QSet<int> seen;
    for (int id : ids) {
        if (id < 0 || id >= Sanguosha->getCardCount() || seen.contains(id)) return false;
        seen.insert(id);
        const Card *material = Sanguosha->getCard(id);
        if (!material || material->hasFlag("using") || m_room.getCardPlace(id) == Player::PlaceUnknown) return false;
        if (admittedLocations) {
            const auto admitted = admittedLocations->constFind(id);
            if (admitted != admittedLocations->constEnd()
                && (m_room.getCardOwner(id) != admitted->owner || m_room.getCardPlace(id) != admitted->place
                    || (admitted->owner && admitted->place == Player::PlaceSpecial
                        && admitted->owner->getPileName(id) != admitted->pileName))) return false;
        }
    }
    // Foreign-zone material is only supplied by a trusted effect callback. The
    // skill owns its exact reveal/provision receipt; ownership is not guessed here.
    return true;
}

bool CardUseService::resolveCardUse(CardUseStruct &use, bool add_history, const SkillContext *acceptedEffect)
{
    SkillContext physicalEffect;
    QVariantMap physicalEffectReceipt;
    if (!acceptedEffect && use.card && !use.card->appliedPhysicalEffectSource().isEmpty()) {
        if (!physicalCardEffectContext(use.card, physicalEffect) || !use.from
            || m_room.getCardOwner(use.card->getEffectiveId()) != use.from
            || use.card->hasFlag("using")) return false;
        physicalEffect.invoker = physicalEffect.initiator = use.from;
        physicalEffectReceipt = use.card->appliedPhysicalEffectSource();
        acceptedEffect = &physicalEffect;
    }
    const QString admittedPattern = m_room.m_runtime->state().getCurrentCardUsePattern();
    const auto admittedReason = m_room.m_runtime->state().getCurrentCardUseReason();
    if (acceptedEffect == &physicalEffect) {
        const int id = use.card->getEffectiveId();
        const bool ownedMaterial = m_room.getCardPlace(id) == Player::PlaceHand || use.from->getHandPile().contains(id);
        const bool patternMatches = admittedReason == CardUseStruct::CARD_USE_REASON_PLAY
            || admittedPattern.isEmpty() || admittedPattern.startsWith("@")
            || Sanguosha->matchPattern(admittedPattern, use.from, use.card);
        if (!use.from->isAlive() || !ownedMaterial || !patternMatches
            || (admittedReason == CardUseStruct::CARD_USE_REASON_PLAY && !use.card->isAvailable(use.from))) return false;
    }
	CardLifetimeScope cardScope(globalCardLifetimeManager());
	use.cardFinished = false;
    use.m_acceptedSkillEffectCard = acceptedEffect != nullptr;
	if (acceptedEffect) {
        use.sourceRef = acceptedEffect->sourceRef;
        use.physicalEquipSource = acceptedEffect->physicalEquipSource;
        use.activationRef = acceptedEffect->activationRef;
        use.skillExecutionID = acceptedEffect->executionID;
        use.hasSkillActivationRequest = false;
        if (use.card->isVirtualCard()) {
            Card *generated = const_cast<Card *>(use.card);
            generated->setSourceSkill(use.sourceRef.key.skillName, use.sourceRef.key.instanceID);
            generated->setActivationSkill(use.activationRef.key.skillName, use.activationRef.key.instanceID);
        }
    } else {
        if (!m_room.resolveCardSkillInstance(use)) return false;
        if (!m_room.canShowGeneralForSkill(use.activationRef)) return false;
        if (!use.activationRef.isValid() && !m_room.canShowGeneralForSkill(use.sourceRef)) return false;
    }
	// Revalidate only client/AI-selected targets. Server-created uses may carry
	// authoritative targets for target-fixed cards, such as Peach in dying rescue.
	if (use.m_validateTargets && !areCardTargetsLegal(use)) return false;
	if ((!use.card->canRecast()||use.from->isCardLimited(use.card,Card::MethodRecast))&&use.from->isCardLimited(use.card,use.card->getHandlingMethod()))
		return false;
	use.m_addHistory = add_history;
	bool residue_unlimited = false;
	if (use.to.isEmpty()) {
		residue_unlimited = Sanguosha->hasResidueUnlimited(use.from, use.card, nullptr);
	} else {
		foreach (ServerPlayer *target, use.to) {
			if (Sanguosha->hasResidueUnlimited(use.from, use.card, target)) {
				residue_unlimited = true;
				break;
			}
		}
	}
	if (residue_unlimited)
		use.m_addHistory = false;
	const Card*card = use.card->validate(use);
	if(card==nullptr) return false;
    if (acceptedEffect == &physicalEffect) {
        SkillContext revalidated;
        if (card->getEffectiveId() != use.card->getEffectiveId()
            || card->appliedPhysicalEffectSource() != physicalEffectReceipt
            || !physicalCardEffectContext(card, revalidated)
            || revalidated.sourceRef != physicalEffect.sourceRef
            || revalidated.activationRef != physicalEffect.activationRef) return false;
    }
    if (acceptedEffect) {
        // A native validator cannot turn a continuation into a new SkillCard activation.
        if (card->getTypeId() == Card::TypeSkill) return false;
        use.sourceRef = acceptedEffect->sourceRef;
        use.physicalEquipSource = acceptedEffect->physicalEquipSource;
        use.activationRef = acceptedEffect->activationRef;
        use.skillExecutionID = acceptedEffect->executionID;
        if (card->isVirtualCard()) {
            const_cast<Card *>(card)->setSourceSkill(use.sourceRef.key.skillName, use.sourceRef.key.instanceID);
            const_cast<Card *>(card)->setActivationSkill(use.activationRef.key.skillName, use.activationRef.key.instanceID);
        }
    }
	prepareTargetModSkillReveal(use);
	notifyCardProvenance("use", use.from, card, use.sourceRef, use.activationRef);
	// Provenance already publishes the card; private target drafts stay private.
	Room::ResolutionScope resolution(m_room, QStringLiteral("card"), use.from, use.from, nullptr, card->objectName());
	ResolutionHistoryEventGuard useHistory(m_room.m_resolutionHistory, QStringLiteral("use_card"),
		{{QStringLiteral("from"), use.from->objectName()},
		 {QStringLiteral("card"), m_room.historyCardSnapshot(card)},
         {QStringLiteral("physical_equipment"), use.physicalEquipSource.toVariantMap()}}, m_room.historyRecordingEnabled());
    if (acceptedEffect == &physicalEffect && useHistory.id() > 0) {
        QVariantMap identity = m_room.historySkillContext(physicalEffect);
        identity.insert(QStringLiteral("applied_physical_effect"), true);
        useHistory.update(identity);
    }
	use.targetModReveal.useHistoryEventId = useHistory.id();
	std::unique_ptr<ResolutionHistoryEventGuard> skillHistory;

	bool isSkillCard = card->isKindOf("SkillCard");
	bool isViewAsCard = !acceptedEffect && !isSkillCard && card->isVirtualCard() && !card->getSkillName().isEmpty();
    // Delegation changes the actor, not the admitted material's owner or zone.
    const auto admittedMaterialLocations = isViewAsCard
        ? skillCardMaterialLocations(card) : Room::SkillCardMaterialLocations();
	SkillContext skillCardCtx;
	SkillContext skillCardIdentity;
	ServerPlayer *skillCardInvoker = nullptr;
	QVariant skillCardCtxData;
	SkillExecutionRegistry::Guard skillExecution;
	bool skipOnUse = false;
	bool skillUsageReserved = false;
	bool skillUsageCommitted = false;
	bool skillExecutionFinishStarted = false;
	bool cardProcessingStarted = false;
	bool cardProcessingCompleted = false;
	bool cardReplacedDuringActivation = false;
    bool delegatedActivation = false;
    bool ordinaryEffectHandled = false;
	const ViewAsSkillV2 *activeSkill = nullptr;
	QString tagKey;
	QList<int> ids;
	QString key;
	auto loadSkillContext = [&]() {
		return skillExecution.executionID() > 0
			? m_room.getSkillExecutionContext(skillExecution.executionID())
			: m_room.getTag(tagKey).value<SkillContext>();
	};
	auto restoreSkillCardIdentity = [&](SkillContext &context) {
		context.skill_name = skillCardIdentity.skill_name;
		context.sourceRef = skillCardIdentity.sourceRef;
        context.physicalEquipSource = skillCardIdentity.physicalEquipSource;
		context.activationRef = skillCardIdentity.activationRef;
		context.initiator = skillCardIdentity.initiator;
		context.instanceID = skillCardIdentity.instanceID;
	};
	auto saveSkillContext = [&](const SkillContext &context) {
		if (skillExecution.executionID() > 0)
			m_room.setSkillExecutionContext(skillExecution.executionID(), context);
		else
			m_room.setTag(tagKey, QVariant::fromValue(context));
	};
	auto adoptUpdatedCard = [&]() {
		if (!skillCardCtx.updated_card) return;
		// Cost and payment replacements use the same lifetime/metadata path.
		// Keep updated_card through pay(), where it may select the payment branch.
		Card *replacement = const_cast<Card *>(skillCardCtx.updated_card);
		if (replacement != use.card) use.changeCard(replacement);
		CardLifetimeManager &manager = globalCardLifetimeManager();
		const auto replacementToken = manager.liveToken(replacement);
		if (replacementToken && manager.state(replacementToken) == CardLifetimeState::PendingDelete)
			use.setOwnedCard(replacement);
		replacement->setActivationSkill(skillCardIdentity.activationRef.key.skillName,
			skillCardIdentity.activationRef.key.instanceID);
		replacement->setSourceSkill(skillCardIdentity.sourceRef.key.skillName,
			skillCardIdentity.sourceRef.key.instanceID);
		cardReplacedDuringActivation = cardReplacedDuringActivation || replacement != card;
		card = use.card;
		skillCardCtx.use_card = card;
	};
    auto areActivationTargetsLegal = [&](const CardUseStruct &selection) {
        if (!delegatedActivation) return areCardTargetsLegal(selection);
        // Delegation preserves the initiator's admitted target rules. Updated
        // targets must still be real players in this room.
        const QList<ServerPlayer *> players = m_room.getPlayers();
        for (ServerPlayer *target : selection.to)
            if (!target || !players.contains(target)) return false;
        return true;
    };
	auto finishSkillExecution = [&](SkillExecutionResult result) {
		if (!(isSkillCard || isViewAsCard) || skillExecutionFinishStarted) return;
		skillExecutionFinishStarted = true;
		skillCardCtx = loadSkillContext();
		skillCardCtx.current_event = EventSkillEffectFinished;
		skillCardCtxData = QVariant::fromValue(skillCardCtx);
		try {
			m_room.thread->trigger(EventSkillEffectFinished, &m_room, use.from, skillCardCtxData);
		} catch (...) {
			skillCardCtx = skillCardCtxData.value<SkillContext>();
			restoreSkillCardIdentity(skillCardCtx);
			if (skillCardInvoker) skillCardCtx.invoker = skillCardInvoker;
			saveSkillContext(skillCardCtx);
			if (skillHistory) {
				skillHistory->update(m_room.historySkillContext(skillCardCtx));
				skillHistory->finish(QStringLiteral("aborted"));
			}
			if (skillExecution.executionID() > 0) {
				m_room.recordSkillExecutionAudit(skillCardCtx, result);
				skillExecution.finish(result);
			} else {
				m_room.removeTag(tagKey);
			}
			throw;
		}
		skillCardCtx = skillCardCtxData.value<SkillContext>();
		restoreSkillCardIdentity(skillCardCtx);
		if (skillCardInvoker) skillCardCtx.invoker = skillCardInvoker;
		saveSkillContext(skillCardCtx);
		// Snapshot before releasing execution-owned card/context storage.
		if (skillHistory) skillHistory->update(m_room.historySkillContext(skillCardCtx));
		if (skillExecution.executionID() > 0) {
			m_room.recordSkillExecutionAudit(skillCardCtx, result);
			skillExecution.finish(result);
		} else {
			m_room.removeTag(tagKey);
		}
		if (skillHistory) {
			skillHistory->finish(result == SkillExecutionCompleted ? QStringLiteral("completed")
				: result == SkillExecutionNoResult ? QStringLiteral("aborted")
				: result == SkillExecutionEffectSkipped ? QStringLiteral("skipped")
				: QStringLiteral("cancelled"));
		}
		if (result == SkillExecutionPayFailed || result == SkillExecutionInvalidTargetUpdate)
			useHistory.finish(QStringLiteral("cancelled"));
	};

	try {
	if (isSkillCard || isViewAsCard) {
		const SkillCard *skillCard = isSkillCard ? qobject_cast<const SkillCard*>(card->getRealCard()) : nullptr;

		skillCardCtx.skill_name = use.sourceRef.isValid() ? use.sourceRef.key.skillName
								  : (card->getSkillName().isEmpty() ? card->objectName() : card->getSkillName());
		skillCardCtx.invoker = use.from;
		skillCardCtx.owner = skillCard ? skillCard->getSkillOwner() : nullptr;
		if (!skillCardCtx.owner && use.sourceRef.isValid())
			skillCardCtx.owner = m_room.findPlayerByObjectName(use.sourceRef.ownerObjectName);
		if (!skillCardCtx.owner) skillCardCtx.owner = use.from;
		skillCardCtx.targets = use.to;
		skillCardCtx.instanceID = use.activationRef.isValid() ? use.activationRef.key.instanceID
								: (skillCard ? skillCard->getSkillInstanceId() : 0);
		skillCardCtx.use_card = card;
		skillCardCtx.sourceRef = use.sourceRef;
        skillCardCtx.physicalEquipSource = use.physicalEquipSource;
		skillCardCtx.activationRef = use.activationRef;
		skillCardCtx.initiator = use.from;
		activeSkill = dynamic_cast<const ViewAsSkillV2 *>(
			Sanguosha->getViewAsSkill(skillCardCtx.activationRef.key.skillName));
		if (activeSkill) {
			bool amountOk = false;
			skillCardCtx.amount = m_room.getSkillInstanceAmount(
				activeSkill->getAmountRef(skillCardCtx), &amountOk);
			if (!amountOk) skillCardCtx.amount = activeSkill->getBaseAmount();
		}
		skillCardIdentity = skillCardCtx;
		skillExecution = m_room.beginSkillExecution(skillCardCtx, QVariant::fromValue(use));
		use.skillExecutionID = skillExecution.executionID();
		skillHistory = std::make_unique<ResolutionHistoryEventGuard>(m_room.m_resolutionHistory,
			QStringLiteral("skill"), m_room.historySkillContext(skillCardCtx), m_room.historyRecordingEnabled());

		QString prefix = isViewAsCard ? "ViewAsContext_" : "SkillCardContext_";
		tagKey = prefix + skillCardCtx.skill_name + "_"
				 + QString::number(skillCardCtx.instanceID);

		saveSkillContext(skillCardCtx);
		if (activeSkill && !skillCardCtx.bypass_cost) {
			ActiveSkillRequest request;
			request.reason = m_room.m_runtime->state().getCurrentCardUseReason();
			request.pattern = m_room.m_runtime->state().getCurrentCardUsePattern();
			request.initiator = skillCardCtx.initiator;
			request.activationRef = skillCardCtx.activationRef;
			request.setCardSelection(use.card);
			foreach (ServerPlayer *target, use.to) request.selectedTargetNames << target->objectName();
            bool paidCost = false;
            {
                Room::AiSkillContextScope aiContext(m_room, skillCardCtx);
                paidCost = activeSkill->cost(&m_room, skillCardCtx, request);
            }
			restoreSkillCardIdentity(skillCardCtx);
			skillCardCtx.invoker = skillCardIdentity.invoker;
			if (!paidCost) {
				finishSkillExecution(SkillExecutionPayFailed);
				return false;
			}
		}

		skillCardCtx.current_event = EventSkillWillInvoke;
		skillCardCtxData = QVariant::fromValue(skillCardCtx);
		m_room.thread->trigger(EventSkillWillInvoke, &m_room, use.from, skillCardCtxData);
		skillCardCtx = skillCardCtxData.value<SkillContext>();
		restoreSkillCardIdentity(skillCardCtx);
		if (skillCardCtx.is_canceled) {
			finishSkillExecution(SkillExecutionInvalidTargetUpdate);
			return false;
		}
		skillCardInvoker = skillCardCtx.invoker ? skillCardCtx.invoker : skillCardIdentity.invoker;
		skillCardCtx.invoker = skillCardInvoker;
		delegatedActivation = activeSkill && skillCardInvoker != skillCardIdentity.initiator;
		if (skillCardCtx.invoker && skillCardCtx.invoker != use.from) {
			if (!m_room.getPlayers().contains(skillCardCtx.invoker) || !skillCardCtx.invoker->isAlive()
                || skillCardCtx.invoker->isCardLimited(card, card->getHandlingMethod())) {
				finishSkillExecution(SkillExecutionInvalidTargetUpdate);
				return false;
			}
			use.from = skillCardCtx.invoker;
		}
		if (skillCardCtx.updated_card) {
			adoptUpdatedCard();
			saveSkillContext(skillCardCtx);
			if (use.from->isCardLimited(card, card->getHandlingMethod())) {
				finishSkillExecution(SkillExecutionInvalidTargetUpdate);
				return false;
			}
		}

        if (isViewAsCard && activeSkill && !skillEffectCardMaterialsValid(card, &admittedMaterialLocations)) {
            finishSkillExecution(SkillExecutionInvalidTargetUpdate);
            return false;
        }
		if (activeSkill && !m_room.reserveActiveSkillUsage(activeSkill, skillCardCtx)) {
			finishSkillExecution(SkillExecutionPayFailed);
			return false;
		}
		skillUsageReserved = hasGenericActiveSkillUsage(activeSkill);
		if (!skillCardCtx.bypass_cost) {
			skillCardCtx.current_event = EventSkillPay;
			skillCardCtxData = QVariant::fromValue(skillCardCtx);
			m_room.thread->trigger(EventSkillPay, &m_room, use.from, skillCardCtxData);
			skillCardCtx = skillCardCtxData.value<SkillContext>();
			restoreSkillCardIdentity(skillCardCtx);
			skillCardCtx.invoker = skillCardInvoker;
			if (skillCardCtx.is_canceled) {
				if (skillUsageReserved)
					m_room.releaseActiveSkillUsage(activeSkill, skillCardCtx);
				skillUsageReserved = false;
				finishSkillExecution(SkillExecutionPayFailed);
				return false;
			}
		}
        if (isViewAsCard && activeSkill && !skillEffectCardMaterialsValid(
            skillCardCtx.updated_card ? skillCardCtx.updated_card : card, &admittedMaterialLocations)) {
            if (skillUsageReserved) m_room.releaseActiveSkillUsage(activeSkill, skillCardCtx);
            skillUsageReserved = false;
            finishSkillExecution(SkillExecutionPayFailed);
            return false;
        }
		if (activeSkill && !skillCardCtx.bypass_cost) {
			ActiveSkillRequest request;
			request.reason = m_room.m_runtime->state().getCurrentCardUseReason();
			request.pattern = m_room.m_runtime->state().getCurrentCardUsePattern();
			request.initiator = skillCardCtx.initiator;
			request.activationRef = skillCardCtx.activationRef;
			request.setCardSelection(use.card);
			foreach (ServerPlayer *target, use.to) request.selectedTargetNames << target->objectName();
			// Payment metadata belongs to this execution's admitted CardUseStruct.
			SkillExecutionRegistry::Entry *entry = skillExecution.get();
			skillCardCtx.original_data = entry ? &entry->backingData : nullptr;
            bool paid = false;
            {
                Room::AiSkillContextScope aiContext(m_room, skillCardCtx);
                paid = activeSkill->pay(&m_room, skillCardCtx, request);
            }
			restoreSkillCardIdentity(skillCardCtx);
			skillCardCtx.invoker = skillCardInvoker;
			if (!paid || skillCardCtx.is_canceled) {
				if (skillUsageReserved)
					m_room.releaseActiveSkillUsage(activeSkill, skillCardCtx);
				skillUsageReserved = false;
				finishSkillExecution(SkillExecutionPayFailed);
				return false;
			}
		}
		adoptUpdatedCard();
		skillCardCtx.updated_card = nullptr;
        if (isViewAsCard && activeSkill && !skillEffectCardMaterialsValid(card, &admittedMaterialLocations)) {
            if (skillUsageReserved) m_room.releaseActiveSkillUsage(activeSkill, skillCardCtx);
            skillUsageReserved = false;
            finishSkillExecution(SkillExecutionInvalidTargetUpdate);
            return false;
        }
		if (cardReplacedDuringActivation) {
			// Provision changes only ownership semantics; callbacks cannot replace
			// the actor, target list, or provenance through the backing use payload.
			const SkillExecutionRegistry::Entry *entry = skillExecution.get();
			if (entry && entry->backingData.canConvert<CardUseStruct>())
				use.m_isOwnerUse = entry->backingData.value<CardUseStruct>().m_isOwnerUse;
			CardUseStruct selection = use;
			selection.to = skillCardCtx.targets;
			const bool legalTargets = delegatedActivation ? areActivationTargetsLegal(selection)
                : (!use.m_validateTargets && card->targetFixed()) || areCardTargetsLegal(selection);
			saveSkillContext(skillCardCtx);
			if (!use.from->isAlive() || use.from->isCardLimited(card, card->getHandlingMethod()) || !legalTargets) {
				if (skillUsageReserved) m_room.releaseActiveSkillUsage(activeSkill, skillCardCtx);
				skillUsageReserved = false;
				finishSkillExecution(SkillExecutionInvalidTargetUpdate);
				return false;
			}
		}
		// Equipment entries have no activation instance; reveal their admitted grant.
		if (activeSkill && activeSkill->isEquipSkill() && !skillCardCtx.activationRef.isValid()
			&& (!m_room.showGeneralForSkill(skillCardCtx.sourceRef)
				|| !activeSkill->isEquipSourceAvailable(&m_room, skillCardCtx))) {
			if (skillUsageReserved) m_room.releaseActiveSkillUsage(activeSkill, skillCardCtx);
			skillUsageReserved = false;
			saveSkillContext(skillCardCtx);
			finishSkillExecution(SkillExecutionPayFailed);
			return false;
		}
		// A paid skill reveals its exact source before consuming its use count.
		// Reveal callbacks may remove that source or kill the acting player.
		// Original skill cards pay extraCost during onUse, after PreCardUsed.
        // Their exact source is revealed there, before CardUsed, as in the donor.
        const bool deferredHegemonyReveal = Config.EnableHegemony
            && ((use.card->getTypeId() == Card::TypeSkill
                    && QByteArray(use.card->metaObject()->className()).startsWith("H"))
                || (use.activationRef.isValid() && use.card->isVirtualCard()
                    && use.card->needsDeferredHegemonyReveal()));
        if ((!deferredHegemonyReveal && !m_room.showGeneralForSkill(skillCardCtx.activationRef))
            || (Config.EnableHegemony && !use.from->isAlive())) {
			if (skillUsageReserved) m_room.releaseActiveSkillUsage(activeSkill, skillCardCtx);
			skillUsageReserved = false;
			saveSkillContext(skillCardCtx);
			finishSkillExecution(SkillExecutionPayFailed);
			return false;
		}
		if (activeSkill) {
			m_room.commitActiveSkillUsage(activeSkill, skillCardCtx);
			skillUsageCommitted = skillUsageReserved;
			skillUsageReserved = false;
		}
		use.bypass_cost = skillCardCtx.bypass_cost;

		skillCardCtx.updated_targets = skillCardCtx.targets;
		skillCardCtx.current_event = EventSkillTargetConfirming;
		skillCardCtxData = QVariant::fromValue(skillCardCtx);
		m_room.thread->trigger(EventSkillTargetConfirming, &m_room, use.from, skillCardCtxData);
		skillCardCtx = skillCardCtxData.value<SkillContext>();
		restoreSkillCardIdentity(skillCardCtx);
		skillCardCtx.invoker = skillCardInvoker;
		if (!skillCardCtx.updated_targets.isEmpty()) {
			use.to = skillCardCtx.updated_targets;
		}

		saveSkillContext(skillCardCtx);
		// Target-confirming hooks may alter either cost or payment replacement recipients.
		if ((delegatedActivation || (cardReplacedDuringActivation && (use.m_validateTargets || !card->targetFixed())))
			&& !areActivationTargetsLegal(use)) {
			finishSkillExecution(SkillExecutionInvalidTargetUpdate);
			return false;
		}
	}

    if (isViewAsCard && activeSkill) {
        const auto restoreRequest = qScopeGuard([&] { m_room.setCurrentCardUse(admittedPattern, admittedReason); });
        if (!skillEffectCardMaterialsValid(card, &admittedMaterialLocations)) {
            finishSkillExecution(SkillExecutionInvalidTargetUpdate);
            return false;
        }
        // Ordinary conversions have an accepted effect stage too. Run it before
        // recording the actual card use, so a failed reveal never counts a preview.
        ordinaryEffectHandled = true;
        skillCardCtx = loadSkillContext();
        skillCardCtx.targets = use.to;
        skillCardCtx.current_event = EventSkillInvoking;
        skillCardCtxData = QVariant::fromValue(skillCardCtx);
        m_room.thread->trigger(EventSkillInvoking, &m_room, use.from, skillCardCtxData);
        skillCardCtx = skillCardCtxData.value<SkillContext>();
        restoreSkillCardIdentity(skillCardCtx); skillCardCtx.invoker = skillCardInvoker;
        skillCardCtx.current_event = EventSkillEffect;
        skillCardCtx.updated_targets.clear();
        skillCardCtxData = QVariant::fromValue(skillCardCtx);
        const bool intercepted = m_room.thread->trigger(EventSkillEffect, &m_room, use.from, skillCardCtxData);
        skillCardCtx = skillCardCtxData.value<SkillContext>();
        restoreSkillCardIdentity(skillCardCtx); skillCardCtx.invoker = skillCardInvoker;
        skillCardCtx.current_event = EventSkillEffect;
        saveSkillContext(skillCardCtx);
        ViewAsSkillV2::EffectFlow flow = ViewAsSkillV2::ContinueEffects;
        m_room.setCurrentCardUse(admittedPattern, admittedReason);
        if (!intercepted && !skillCardCtx.is_canceled) {
            Room::AiSkillContextScope aiContext(m_room, skillCardCtx);
            flow = activeSkill->effect(skillCardCtx);
        }
        m_room.setCurrentCardUse(admittedPattern, admittedReason);
        restoreSkillCardIdentity(skillCardCtx); skillCardCtx.invoker = skillCardInvoker;
        // Replacement callbacks must return an owned virtual clone with material
        // IDs, never mutate the shared physical engine card through changeCard().
        if (skillCardCtx.updated_card && !skillCardCtx.updated_card->isVirtualCard()) skillCardCtx.is_canceled = true;
        else adoptUpdatedCard();
        skillCardCtx.updated_card = nullptr;
        if (!skillCardCtx.updated_targets.isEmpty()) use.to = skillCardCtx.updated_targets;
        SkillExecutionRegistry::Entry *entry = skillExecution.get();
        if (entry && entry->backingData.canConvert<CardUseStruct>()) {
            use.m_isOwnerUse = entry->backingData.value<CardUseStruct>().m_isOwnerUse;
            entry->backingData = QVariant::fromValue(use);
        }
        skillCardCtx.use_card = card; skillCardCtx.targets = use.to;
        saveSkillContext(skillCardCtx);
        if (!skillEffectCardMaterialsValid(card, &admittedMaterialLocations)) {
            finishSkillExecution(SkillExecutionInvalidTargetUpdate);
            useHistory.finish(QStringLiteral("cancelled"));
            return false;
        }
        if (intercepted || skillCardCtx.is_canceled || flow == ViewAsSkillV2::FinishSkill) {
            if (use.m_isOwnerUse) {
                // An ordinary conversion has not paid its own material yet.
                finishSkillExecution(SkillExecutionEffectSkipped);
                useHistory.finish(QStringLiteral("skipped"));
                return false;
            }
            // Accepted supplied material still needs CardUsed/CardFinished cleanup.
            use.skipSkillEffect = true;
        } else {
            const QString pattern = admittedPattern;
            const bool responseUse = admittedReason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
            const bool validPattern = !responseUse || pattern.startsWith("@") || pattern.isEmpty()
                || Sanguosha->matchPattern(pattern, use.from, card);
            if (!use.from->isAlive() || !skillEffectCardMaterialsValid(card)
                || (!delegatedActivation && admittedReason == CardUseStruct::CARD_USE_REASON_PLAY && !card->isAvailable(use.from))
                || use.from->isCardLimited(card, card->getHandlingMethod()) || !validPattern
                || ((delegatedActivation || use.m_validateTargets || !card->targetFixed()) && !areActivationTargetsLegal(use))) {
                finishSkillExecution(SkillExecutionInvalidTargetUpdate);
                useHistory.finish(QStringLiteral("cancelled"));
                return false;
            }
        }
    }

	{
		CardUseStruct selection = use;
		// validate() may replace a virtual card without changing use.card yet.
		if (use.card->getRealCard() != card) selection.card = card;
		planTargetModSkillReveal(selection);
		use.targetModReveal = selection.targetModReveal;
	}
	if (use.card->isVirtualCard()) ids = use.card->getSubcards();
	else ids << use.card->getId();
	foreach(int id, ids){
		use.m_isHandcard = use.from->handCards().contains(id);
		if (!use.m_isHandcard) break;
	}
	key = use.card->getClassName();
	m_room.tag.remove("UseHistory"+use.card->toString());
	if (use.card->inherits("LuaSkillCard")) key = "#"+use.card->objectName();
	const ViewAsSkillV2 *historySkill = dynamic_cast<const ViewAsSkillV2 *>(
		Sanguosha->getViewAsSkill(use.activationRef.key.skillName));
	// Ordinary conversions keep their card class so native limits (e.g. Slash residue)
	// count them; only skill/proxy cards are recorded under the skill's history key.
	if (historySkill && use.card->getTypeId() == Card::TypeSkill) {
		ActiveSkillRequest request;
		request.reason = m_room.m_runtime->state().getCurrentCardUseReason();
		request.pattern = m_room.m_runtime->state().getCurrentCardUsePattern();
		request.initiator = use.from;
		request.activationRef = use.activationRef;
		request.setCardSelection(use.card);
		foreach (ServerPlayer *target, use.to) request.selectedTargetNames << target->objectName();
		key = historySkill->historyKey(request);
	}
	//use.m_isOwnerUse = (ids.isEmpty()&&use.m_isOwnerUse)||getCardOwner(ids.first())==use.from;
	if(m_room.m_runtime->state().getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_PLAY)
		m_room.addPlayerHistory(nullptr, "pushPile");
	// The final rollback undoes only history counted here; unlimited uses never added one.
	add_history = use.m_addHistory;
	if (use.m_addHistory){
		m_room.addPlayerHistory(use.from, key);
		use.targetModReveal.historyKey = key;
	}

	if (!ordinaryEffectHandled && (isSkillCard || isViewAsCard)) {
		skillCardCtx = loadSkillContext();
		skillCardCtx.current_event = EventSkillInvoking;
		skillCardCtxData = QVariant::fromValue(skillCardCtx);
		m_room.thread->trigger(EventSkillInvoking, &m_room, use.from, skillCardCtxData);
		skillCardCtx = skillCardCtxData.value<SkillContext>();
		restoreSkillCardIdentity(skillCardCtx);
		skillCardCtx.invoker = skillCardInvoker;
		saveSkillContext(skillCardCtx);
	}

	if (!ordinaryEffectHandled && (isSkillCard || isViewAsCard)) {
		skillCardCtx = loadSkillContext();
		skillCardCtx.current_event = EventSkillEffect;
		skillCardCtxData = QVariant::fromValue(skillCardCtx);
		skipOnUse = m_room.thread->trigger(EventSkillEffect, &m_room, use.from, skillCardCtxData);
		skillCardCtx = skillCardCtxData.value<SkillContext>();
        // Cancellation need not break the surrounding event dispatch.
        skipOnUse = skipOnUse || skillCardCtx.is_canceled;
		if (skipOnUse) {
			if (card->property("LegacyOnUseLimited").toBool()) {
				// Explicitly marked monolithic cards cannot safely enter their custom onUse.
				use.skipSkillEffect = false;
			} else {
				use.skipSkillEffect = true;
				skipOnUse = false;
			}
		}
		restoreSkillCardIdentity(skillCardCtx);
		skillCardCtx.invoker = skillCardInvoker;
		saveSkillContext(skillCardCtx);
	}

		if (!skipOnUse && m_room.historyRecordingEnabled()) {
			QVariantMap fact = m_room.historyCause(CardMoveReason(CardMoveReason::S_REASON_USE,
				use.from->objectName(), card->getSkillName(), QString()));
            if (acceptedEffect) {
                const QVariantMap identity = m_room.historySkillContext(*acceptedEffect);
                for (auto it = identity.cbegin(); it != identity.cend(); ++it) fact.insert(it.key(), it.value());
            }
			QVariantList targets;
			for (const ServerPlayer *target : use.to) targets.append(target->objectName());
			fact.insert(QStringLiteral("from"), use.from->objectName());
			fact.insert(QStringLiteral("targets"), targets);
			fact.insert(QStringLiteral("card"), m_room.historyCardSnapshot(card));
			// Preserve the accepted use's origin for first-hand-card history queries.
			fact.insert(QStringLiteral("is_handcard"), use.m_isHandcard);
			// Preserve the accepted accounting key independently of the physical card class.
			fact.insert(QStringLiteral("history_key"), key);
			fact.insert(QStringLiteral("execution_id"), use.skillExecutionID);
            if (use.physicalEquipSource.isValid()) fact.insert(QStringLiteral("physical_equipment"), use.physicalEquipSource.toVariantMap());
			useHistory.update(fact);
			m_room.m_resolutionHistory.appendFact(useHistory.id(), QStringLiteral("use_card"), fact);
		}
		if (use.card->getRealCard() == card){
			if (!use.card->isVirtualCard()){
				WrappedCard*wrapped = Sanguosha->getWrappedCard(ids.first());
				if (wrapped->isModified()) m_room.broadcastUpdateCard(m_room.getPlayers(), ids.first(), wrapped);
				//else broadcastResetCard(getPlayers(), ids.first());
				if (getConvertedPhysicalCardId(use.card) >= 0) {
					ServerPlayer *displayTarget = use.to.size() == 1 && use.to.first() != use.from
						? use.to.first() : nullptr;
					m_room.showVirtualCard(use.from, use.card, displayTarget);
				}
			} else if (use.card->getTypeId() != Card::TypeSkill) {
				ServerPlayer *displayTarget = use.to.size() == 1 && use.to.first() != use.from
					? use.to.first() : nullptr;
				m_room.showVirtualCard(use.from, use.card, displayTarget);
			}
			if (!skipOnUse) {
				cardProcessingStarted = true;
				use.card->onUse(&m_room, use);
			}
		} else {
			use.card = card;
			if (!skipOnUse) {
				cardProcessingStarted = true;
				use.card->onUse(&m_room, use);
			}
		}
	cardProcessingCompleted = cardProcessingStarted;

	// A paid use can stop at the reveal gate. Keep its paid cost/history count,
	// but record the skipped effect rather than reporting successful resolution.
	const bool effectSkipped = skipOnUse || use.skipSkillEffect;
	finishSkillExecution(effectSkipped ? SkillExecutionEffectSkipped : SkillExecutionCompleted);
	if (effectSkipped) useHistory.finish(QStringLiteral("skipped"));
	} catch (const TriggerCascadeBreak &) {
		RoomThread::MandatoryCleanupScope cleanup(*m_room.thread);
		if (use.card) m_room.removeTag(use.card->toString() + "PendingNullification");
		if (skillUsageReserved && !skillUsageCommitted)
			m_room.releaseActiveSkillUsage(activeSkill, skillCardCtx);
		skillUsageReserved = false;
		QList<int> pending;
		for (int id : ids) {
			if (m_room.getCardPlace(id) == Player::PlaceTable) pending << id;
			m_room.setCardFlag(id, "-using");
		}
		if (!pending.isEmpty()) {
			CardMoveReason reason(CardMoveReason::S_REASON_UNKNOWN, use.from->objectName(),
			                     use.card ? use.card->getSkillName() : QString(), "cascade_cancelled");
			m_room.moveCardsAtomic(CardsMoveStruct(pending, use.from, nullptr, Player::PlaceTable,
			                              Player::DiscardPile, reason), true);
		}
		if (use.card) for (ServerPlayer *player : m_room.getAllPlayers()) player->removeQinggangTag(use.card);
		finishSkillExecution(SkillExecutionNoResult);
		throw;
	} catch (TriggerEvent triggerEvent){
		if (use.card)
			m_room.removeTag(use.card->toString() + "PendingNullification");
		if (skillUsageReserved && !skillUsageCommitted)
			m_room.releaseActiveSkillUsage(activeSkill, skillCardCtx);
		skillUsageReserved = false;
		if (triggerEvent == StageChange || triggerEvent == TurnBroken){
			if (cardProcessingStarted && !cardProcessingCompleted) {
				bool processBrokenFlagSet = false;
				try {
					CardMoveReason reason(CardMoveReason::S_REASON_UNKNOWN, use.from->objectName(), use.card->getSkillName(), "");
					if (use.to.size() == 1) reason.m_targetId = use.to.first()->objectName();
					foreach(int id, ids){
						if (m_room.getCardPlace(id) != Player::PlaceTable)
							ids.removeOne(id);
					}
					m_room.moveCardsAtomic(CardsMoveStruct(ids, use.from, nullptr, Player::PlaceTable, Player::DiscardPile, reason), true);
					use.from->setFlags("Global_ProcessBroken");
					processBrokenFlagSet = true;
					if (!use.cardFinished) {
						use.cardFinished = true;
						QVariant data = QVariant::fromValue(use);
						try {
							m_room.thread->trigger(CardFinished, &m_room, use.from, data);
						} catch (TriggerEvent) {
							// Cleanup must not replace the original control event.
						}
					}
					use.from->setFlags("-Global_ProcessBroken");
					processBrokenFlagSet = false;
					foreach(ServerPlayer*p, m_room.getAlivePlayers())
						p->removeQinggangTag(use.card);
					foreach(int id, m_room.m_cardMovement->primaryPile()){
						if (m_room.getCardPlace(id) == Player::PlaceJudge)
							m_room.moveCardTo(Sanguosha->getCard(id), nullptr, Player::DiscardPile, true);
						m_room.setCardFlag(id, "-using");
					}
				} catch (TriggerEvent) {
					if (processBrokenFlagSet)
						use.from->setFlags("-Global_ProcessBroken");
				}
			}
			try {
				finishSkillExecution(SkillExecutionNoResult);
			} catch (TriggerEvent) {
				// The original StageChange/TurnBroken remains authoritative.
			}
		}
		throw triggerEvent;
	}

	if(add_history&&!use.m_addHistory)
		m_room.addPlayerHistory(use.from, key, -1);
	return true;
}

bool CardUseService::cardEffect(const Card*card, ServerPlayer*from, ServerPlayer*to, bool multiple)
{
	CardEffectStruct effect;
	effect.card = card;
	effect.from = from;
	effect.to = to;
	effect.multiple = multiple;
    effect.setSkillUseContext(getUseStruct(card));
	return cardEffect(effect);
}

bool CardUseService::cardEffect(CardEffectStruct&effect)
{
	Room::ResolutionScope resolution(m_room, QStringLiteral("effect"), effect.to, effect.from, effect.to);
	bool cancel = false;
	QVariant data = QVariant::fromValue(effect);
	if (effect.to->isAlive()){ // Be care!!!
		// No skills should be triggered here!
		m_room.thread->trigger(CardEffect, &m_room, effect.to, data);
		effect = data.value<CardEffectStruct>();
		// Make sure that effectiveness of Slash isn't judged here!
		resolution.update(effect.from, effect.to);
		if (m_room.thread->trigger(CardEffected, &m_room, effect.to, data)){
			if (effect.to->hasFlag("Global_NonSkillNullify"))
				effect.to->setFlags("-Global_NonSkillNullify");
			else
				m_room.setEmotion(effect.to, "skill_nullify");
		} else
			cancel = true;
		effect = data.value<CardEffectStruct>();
		effect.to->removeQinggangTag(effect.card);
	}
	m_room.thread->trigger(PostCardEffected, &m_room, effect.to, data);
	return cancel;
}

bool CardUseService::isJinkEffected(ServerPlayer*user, const Card*jink)
{
	//Q_ASSERT(jink->isKindOf("Jink"));
	QVariant jink_data = QVariant::fromValue(jink);
	return !m_room.thread->trigger(JinkEffect, &m_room, user, jink_data);
}

QList<ServerPlayer *> CardUseService::getUseExtraTargets(const CardUseStruct &use,
                                          bool distanceLimit)
{
    QList<ServerPlayer *> result;
    if (!use.from || !use.card) return result;

    const QList<const Player *> others = selectedPlayers(use);
    for (ServerPlayer *candidate : m_room.getAlivePlayers()) {
        if (!candidate || use.to.contains(candidate)) continue;

        if (use.card->isKindOf("Slash")) {
            if (use.from->canSlash(candidate, use.card, distanceLimit, 0, others))
                result << candidate;
            continue;
        }

        if (use.card->isKindOf("Duel")) {
            if (candidate != use.from
                && !use.from->isProhibited(candidate, use.card, others))
                result << candidate;
            continue;
        }

        // Keep the helper useful for future cards, while ordinary cards still
        // retain their own target-count rules through targetFilter().
        if (use.card->targetFilter(others, candidate, use.from)
            && !use.from->isProhibited(candidate, use.card, others))
            result << candidate;
    }
    return result;
}

void CardUseService::cancelTarget(CardUseStruct &use, ServerPlayer *player)
{
    if (!player || !use.card) return;
    LogMessage log;
    log.type = use.from ? "$CancelTarget" : "$CancelTargetNoUser";
    log.from = use.from;
    log.to << player;
    log.arg = use.card->objectName();
    m_room.sendLog(log);
    m_room.setEmotion(player, "cancel");
    use.to.removeOne(player);
    if (use.card->isKindOf("Slash")) {
        player->removeQinggangTag(use.card);
        QStringList bladeUses = player->property("blade_use").toStringList();
        if (bladeUses.removeOne(use.card->toString())) {
            m_room.setPlayerProperty(player, "blade_use", bladeUses);
            if (bladeUses.isEmpty()) m_room.removePlayerDisableShow(player, "Blade");
        }
    }
    // Handlers may add targets (thyingdeng); callers write `use` back to their event data.
    QVariant data = QVariant::fromValue(use);
    m_room.thread->trigger(TargetCanceled, &m_room, player, data);
    use = data.value<CardUseStruct>();
}
