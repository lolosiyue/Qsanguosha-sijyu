#include "roomthread.h"
#include <QScopedValueRollback>
#include "card-lifetime-manager.h"
#include "lua.hpp"
#include "room.h"
#include "engine.h"
#include "gamerule.h"
#include "settings.h"
#include "standard.h"
#include "exppattern.h"
#include "skill-instance-utils.h"
#include "skill-set-generation.h"
#include "v2-record-owner-index.h"
#include "crashhandler.h"
#include "../core/resolution-history.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>

#include <cstdio>
#include <exception>
#include <memory>

#ifdef QSAN_UI_LIBRARY_AVAILABLE
#pragma message WARN("UI elements detected in server side!!!")
#endif

using namespace QSanProtocol;
LogMessage::LogMessage()
	: from(nullptr)
{
}

QVariant LogMessage::toVariant() const
{
	QStringList tos;
	foreach(ServerPlayer*player, to)
		if (player != nullptr) tos << player->objectName();
	return QVariantMap{
		{QStringLiteral("schema_version"), 1},
		{QStringLiteral("log_type"), type},
		{QStringLiteral("from_player"), from ? from->objectName() : QString()},
		{QStringLiteral("to_players"), tos},
		{QStringLiteral("card_string"), card_str},
		{QStringLiteral("arguments"), QStringList{arg, arg2, arg3, arg4, arg5}}
	};
}

DamageStruct::DamageStruct()
	: from(nullptr), to(nullptr), card(nullptr), damage(1), nature(Normal), chain(false),
	transfer(false), by_user(true), prevented(false), ignore_hujia(false)
{
}

DamageStruct::DamageStruct(const Card*card, ServerPlayer*from, ServerPlayer*to, int damage, DamageStruct::Nature nature)
	: from(from), to(to), card(card), damage(damage), nature(nature),
	chain(false), transfer(false), by_user(true), prevented(false), ignore_hujia(false)
{
}

DamageStruct::DamageStruct(const QString &reason, ServerPlayer*from, ServerPlayer*to, int damage, DamageStruct::Nature nature)
	: from(from), to(to), card(nullptr), damage(damage), nature(nature),
	chain(false), transfer(false), by_user(true), reason(reason), prevented(false), ignore_hujia(false)
{
}

DamageStruct::DamageStruct(const DamageStruct &other) { *this = other; }
DamageStruct::DamageStruct(DamageStruct &&other) noexcept { *this = other; }
DamageStruct &DamageStruct::operator=(const DamageStruct &other)
{
	if (this == &other) return *this;
	globalCardLifetimeManager().releaseEventPayload(this);
	from=other.from; to=other.to; card=other.card; damage=other.damage; nature=other.nature;
	chain=other.chain; transfer=other.transfer; by_user=other.by_user; reason=other.reason;
	transfer_reason=other.transfer_reason; prevented=other.prevented; tips=other.tips; ignore_hujia=other.ignore_hujia;
	globalCardLifetimeManager().retainEventPayload(this, {card});
	return *this;
}
DamageStruct &DamageStruct::operator=(DamageStruct &&other) noexcept { return operator=(static_cast<const DamageStruct &>(other)); }
DamageStruct::~DamageStruct() { globalCardLifetimeManager().releaseEventPayload(this); }

QString DamageStruct::getReason() const
{
	if (reason.isEmpty()&&card)
		return card->objectName();
	return reason;
}

CardEffectStruct::CardEffectStruct()
	: card(nullptr), offset_card(nullptr), offset_num(1), from(nullptr), to(nullptr), multiple(false),
	nullified(false), no_respond(false), no_offset(false), extra_effect(0), skillExecutionID(0)
{
}

CardEffectStruct::CardEffectStruct(const CardEffectStruct &other) { *this = other; }
CardEffectStruct::CardEffectStruct(CardEffectStruct &&other) noexcept { *this = other; }
CardEffectStruct &CardEffectStruct::operator=(const CardEffectStruct &other)
{
	if (this == &other) return *this;
	globalCardLifetimeManager().releaseEventPayload(this);
	card=other.card; offset_card=other.offset_card; offset_num=other.offset_num; from=other.from; to=other.to;
	multiple=other.multiple; nullified=other.nullified; no_respond=other.no_respond; no_offset=other.no_offset;
	extra_effect=other.extra_effect; skillExecutionID=other.skillExecutionID;
    m_acceptedSkillEffectCard=other.m_acceptedSkillEffectCard;
    physicalEquipSource=other.physicalEquipSource;
	globalCardLifetimeManager().retainEventPayload(this, {card, offset_card});
	return *this;
}
CardEffectStruct &CardEffectStruct::operator=(CardEffectStruct &&other) noexcept { return operator=(static_cast<const CardEffectStruct &>(other)); }
CardEffectStruct::~CardEffectStruct() { globalCardLifetimeManager().releaseEventPayload(this); }
void CardEffectStruct::setSkillUseContext(const CardUseStruct &use)
{
    skillExecutionID = use.skillExecutionID;
    physicalEquipSource = use.physicalEquipSource;
    m_acceptedSkillEffectCard = use.isAcceptedSkillEffectCard();
}


SlashEffectStruct::SlashEffectStruct()
	: jink_num(1), slash(nullptr), jink(nullptr), from(nullptr), to(nullptr), drank(0), nature(DamageStruct::Normal), multiple(false), nullified(false),
	no_respond(false), no_offset(false)
{
}

SlashEffectStruct::SlashEffectStruct(const SlashEffectStruct &other) { *this = other; }
SlashEffectStruct::SlashEffectStruct(SlashEffectStruct &&other) noexcept { *this = other; }
SlashEffectStruct &SlashEffectStruct::operator=(const SlashEffectStruct &other)
{
	if (this == &other) return *this;
	globalCardLifetimeManager().releaseEventPayload(this);
	jink_num=other.jink_num; slash=other.slash; jink=other.jink; from=other.from; to=other.to; drank=other.drank;
	nature=other.nature; multiple=other.multiple; nullified=other.nullified; no_respond=other.no_respond; no_offset=other.no_offset;
	globalCardLifetimeManager().retainEventPayload(this, {slash, jink});
	return *this;
}
SlashEffectStruct &SlashEffectStruct::operator=(SlashEffectStruct &&other) noexcept { return operator=(static_cast<const SlashEffectStruct &>(other)); }
SlashEffectStruct::~SlashEffectStruct() { globalCardLifetimeManager().releaseEventPayload(this); }

DyingStruct::DyingStruct()
	: who(nullptr), damage(nullptr)
{
}

DeathStruct::DeathStruct()
	: who(nullptr), damage(nullptr)
{
}

RecoverStruct::RecoverStruct(ServerPlayer*who, const Card*card, int recover, const QString &reason)
	: recover(recover), who(who), card(card), reason(reason)
{
}

RecoverStruct::RecoverStruct(const QString &reason, ServerPlayer*who, int recover)
	: recover(recover), who(who), card(nullptr), reason(reason)
{
}

RecoverStruct::RecoverStruct(const RecoverStruct &other) { *this = other; }
RecoverStruct::RecoverStruct(RecoverStruct &&other) noexcept { *this = other; }
RecoverStruct &RecoverStruct::operator=(const RecoverStruct &other)
{
	if (this == &other) return *this;
	globalCardLifetimeManager().releaseEventPayload(this);
	recover=other.recover; who=other.who; card=other.card; reason=other.reason;
	globalCardLifetimeManager().retainEventPayload(this, {card});
	return *this;
}
RecoverStruct &RecoverStruct::operator=(RecoverStruct &&other) noexcept { return operator=(static_cast<const RecoverStruct &>(other)); }
RecoverStruct::~RecoverStruct() { globalCardLifetimeManager().releaseEventPayload(this); }

MarkStruct::MarkStruct()
	: who(nullptr), count(1), gain(-1)
{
}

DrawStruct::DrawStruct()
	: historyEventId(0), who(nullptr), num(1), top(true), visible(false)
{
}

HpLostStruct::HpLostStruct()
	: from(nullptr), to(nullptr), lose(1), ignore_hujia(true)
{
}

HpLostStruct::HpLostStruct(ServerPlayer*to, int lose, const QString &reason, ServerPlayer*from, bool ignore_hujia)
	: from(from), to(to), reason(reason), lose(lose), ignore_hujia(ignore_hujia)
{
}

MaxHpStruct::MaxHpStruct()
	: who(nullptr), change(0)
{
}

MaxHpStruct::MaxHpStruct(ServerPlayer*who, int change, const QString &reason)
	: who(who), change(change), reason(reason)
{
}

PindianStruct::PindianStruct()
	: from(nullptr), to(nullptr), from_card(nullptr), to_card(nullptr), success(false)
{
}

bool PindianStruct::isSuccess() const
{
	return success;
}

JudgeStruct::JudgeStruct()
	: who(nullptr), card(nullptr), pattern("."), good(true), time_consuming(false),
	negative(false), play_animation(true), throw_card(true), retrial_by_response(nullptr),
	_m_result(TRIAL_RESULT_UNKNOWN)
{
}

bool JudgeStruct::isEffected() const
{
	return negative ? isBad() : isGood();
}

void JudgeStruct::updateResult()
{
	if (good == Sanguosha->matchExpPattern(pattern,who,card))
		_m_result = TRIAL_RESULT_GOOD;
	else
		_m_result = TRIAL_RESULT_BAD;
}

bool JudgeStruct::isGood() const
{
	//Q_ASSERT(_m_result != TRIAL_RESULT_UNKNOWN);
	return _m_result == TRIAL_RESULT_GOOD;
}

bool JudgeStruct::isBad() const
{
	return !isGood();
}

bool JudgeStruct::isGood(const Card*card) const
{
	//Q_ASSERT(card);
	return good == Sanguosha->matchExpPattern(pattern,who,card);
}

PhaseChangeStruct::PhaseChangeStruct()
	: from(Player::NotActive), to(Player::NotActive)
{
}

CardUseStruct::CardUseStruct()
	: card(nullptr), from(nullptr), m_isOwnerUse(true), m_addHistory(true), m_isHandcard(false), m_validateTargets(false), whocard(nullptr), who(nullptr), extra_use(0), bypass_cost(false), skipSkillEffect(false), hasSkillActivationRequest(false), skillExecutionID(0)
{
}

CardUseStruct::CardUseStruct(const Card*card, ServerPlayer*from, QList<ServerPlayer*> to, bool isOwnerUse, const Card*whocard, ServerPlayer*who)
	: card(card), from(from), to(to), m_isOwnerUse(isOwnerUse), m_addHistory(true), m_isHandcard(false), m_validateTargets(false), whocard(whocard), who(who), extra_use(0), bypass_cost(false), skipSkillEffect(false), hasSkillActivationRequest(false), skillExecutionID(0)
{
	globalCardLifetimeManager().retainEventPayload(this, {card, whocard});
}

CardUseStruct::CardUseStruct(const Card*card, ServerPlayer*from, ServerPlayer*target, bool isOwnerUse, const Card*whocard, ServerPlayer*who)
	: card(card), from(from), m_isOwnerUse(isOwnerUse), m_addHistory(true), m_isHandcard(false), m_validateTargets(false), whocard(whocard), who(who), extra_use(0), bypass_cost(false), skipSkillEffect(false), hasSkillActivationRequest(false), skillExecutionID(0)
{
	if (target) this->to << target;
	globalCardLifetimeManager().retainEventPayload(this, {card, whocard});
}

CardUseStruct::CardUseStruct(const CardUseStruct &other) { *this = other; }
CardUseStruct::CardUseStruct(CardUseStruct &&other) noexcept { *this = std::move(other); }
CardUseStruct &CardUseStruct::operator=(const CardUseStruct &other)
{
	if (this == &other) return *this;
	globalCardLifetimeManager().releaseEventPayload(this);
	card=other.card; from=other.from; to=other.to; m_isOwnerUse=other.m_isOwnerUse; m_addHistory=other.m_addHistory;
	m_isHandcard=other.m_isHandcard; m_validateTargets=other.m_validateTargets; nullified_list=other.nullified_list;
	whocard=other.whocard; who=other.who; no_respond_list=other.no_respond_list; no_offset_list=other.no_offset_list;
	extra_use=other.extra_use; bypass_cost=other.bypass_cost; skipSkillEffect=other.skipSkillEffect; cardFinished=other.cardFinished;
	hasSkillActivationRequest=other.hasSkillActivationRequest; sourceRef=other.sourceRef; activationRef=other.activationRef;
    m_acceptedSkillEffectCard=other.m_acceptedSkillEffectCard;
    physicalEquipSource=other.physicalEquipSource;
	skillExecutionID=other.skillExecutionID; m_ownedCard=other.m_ownedCard;
	targetModReveal=other.targetModReveal;
	globalCardLifetimeManager().retainEventPayload(this, {card, whocard, m_ownedCard.data()});
	return *this;
}
CardUseStruct &CardUseStruct::operator=(CardUseStruct &&other) noexcept
{
	if (this == &other) return *this;
	globalCardLifetimeManager().releaseEventPayload(this);
	card=other.card; from=other.from; to=std::move(other.to); m_isOwnerUse=other.m_isOwnerUse; m_addHistory=other.m_addHistory;
	m_isHandcard=other.m_isHandcard; m_validateTargets=other.m_validateTargets; nullified_list=std::move(other.nullified_list);
	whocard=other.whocard; who=other.who; no_respond_list=std::move(other.no_respond_list); no_offset_list=std::move(other.no_offset_list);
	extra_use=other.extra_use; bypass_cost=other.bypass_cost; skipSkillEffect=other.skipSkillEffect; cardFinished=other.cardFinished;
	hasSkillActivationRequest=other.hasSkillActivationRequest; sourceRef=other.sourceRef; activationRef=other.activationRef;
    m_acceptedSkillEffectCard=other.m_acceptedSkillEffectCard;
    physicalEquipSource=other.physicalEquipSource;
	skillExecutionID=other.skillExecutionID; m_ownedCard=std::move(other.m_ownedCard);
	targetModReveal=std::move(other.targetModReveal);
	globalCardLifetimeManager().retainEventPayload(this, {card, whocard, m_ownedCard.data()});
	globalCardLifetimeManager().releaseEventPayload(&other);
	other.card = nullptr;
	other.whocard = nullptr;
    other.m_acceptedSkillEffectCard = false;
	return *this;
}
CardUseStruct::~CardUseStruct() { globalCardLifetimeManager().releaseEventPayload(this); }

void CardUseStruct::replaceCard(const Card *newCard, Card *ownedCard)
{
	Q_ASSERT(!ownedCard || ownedCard == newCard);
	CardLifetimeManager &manager = globalCardLifetimeManager();
	CardLifetimeLease replacementLease(manager, manager.liveToken(newCard));
	manager.releaseEventPayload(this);
	card = nullptr;
	if (m_ownedCard.data() != ownedCard) {
		m_ownedCard.clear();
		if (ownedCard)
			m_ownedCard.reset(ownedCard);
	}
	card = newCard;
	manager.retainEventPayload(this, {card, whocard, m_ownedCard.data()});
}

bool CardUseStruct::isValid(const QString &pattern) const
{
	Q_UNUSED(pattern)
	return card != nullptr;
	/*if (card == nullptr) return false;
	if (!card->getSkillName().isEmpty()) {
	bool validSkill = false;
	QString skillName = card->getSkillName();
	QSet<const Skill*> skills = from->getVisibleSkills();
	for (int i = 0; i < 4; i++) {
	const EquipCard*equip = from->getEquip(i);
	if (equip == nullptr) continue;
	const Skill*skill = Sanguosha->getSkill(equip);
	if (skill)
	skills.insert(skill);
	}
	foreach (const Skill*skill, skills) {
	if (skill->objectName() != skillName) continue;
	const ViewAsSkill*vsSkill = ViewAsSkill::parseViewAsSkill(skill);
	if (vsSkill) {
	if (!vsSkill->isAvailable(from, m_reason, pattern))
	return false;
	else {
	validSkill = true;
	break;
	}
	} else if (skill->getFrequency() == Skill::Wake) {
	bool valid = (from->getMark(skill->objectName()) > 0);
	if (!valid)
	return false;
	else
	validSkill = true;
	} else
	return false;
	}
	if (!validSkill) return false;
	}
	if (card->targetFixed())
	return true;
	else {
	QList<const Player*> targets;
	foreach (const ServerPlayer*player, to)
	targets.push_back(player);
	return card->targetsFeasible(targets, from);
	}*/
}

bool CardUseStruct::tryParse(const QVariant &usage, Room*room)
{
    m_acceptedSkillEffectCard = false;
    sourceRef = SkillInstanceRef(); activationRef = SkillInstanceRef(); physicalEquipSource = PhysicalEquipSource(); skillExecutionID = 0;
	JsonArray use = usage.value<JsonArray>();
	replaceCard(nullptr);
	to.clear();
	m_validateTargets = true;
	hasSkillActivationRequest = false;
	if (use.length()>1&&JsonUtils::isString(use[0])&&use[1].canConvert<JsonArray>()){
		foreach(const QVariant &target, use[1].value<JsonArray>()) {
			if (!JsonUtils::isString(target)) return false;
			ServerPlayer *player = room->findChild<ServerPlayer*>(target.toString());
			if (!player) return false;
			to << player;
		}
		const Card *parsedCard = Card::Parse(use[0].toString());
		if (!parsedCard) return false;
		SkillInstanceUtils::SkillActivationRequest request;
		if (!SkillInstanceUtils::decodeActivationRequest(use, parsedCard->getSkillName(false), request)) return false;
		hasSkillActivationRequest = request.supplied;
		if (request.instanceID > 0)
			const_cast<Card *>(parsedCard)->setActivationSkill(request.skillName, request.instanceID);
		CardLifetimeManager &manager = globalCardLifetimeManager();
		const auto token = manager.liveToken(parsedCard);
		replaceCard(parsedCard, token && manager.state(token) == CardLifetimeState::PendingDelete
			? const_cast<Card *>(parsedCard) : nullptr);
		return true;
	}
	return false;
}

void CardUseStruct::parse(const QString &str, Room*room)
{
    m_acceptedSkillEffectCard = false;
    sourceRef = SkillInstanceRef(); activationRef = SkillInstanceRef(); physicalEquipSource = PhysicalEquipSource(); skillExecutionID = 0;
	replaceCard(nullptr);
	to.clear();
	m_validateTargets = true;
	const Card *parsedCard = Card::Parse(str);
	CardLifetimeManager &manager = globalCardLifetimeManager();
	const auto token = manager.liveToken(parsedCard);
	replaceCard(parsedCard, token && manager.state(token) == CardLifetimeState::PendingDelete
		? const_cast<Card *>(parsedCard) : nullptr);
	if (str.contains("->sgs")){
		foreach(QString target_name, str.split("->").last().split("+"))
			to << room->findChild<ServerPlayer*>(target_name);
	}
}

void CardUseStruct::clientReply()
{
	if (from){
		const QVariant &client = from->getRoom()->getTag("AiResult");
		if(client.canConvert<JsonArray>()) tryParse(client, from->getRoom());
		else parse(client.toString(), from->getRoom());
	}
}

void CardUseStruct::changeCard(Card*newcard)
{
	globalCardLifetimeManager().releaseEventPayload(this);
	const bool replacingOwnedCard = !m_ownedCard.isNull() && m_ownedCard.data() == card;
	QVariantMap tag = newcard->tag;
	for (auto it = card->tag.cbegin(); it != card->tag.cend(); ++it)
		tag.insert(it.key(), it.value());
	newcard->tag = tag;
	newcard->setFlags(newcard->getFlags()+card->getFlags());
	if (activationRef.isValid())
		newcard->setActivationSkill(activationRef.key.skillName, activationRef.key.instanceID);
	if (sourceRef.isValid())
		newcard->setSourceSkill(sourceRef.key.skillName, sourceRef.key.instanceID);
	if (!replacingOwnedCard)
		newcard->change_cards << card;
	replaceCard(newcard);
	if (from) {
		Room *room = from->getRoom();
		const QVariantMap use = room->historyParent(room->currentHistoryEventId(), "use_card", true);
		if (room->historyRecordingEnabled() && use.value("data").toMap().value("from").toString() == from->objectName()) {
			// Conversions such as Fan keep the same use event. Its resolved card
			// changes, while the accepted-use fact remains an immutable snapshot.
			room->resolutionHistory().updateEvent(use.value("id").toLongLong(),
				{{"card", room->historyCardSnapshot(newcard)}});
		}
	}
}

void CardUseStruct::setOwnedCard(Card *ownedCard)
{
	replaceCard(ownedCard, ownedCard);
}

CardResponseStruct::CardResponseStruct()
	: m_card(nullptr), m_who(nullptr), m_isUse(false), m_isHandcard(false), m_isRetrial(false),
	  m_toCard(nullptr), skillExecutionID(0), nullified(false)
{
}

CardResponseStruct::CardResponseStruct(const Card *card, bool isUse)
	: m_card(card), m_who(nullptr), m_isUse(isUse), m_isHandcard(false), m_isRetrial(false),
	  m_toCard(nullptr), skillExecutionID(0), nullified(false)
{
	globalCardLifetimeManager().retainEventPayload(this, {m_card});
}

CardResponseStruct::CardResponseStruct(const Card *card, ServerPlayer *who, bool isUse)
	: m_card(card), m_who(who), m_isUse(isUse), m_isHandcard(false), m_isRetrial(false),
	  m_toCard(nullptr), skillExecutionID(0), nullified(false)
{
	globalCardLifetimeManager().retainEventPayload(this, {m_card});
}

CardResponseStruct::CardResponseStruct(const CardResponseStruct &other) { *this = other; }
CardResponseStruct::CardResponseStruct(CardResponseStruct &&other) noexcept { *this = std::move(other); }
CardResponseStruct &CardResponseStruct::operator=(const CardResponseStruct &other)
{
	if (this == &other) return *this;
	globalCardLifetimeManager().releaseEventPayload(this);
	m_card=other.m_card; m_who=other.m_who; m_isUse=other.m_isUse; m_isHandcard=other.m_isHandcard;
	m_isRetrial=other.m_isRetrial; m_toCard=other.m_toCard; sourceRef=other.sourceRef; physicalEquipSource=other.physicalEquipSource;
	activationRef=other.activationRef; skillExecutionID=other.skillExecutionID; nullified=other.nullified;
	globalCardLifetimeManager().retainEventPayload(this, {m_card, m_toCard});
	return *this;
}
CardResponseStruct &CardResponseStruct::operator=(CardResponseStruct &&other) noexcept
{
	if (this == &other) return *this;
	globalCardLifetimeManager().releaseEventPayload(this);
	m_card=other.m_card; m_who=other.m_who; m_isUse=other.m_isUse; m_isHandcard=other.m_isHandcard;
	m_isRetrial=other.m_isRetrial; m_toCard=other.m_toCard; sourceRef=other.sourceRef; physicalEquipSource=other.physicalEquipSource;
	activationRef=other.activationRef; skillExecutionID=other.skillExecutionID; nullified=other.nullified;
	globalCardLifetimeManager().retainEventPayload(this, {m_card, m_toCard});
	globalCardLifetimeManager().releaseEventPayload(&other);
	other.m_card = nullptr;
	other.m_toCard = nullptr;
	return *this;
}
CardResponseStruct::~CardResponseStruct() { globalCardLifetimeManager().releaseEventPayload(this); }

void CardResponseStruct::changeCard(Card*newcard)
{
	globalCardLifetimeManager().releaseEventPayload(this);
	QVariantMap tag = newcard->tag;
	for (auto it = m_card->tag.cbegin(); it != m_card->tag.cend(); ++it)
		tag.insert(it.key(), it.value());
	newcard->tag = tag;
	newcard->setFlags(newcard->getFlags()+m_card->getFlags());
	if (activationRef.isValid())
		newcard->setActivationSkill(activationRef.key.skillName, activationRef.key.instanceID);
	if (sourceRef.isValid())
		newcard->setSourceSkill(sourceRef.key.skillName, sourceRef.key.instanceID);
	newcard->change_cards << m_card;
	m_card = newcard;
	globalCardLifetimeManager().retainEventPayload(this, {m_card, m_toCard});
}

QString EventTriplet::toString() const
{
	return QString("event[%1], room[%2], target = %3[%4]\n")
		.arg(_m_event)
		.arg(_m_room->getId())
		.arg(_m_target ? _m_target->objectName() : "nullptr")
		.arg(_m_target ? _m_target->getGeneralName() : "");
}

RoomThread::RoomThread(Room*room)
	: room(room),
	  m_perfTraceEnabled(Config.value("RoomThreadPerfTrace", false).toBool()),
	  m_profileRoomId(room ? room->getId() : -1),
	  m_profileMode(room ? room->getMode() : QString())
{
	// The shutdown check must be able to say which thread still holds a card-lifetime scope,
	// hence the name.
	setObjectName(QStringLiteral("RoomThread(room %1)").arg(m_profileRoomId));
	if (m_perfTraceEnabled) {
		connect(this, &QThread::finished, this,
			&RoomThread::emitPerfTrace, Qt::DirectConnection);
	}
}

void RoomThread::emitPerfTrace() const
{
	QJsonObject payload;
	payload.insert(QStringLiteral("room_id"), m_profileRoomId);
	payload.insert(QStringLiteral("mode"), m_profileMode);
	payload.insert(QStringLiteral("trigger_dispatch"),
		QJsonObject::fromVariantMap(triggerDispatchProfile()));

	const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
	std::fprintf(stdout, "ROOMTHREAD_PERF %s\n", json.constData());
	std::fflush(stdout);
}

QVariantMap RoomThread::triggerDispatchProfile() const
{
	return QVariantMap{
		{QStringLiteral("trigger_count"), qint64(m_triggerDispatchProfile.triggerCount)},
		{QStringLiteral("priority_rebuild_count"), qint64(m_triggerDispatchProfile.priorityRebuildCount)},
		{QStringLiteral("priority_skill_count"), qint64(m_triggerDispatchProfile.prioritySkillCount)},
		{QStringLiteral("priority_sort_count"), qint64(m_triggerDispatchProfile.prioritySortCount)},
		{QStringLiteral("v2_dispatch_count"), qint64(m_triggerDispatchProfile.v2DispatchCount)},
		{QStringLiteral("v2_empty_dispatch_count"), qint64(m_triggerDispatchProfile.v2EmptyDispatchCount)},
		{QStringLiteral("v2_candidate_count"), qint64(m_triggerDispatchProfile.v2CandidateCount)},
		{QStringLiteral("main_table_candidate_visit_count"),
		 qint64(m_triggerDispatchProfile.mainTableCandidateVisitCount)}
	};
}

void RoomThread::markDistanceCacheDirty()
{
	m_distanceCacheDirty = true;
}

const QByteArray &RoomThread::distancePropertyName(const ServerPlayer *player)
{
	auto it = m_distancePropertyNames.constFind(player);
	if (it == m_distancePropertyNames.constEnd()) {
		m_distancePropertyNames.insert(player,
			QByteArrayLiteral("distanceTo_") + player->objectName().toLatin1());
		it = m_distancePropertyNames.constFind(player);
	}
	return it.value();
}

void RoomThread::addPlayerSkills(ServerPlayer*player, bool invoke_game_start)
{
	bool invoke_verify = false;

	foreach (const TriggerSkill*skill, player->getTriggerSkills()) {
		addTriggerSkill(skill);

		if (invoke_game_start && skill->hasEvent(GameReady))
			invoke_verify = true;
	}

	//We should make someone trigger a whole GameReady event instead of trigger a skill only.
	if (invoke_verify)
		trigger(GameReady, room, player);
}

void RoomThread::constructTriggerTable()
{
	foreach(ServerPlayer*player, room->getPlayers())
		addPlayerSkills(player, true);
}

ServerPlayer*RoomThread::find3v3Next(QList<ServerPlayer*> &first, QList<ServerPlayer*> &second)
{
	bool all_actioned = true;
	foreach (ServerPlayer*player, room->getAlivePlayers()) {
		if (!player->hasFlag("actioned")) {
			all_actioned = false;
			break;
		}
	}

	if (all_actioned) {
		foreach (ServerPlayer*player, room->getAlivePlayers()) {
			room->setPlayerFlag(player, "-actioned");
			trigger(ActionedReset, room, player);
		}

		qSwap(first, second);
		QList<ServerPlayer*> first_alive;
		foreach (ServerPlayer*p, first) {
			if (p->isAlive())
				first_alive << p;
		}
		return room->askForPlayerChosen(first.first(), first_alive, "3v3-action", "@3v3-action");
	}

	ServerPlayer*current = room->getCurrent();
	if (current != first.first()) {
		ServerPlayer*another = nullptr;
		if (current == first.last())
			another = first.at(1);
		else
			another = first.last();
		if (!another->hasFlag("actioned") && another->isAlive())
			return another;
	}

	QList<ServerPlayer*> targets;
	do {
		targets.clear();
		qSwap(first, second);
		foreach (ServerPlayer*player, first) {
			if (!player->hasFlag("actioned") && player->isAlive())
				targets << player;
		}
	} while (targets.isEmpty());

	return room->askForPlayerChosen(first.first(), targets, "3v3-action", "@3v3-action");
}

void RoomThread::run3v3(QList<ServerPlayer*> &first, QList<ServerPlayer*> &second, GameRule*game_rule, ServerPlayer*current)
{
	try {
		forever{
			room->setCurrent(current);
			trigger(TurnStart, room, current);
			room->setPlayerFlag(current, "actioned");
			current = find3v3Next(first, second);
		}
	}catch (TriggerEvent triggerEvent) {
		if (triggerEvent == TurnBroken)
			_handleTurnBroken3v3(first, second, game_rule);
		else
			throw triggerEvent;
	}
}

void RoomThread::_handleTurnBroken3v3(QList<ServerPlayer*> &first, QList<ServerPlayer*> &second, GameRule*game_rule)
{
	try {
		ServerPlayer*player = room->getCurrent();
		{
			const qint64 cleanupEvent = interruptedPhase() != 0 ? interruptedPhase() : interruptedTurn();
			ResolutionHistoryContextGuard context(room->resolutionHistory(), cleanupEvent,
				room->historyRecordingEnabled() && cleanupEvent != 0);
			trigger(TurnBroken, room, player);
			if (player->getPhase() != Player::NotActive) {
				game_rule->trigger(EventPhaseEnd, room, player);
				player->changePhase(player->getPhase(), Player::NotActive);
			}
			if (!player->hasFlag("actioned"))
				room->setPlayerFlag(player, "actioned");

			reclaimCompletedTurn();
		}
		clearInterruptedTurn();
		clearInterruptedPhase();
		ServerPlayer*next = find3v3Next(first, second);
		run3v3(first, second, game_rule, next);
	}catch (TriggerEvent triggerEvent) {
		if (triggerEvent == TurnBroken)
			_handleTurnBroken3v3(first, second, game_rule);
		else
			throw triggerEvent;
	}
}

ServerPlayer*RoomThread::findHulaoPassNext(ServerPlayer*shenlvbu, QList<ServerPlayer*> league, int stage)
{
	ServerPlayer*current = room->getCurrent();
	if (stage == 1) {
		if (current == shenlvbu) {
			foreach (ServerPlayer*p, league) {
				if (p->isAlive() && !p->hasFlag("actioned"))
					return p;
			}
			foreach (ServerPlayer*p, league) {
				if (p->isAlive())
					return p;
			}
			Q_ASSERT(false);
			return league.first();
		} else {
			return shenlvbu;
		}
	} else {
		Q_ASSERT(stage == 2);
		return current->getNextGamePlayer();
	}
}

void RoomThread::actionHulaoPass(ServerPlayer*shenlvbu, QList<ServerPlayer*> league, GameRule*game_rule, int stage)
{
	try {
		if (stage == 1) {
			forever{
				ServerPlayer*current = room->getCurrent();
				trigger(TurnStart, room, current);

				ServerPlayer*next = findHulaoPassNext(shenlvbu, league, 1);
				if (current != shenlvbu) {
					if (current->isAlive() && !current->hasFlag("actioned"))
						room->setPlayerFlag(current, "actioned");
				} else {
					bool all_actioned = true;
					foreach (ServerPlayer*player, league) {
						if (player->isAlive() && !player->hasFlag("actioned")) {
							all_actioned = false;
							break;
						}
					}
					if (all_actioned) {
						foreach (ServerPlayer*player, league) {
							if (player->hasFlag("actioned"))
								room->setPlayerFlag(player, "-actioned");
						}
						foreach (ServerPlayer*player, league) {
							if (player->isDead())
								trigger(TurnStart, room, player);
						}
					}
				}

				room->setCurrent(next);
			}
		} else {
			Q_ASSERT(stage == 2);
			forever{
				ServerPlayer*current = room->getCurrent();
				trigger(TurnStart, room, current);

				ServerPlayer*next = findHulaoPassNext(shenlvbu, league, 2);

				if (current == shenlvbu) {
					foreach (ServerPlayer*player, league) {
						if (player->isDead())
							trigger(TurnStart, room, player);
					}
				}
				room->setCurrent(next);
			}
		}
	}catch (TriggerEvent triggerEvent) {
		if (triggerEvent == StageChange) {
			stage = 2;
			trigger(triggerEvent, room, nullptr);
			foreach (ServerPlayer*player, room->getPlayers()) {
				if (player != shenlvbu) {
					if (player->hasFlag("actioned"))
						room->setPlayerFlag(player, "-actioned");

					if (player->getPhase() != Player::NotActive) {
						game_rule->trigger(EventPhaseEnd, room, player);
						player->changePhase(player->getPhase(), Player::NotActive);
					}
				}
			}

			reclaimCompletedTurn();
			room->setCurrent(shenlvbu);
			actionHulaoPass(shenlvbu, league, game_rule, 2);
		} else if (triggerEvent == TurnBroken) {
			_handleTurnBrokenHulaoPass(shenlvbu, league, game_rule, stage);
		} else
			throw triggerEvent;
	}
}

void RoomThread::_handleTurnBrokenHulaoPass(ServerPlayer*shenlvbu, QList<ServerPlayer*> league, GameRule*game_rule, int stage)
{
	try {
		ServerPlayer*player = room->getCurrent();
		ServerPlayer *next = nullptr;
		{
			const qint64 cleanupEvent = interruptedPhase() != 0 ? interruptedPhase() : interruptedTurn();
			ResolutionHistoryContextGuard context(room->resolutionHistory(), cleanupEvent,
				room->historyRecordingEnabled() && cleanupEvent != 0);
			trigger(TurnBroken, room, player);
			next = findHulaoPassNext(shenlvbu, league, stage);
			if (player->getPhase() != Player::NotActive) {
				game_rule->trigger(EventPhaseEnd, room, player);
				player->changePhase(player->getPhase(), Player::NotActive);
				if (player != shenlvbu && stage == 1)
					room->setPlayerFlag(player, "actioned");
			}
			reclaimCompletedTurn();
		}
		clearInterruptedTurn();
		clearInterruptedPhase();
		room->setCurrent(next);
		actionHulaoPass(shenlvbu, league, game_rule, stage);
	}catch (TriggerEvent triggerEvent) {
		if (triggerEvent == TurnBroken)
			_handleTurnBrokenHulaoPass(shenlvbu, league, game_rule, stage);
		else
			throw triggerEvent;
	}
}

void RoomThread::actionNormal(GameRule*game_rule)
{
	try {
		forever{
			// This is the sole resumable boundary: capture synchronously while
			// the previous turn is fully quiescent, immediately before the
			// complete top-level TurnStart dispatch. Extra turns enter TurnStart
			// from GameRule and therefore never pass through this hook.
			room->saveSnapshot("turn");
			trigger(TurnStart, room, room->getCurrent());
			if (room->isFinished()) break;
			room->setCurrent(room->getCurrent()->getNextGamePlayer());
		}
	}catch (TriggerEvent triggerEvent) {
		if (triggerEvent == TurnBroken)
			_handleTurnBrokenNormal(game_rule);
		else
			throw triggerEvent;
	}
}

void RoomThread::_handleTurnBrokenNormal(GameRule*game_rule)
{
	try {
		ServerPlayer*player = room->getCurrent();
		ServerPlayer *next = nullptr;
		{
			const qint64 cleanupEvent = interruptedPhase() != 0 ? interruptedPhase() : interruptedTurn();
			ResolutionHistoryContextGuard context(room->resolutionHistory(), cleanupEvent,
				room->historyRecordingEnabled() && cleanupEvent != 0);
			trigger(TurnBroken, room, player);
			next = player->getNextGamePlayer();
			if (player->getPhase() != Player::NotActive) {
				game_rule->trigger(EventPhaseEnd, room, player);
				player->changePhase(player->getPhase(), Player::NotActive);
			}
			reclaimCompletedTurn();
		}
		clearInterruptedTurn();
		clearInterruptedPhase();
		room->setCurrent(next);
		actionNormal(game_rule);
	}catch (TriggerEvent triggerEvent) {
		if (triggerEvent == TurnBroken)
			_handleTurnBrokenNormal(game_rule);
		else
			throw triggerEvent;
	}
}

void RoomThread::run()
{
	LuaRuntime::Binding luaBinding(room->roomRuntime()->lua());
	GameRng::Binding rngBinding(room->roomRuntime()->rng());
	Sanguosha->registerRoom(room);
	CrashHandler::setLuaState(room->getLuaState());
	auto runtimeCleanup = qScopeGuard([]() {
		CrashHandler::setLuaState(nullptr);
		Sanguosha->unregisterRoom();
	});
	bool turnReclamationRegistered = false;
	auto turnReclaimCleanup = qScopeGuard([this, &turnReclamationRegistered]() {
		if (turnReclamationRegistered)
			globalCardLifetimeManager().endTurnReclamation(room->roomRuntime());
	});
	auto workerFinal = qScopeGuard([this]() {
		// finalizeWorker() closes the room's Lua states, so the crash handler
		// has to drop its lua_State before that and not after.
		CrashHandler::setLuaState(nullptr);
		room->roomRuntime()->finalizeWorker();
	});
	// Keep this domain out of opportunistic/global drains until worker-final
	// cleanup has completed. Only this registered worker may drain at turn end.
	turnReclamationRegistered = globalCardLifetimeManager().beginTurnReclamation(room->roomRuntime());
	if (!turnReclamationRegistered) {
		qWarning("Cannot register the Room worker for turn-end Card reclamation");
		return;
	}
	if (room->getPlayers().size() > 20 && room->getLuaState()) {
		// Large rooms create many short-lived Lua argument wrappers. Collect them
		// during play instead of leaving a long finalizer backlog for lua_close.
		// Switch at the idle worker boundary under the normal Lua lifetime pin.
		LuaRuntime::LuaInvocationScope invocation(room->roomRuntime()->lua());
		lua_gc(room->getLuaState(), LUA_GCGEN, 0, 0);
	}

	foreach(const TriggerSkill*triggerSkill, Sanguosha->getGlobalTriggerSkills())
		addTriggerSkill(triggerSkill);

	static QList<const EquipCard*> equips = Sanguosha->findChildren<const EquipCard*>();
	foreach (const EquipCard*e, equips) {
        // The card keeps its canonical name; imported equipment owns an heg_
        // definition. Register records before installation/removal can occur.
        const Skill *equipmentSkill = Sanguosha->getSkill(e);
        addTriggerSkill(qobject_cast<const TriggerSkill *>(equipmentSkill));
        if (equipmentSkill && Config.EnableHegemony
            && equipmentSkill->objectName().startsWith(QLatin1String("heg_"))) {
            // A ViewAsSkill root (for example WoodenOx) may have trigger-only
            // companions even though the root itself is not a TriggerSkill.
            for (const Skill *related : Sanguosha->getRelatedSkills(equipmentSkill->objectName()))
                addTriggerSkill(qobject_cast<const TriggerSkill *>(related));
        }
    }

	GameRule *game_rule = Config.EnableHegemony ? new HegemonyRule(this)
		: (room->getMode() == "04_1v3" ? new HulaoPassMode(this) : new GameRule(this));
	addTriggerSkill(game_rule);

	// start game
	try {
		QList<ServerPlayer*> warm, cool, first, second;
		if (room->getMode() == "06_3v3") {
			foreach (ServerPlayer*player, room->getPlayers()) {
				switch (player->getRoleEnum()) {
				case Player::Lord: warm.prepend(player); break;
				case Player::Loyalist: warm.append(player); break;
				case Player::Renegade: cool.prepend(player); break;
				case Player::Rebel: cool.append(player); break;
				}
			}
			if (room->askForOrder(cool.first(), "cool") == "warm") {
				second = cool;
				first = warm;
			} else {
				first = cool;
				second = warm;
			}
		}
		room->removeDerivativeCards();
		room->beginNumericStateHistory();
		constructTriggerTable();
		trigger(GameReady, room, nullptr);
		room->markGameReadyCompleted();
		if (room->getMode() == "06_3v3") {
			run3v3(first, second, game_rule, first.first());
		} else if (room->getMode() == "04_1v3") {
			ServerPlayer*shenlvbu = room->getLord();
			QList<ServerPlayer*> league = room->getAlivePlayers();
			league.removeOne(shenlvbu);
			room->setCurrent(league.first());
			actionHulaoPass(shenlvbu, league, game_rule, 1);
		} else {
			if (room->getMode() == "02_1v1") {
				ServerPlayer*first = room->getAlivePlayers().first();
				trigger(Debut, room, first);
				trigger(Debut, room, first->getNext());
				room->setCurrent(first);
			}
			actionNormal(game_rule);
		}
	}catch (TriggerEvent triggerEvent) {
		if (triggerEvent == GameFinished) {
			return;
		} else if (triggerEvent == TurnBroken || triggerEvent == StageChange) { // caused in Debut trigger
			ServerPlayer*first = room->getAlivePlayers().first();
			if (first->getRole() != "renegade")
				first = room->getAlivePlayers().at(1);
			room->setCurrent(first);
			actionNormal(game_rule);
		} else
			Q_ASSERT(false);
	}
}

const QList<EventTriplet>*RoomThread::getEventStack() const
{
	return &event_stack;
}

void RoomThread::sortTriggerSkills(TriggerEvent triggerEvent, Room *targetRoom, bool includeLose)
{
	QList<TriggerSkill *> &skills = skill_table[triggerEvent];
	if (skills.length() < 2)
		return;

	const QList<ServerPlayer *> players = targetRoom->getAllPlayers(true);
	// Cache ownership only. Validity (marks, flags, invalidity skills) remains a
	// live hasSkill() query, and the existing generation tracks instance changes.
	static thread_local QPointer<Room> ownerRoom;
	static thread_local quint64 ownerGeneration = 0;
	static thread_local QList<ServerPlayer *> ownerRoster;
	static thread_local QHash<QString, QSet<ServerPlayer *>> possibleOwners;
	const quint64 generation = SkillSet::generation();
	if (ownerRoom != targetRoom || ownerGeneration != generation || ownerRoster != players) {
		possibleOwners.clear();
		foreach (ServerPlayer *player, players) {
			foreach (const QString &name, player->getSkillNames()) {
				possibleOwners[name].insert(player);
				possibleOwners[SkillInstanceUtils::baseName(name)].insert(player);
			}
		}
		ownerRoom = targetRoom;
		ownerGeneration = generation;
		ownerRoster = players;
	}
	// A nested Room dispatch must not replace this invocation's ownership view.
	const auto owners = possibleOwners;
	QHash<const TriggerSkill *, double> priorities;
	priorities.reserve(skills.length());
	foreach (TriggerSkill *skill, skills) {
		double len = players.length();
		double priority = skill->getPriority(triggerEvent);
		const QString skillName = skill->objectName();
		const auto candidates = owners.value(skillName);
		foreach (ServerPlayer *player, players) {
			// A callback may acquire a skill while priorities are being queried.
			if ((SkillSet::generation() != generation || candidates.contains(player))
				&& player->hasSkill(skillName, includeLose)) {
				priority += len / 100.0;
				break;
			}
			--len;
		}
		priorities.insert(skill, priority);
	}

	const auto compareByPriority = [this, &priorities](TriggerSkill *a, TriggerSkill *b) {
		const double aPriority = priorities.value(a);
		const double bPriority = priorities.value(b);
		if (aPriority != bPriority)
			return aPriority > bPriority;

		const bool aEquipOrRule = m_triggerSkillTraits.value(a).equipOrRule;
		const bool bEquipOrRule = m_triggerSkillTraits.value(b).equipOrRule;
		return !aEquipOrRule && bEquipOrRule;
	};

	std::stable_sort(skills.begin(), skills.end(), compareByPriority);
	++m_triggerTableRevision[triggerEvent];
	if (v2_skill_table[triggerEvent].length() > 1) {
		std::stable_sort(v2_skill_table[triggerEvent].begin(),
			v2_skill_table[triggerEvent].end(), compareByPriority);
	}

	if (m_perfTraceEnabled) {
		++m_triggerDispatchProfile.priorityRebuildCount;
		m_triggerDispatchProfile.prioritySkillCount += skills.length();
		++m_triggerDispatchProfile.prioritySortCount;
		if (v2_skill_table[triggerEvent].length() > 1)
			++m_triggerDispatchProfile.prioritySortCount;
	}
}

static QStringList mergeSkillNames(const QStringList &names)
{
	QStringList result;
	QMap<QString, int> count;
	foreach (const QString &name, names) {
		QString skillName = name;
		int multiplier = 1;
		int split = -1;
		if ((split = name.indexOf('*')) != -1) {
			skillName = name.left(split);
			multiplier = name.mid(split + 1).toInt();
		}
		count[skillName] += multiplier;
	}
	QMap<QString, int>::iterator it;
	for (it = count.begin(); it != count.end(); ++it) {
		if (it.value() > 1)
			result << QString("%1*%2").arg(it.key()).arg(it.value());
		else
			result << it.key();
	}
	return result;
}

static bool usesV2EventPriority(const TriggerSkill *skill)
{
    const auto *v2 = dynamic_cast<const TriggerSkillV2 *>(skill);
    return v2 && (v2->isEquipSkill() || v2->usesEventPriority());
}

static QString skillInstanceRuntimeKey(const ServerPlayer *owner, const QString &skillName, int instanceId)
{
	return QString("%1|%2#%3").arg(owner ? owner->objectName() : QString(), skillName)
		.arg(instanceId);
}

bool RoomThread::triggerSkillSources(TriggerEvent event, Room *room, ServerPlayer *target,
    QVariant &data, const QList<SkillInstanceRef> &sources)
{
    if (!room || sources.isEmpty()) return false;
    QList<TriggerSkill *> selected;
    for (TriggerSkill *skill : v2_skill_table[event]) {
        for (const SkillInstanceRef &ref : sources) {
            if (skill->objectName() == ref.key.skillName
                || Sanguosha->getMainSkill(skill->objectName()) == Sanguosha->getSkill(ref.key.skillName)) {
                if (!selected.contains(skill)) selected << skill;
                break;
            }
        }
    }
    // Keep nested callbacks inside this initialization event until it unwinds.
    event_stack << EventTriplet(event, room, target);
    bool broken = false;
    try {
        broken = triggerV2Skills(event, room, target, data, &selected, &sources);
    } catch (...) {
        event_stack.removeLast();
        throw;
    }
    event_stack.removeLast();
    flushOutermostDeferredWork(room);
    return broken;
}

namespace {
// Restore between author callbacks, so the next interceptor observes admission
// identity while retaining mutations to cancellation, targets and effect values.
void restorePhysicalEquipmentIdentity(QVariant &data, const QVariant &physicalIdentity)
{
    if (physicalIdentity.canConvert<SkillContext>() && data.canConvert<SkillContext>()) {
        SkillContext updated = data.value<SkillContext>();
        updated.physicalEquipSource = physicalIdentity.value<SkillContext>().physicalEquipSource;
        data = QVariant::fromValue(updated);
    } else if (physicalIdentity.canConvert<CardUseStruct>() && data.canConvert<CardUseStruct>()) {
        CardUseStruct updated = data.value<CardUseStruct>();
        updated.physicalEquipSource = physicalIdentity.value<CardUseStruct>().physicalEquipSource;
        data = QVariant::fromValue(updated);
    } else if (physicalIdentity.canConvert<CardEffectStruct>() && data.canConvert<CardEffectStruct>()) {
        CardEffectStruct updated = data.value<CardEffectStruct>();
        updated.physicalEquipSource = physicalIdentity.value<CardEffectStruct>().physicalEquipSource;
        data = QVariant::fromValue(updated);
    } else if (physicalIdentity.canConvert<CardResponseStruct>() && data.canConvert<CardResponseStruct>()) {
        CardResponseStruct updated = data.value<CardResponseStruct>();
        updated.physicalEquipSource = physicalIdentity.value<CardResponseStruct>().physicalEquipSource;
        data = QVariant::fromValue(updated);
    }
}
}

bool RoomThread::triggerV2Skills(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data,
                                const QList<TriggerSkill *> *equipmentGroup,
                                const QList<SkillInstanceRef> *allowedSources)
{
    const QVariant physicalIdentity = data;
    const auto restoreIdentity = [&] { restorePhysicalEquipmentIdentity(data, physicalIdentity); };
    const auto physicalGuard = qScopeGuard(restoreIdentity);
	QList<TriggerSkill *> v2_skills;
	if (equipmentGroup) {
		v2_skills = *equipmentGroup;
	} else {
		// Equipment keeps its position relative to legacy rules (notably BuryVictim).
		for (TriggerSkill *skill : v2_skill_table[triggerEvent])
			if (!usesV2EventPriority(skill)) v2_skills << skill;
	}
	if (m_perfTraceEnabled) {
		++m_triggerDispatchProfile.v2DispatchCount;
		m_triggerDispatchProfile.v2CandidateCount += v2_skills.length();
	}
	if (v2_skills.isEmpty()) {
		if (m_perfTraceEnabled)
			++m_triggerDispatchProfile.v2EmptyDispatchCount;
		return false;
	}

	QMap<QString, int> triggerCounts;
	QMap<QString, int> maxMultipliers;
	QSet<QString> triggeredSkills;
	QSet<ServerPlayer *> declinedOwners;
	QMap<QString, QStringList> consumedEquipmentTargets;
	QMap<QString, QStringList> consumedContextTargets;

	// Run once per recorded event and provide full context for each current player instance.
	foreach (const TriggerSkill *ts, v2_skills) {
		TriggerSkillV2 *v2 = const_cast<TriggerSkillV2 *>(qobject_cast<const TriggerSkillV2 *>(ts));
		if (!v2) continue;
        const bool recorded = v2->recordEvent(triggerEvent, room, target, data);
        restoreIdentity();
        if (recorded) continue;
		if (v2->isEquipSkill()) {
			// Card cleanup must run even after onUninstall has detached the skill.
			// This is one event record, not a synthetic Player skill instance.
			SkillContext recordCtx;
			recordCtx.skill_name = v2->objectName();
			recordCtx.owner = target;
			recordCtx.invoker = target;
			recordCtx.original_data = &data;
			recordCtx.current_event = triggerEvent;
			v2->record(triggerEvent, room, target, recordCtx);
            restoreIdentity();
			continue;
		}
        const auto recordPlayers = room->getAllPlayers(true);
        static thread_local V2RecordOwnerIndex recordOwners;
        quint64 recordGeneration;
        const QString recordName = v2->objectName();
        const auto candidates = recordOwners.candidates(room, recordPlayers, recordName, recordGeneration);
        if (candidates.isEmpty()) continue;
        foreach (ServerPlayer *owner, recordPlayers) {
            // Records may attach/detach instances, including on later owners.
            // After any mutation resume the original live scan for this record.
            const bool indexed = SkillSet::generation() == recordGeneration && v2->objectName() == recordName;
            if (indexed && !candidates.contains(owner)) continue;
            const QList<int> instanceIds = indexed ? candidates.value(owner)
                : owner->getSkillInstanceIds(v2->objectName());
			foreach (int instanceId, instanceIds) {
				SkillContext recordCtx;
				recordCtx.skill_name = v2->objectName();
				recordCtx.owner = owner;
				recordCtx.invoker = target;
				recordCtx.instanceID = instanceId;
				recordCtx.activationRef = SkillInstanceRef(
					owner->objectName(), SkillInstanceKey(v2->objectName(), instanceId));
                if (room->isAcceptedViewAsEffect(recordCtx.activationRef)) continue;
                recordCtx.sourceRef = room->resolveSkillInstanceRootRef(recordCtx.activationRef);
                if (!recordCtx.sourceRef.isValid()) continue;
                if (allowedSources) {
                    const SkillInstanceRef root = SkillInstanceUtils::resolveRootRef(recordCtx.activationRef,
                        [room](const SkillInstanceRef &source) -> const SkillInstance * {
                            const ServerPlayer *sourceOwner = room->findPlayerByObjectName(source.ownerObjectName, true);
                            return sourceOwner ? sourceOwner->findSkillInstance(source.key.skillName, source.key.instanceID) : nullptr;
                        }, false);
                    if (!allowedSources->contains(recordCtx.activationRef) && !allowedSources->contains(root)) continue;
                }
				bool amountOk = false;
				recordCtx.amount = room->getSkillInstanceAmount(recordCtx.activationRef, &amountOk);
				if (!amountOk) recordCtx.amount = v2->getBaseAmount();
				recordCtx.original_data = &data;
				recordCtx.current_event = triggerEvent;
				v2->record(triggerEvent, room, target, recordCtx);
                restoreIdentity();
			}
		}
	}

	bool broken = false;

	while (!broken) {
		if (equipmentGroup && (triggerEvent == EnterDying || triggerEvent == Dying
			|| triggerEvent == AskForPeaches)) {
			const ServerPlayer *dying = data.value<DyingStruct>().who;
			if (!dying || !dying->hasFlag("Global_Dying")) break;
		}
		QList<SkillContext> skillContexts;
		QMap<QString, QStringList> equipmentTargetPrefixes;
		QMap<QString, QStringList> contextTargetPrefixes;
		QSet<const TriggerSkillV2 *> contextSelectors;
		bool has_compulsory = false;

		foreach (const TriggerSkill *ts, v2_skills) {
			TriggerSkillV2 *v2 = const_cast<TriggerSkillV2 *>(qobject_cast<const TriggerSkillV2 *>(ts));
			if (!v2) continue;
        QList<SkillContext> supplied;
        const bool collected = v2->collectTriggerContexts(triggerEvent, room, target, data, supplied);
        restoreIdentity();
        if (collected) {
            QMap<QString, QStringList> precedingTargets;
            QSet<QString> compulsoryOrderedKeys;
            for (SkillContext ctx : supplied) {
                const QString definitionName = TriggerSkillV2::parseSkillName(ctx.skill_name);
                const auto *definition = dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getTriggerSkill(definitionName));
                if (!definition || !ctx.owner) continue;
                if (room->isAcceptedViewAsEffect(ctx.activationRef)) continue;
                ServerPlayer *decisionMaker = definition->triggerOrderPlayer(room, ctx);
                if (!decisionMaker || declinedOwners.contains(decisionMaker)) continue;
                const QString key = skillInstanceRuntimeKey(ctx.owner, definitionName, ctx.instanceID)
                    + '|' + (ctx.invoker ? ctx.invoker->objectName() : QString());
                const QString orderedKey = key + '|' + QString::number(ctx.trigger_count);
                if (ctx.preferredTarget) {
                    const QString targetName = ctx.preferredTarget->objectName();
                    precedingTargets[orderedKey] << targetName;
                    if (consumedContextTargets.value(orderedKey).contains(targetName)) continue;
                    contextTargetPrefixes.insert(orderedKey + '|' + ctx.skill_name, precedingTargets.value(orderedKey));
                } else if (ctx.trigger_count < triggerCounts.value(key)) {
                    continue;
                }
                const Skill::Frequency frequency = definition->getFrequency(ctx.owner);
                const bool orderedCompulsory = ctx.preferredTarget
                    && !room->isGeneralHiddenForSkill(ctx.activationRef)
                    && (frequency == Skill::Compulsory || frequency == Skill::Wake);
                if (orderedCompulsory && compulsoryOrderedKeys.contains(orderedKey)) continue;
                if (definition->prepareSource(room, ctx)) {
                    if (orderedCompulsory) compulsoryOrderedKeys.insert(orderedKey);
                    contextSelectors.insert(definition);
                    skillContexts << ctx;
                }
            }
            continue;
        }
		TriggerList list = v2->triggerable(triggerEvent, room, target, data);
        restoreIdentity();
		
		QMap<ServerPlayer *, QStringList>::iterator it;
			for (it = list.begin(); it != list.end(); ++it) {
				ServerPlayer *p = it.key();
				if (!p || declinedOwners.contains(p)) continue;
				QStringList &skills = it.value();
				if (!skills.isEmpty()) {
					foreach (const QString &skill, skills) {
						QString skillName = skill;
						int multiplier = 1;
						int split = skillName.indexOf('*');
						if (split != -1) {
							multiplier = skillName.mid(split + 1).toInt();
							skillName = skillName.left(split);
						}
						QString baseName;
						int instanceId = SkillInstanceUtils::parseName(skillName, baseName);
						skillName = baseName;
						QString orderedTargets;
						if (v2->isEquipSkill())
							skillName = TriggerSkillV2::parseSkillName(skill, nullptr, &orderedTargets,
								&multiplier, &instanceId);
                        // Removal callbacks belong to the public event definition, never a
                        // surviving same-named instance on the other general.
                        bool removalSource = false;
                        if (!v2->isEquipSkill() && instanceId == 0 && p == target
                            && skillName == v2->objectName() && v2->acceptsRemovalEvent(triggerEvent, data)) {
                            if (triggerEvent == GeneralRemoved) {
                                const General *removed = Sanguosha->getGeneral(data.toString());
                                removalSource = removed && removed->hasSkill(skillName, true);
                            } else if (triggerEvent == EventLoseSkill) {
                                SkillChangeStruct change;
                                removalSource = change.tryParse(data) && change.skillName == skillName
                                    && change.instanceID > 0 && !p->hasSkillInstance(skillName, change.instanceID);
                            }
                        }
                        if (v2->acceptsRemovalEvent(triggerEvent, data) && !removalSource) continue;
                        QList<int> instanceIds;
                        if (removalSource) {
                            instanceIds << 0;
                        } else if (v2->isEquipSkill()) {
							// Equipment eligibility is authoritative in its selector, including
							// virtual armor and effects pending after the card has left play.
							if (instanceId != 0 || skillName != v2->objectName()) continue;
							if (!orderedTargets.isEmpty()) {
								// Retain target identity across refreshes: discarding a target's
								// last card must not shift the next target out of the queue.
								const QString key = skillInstanceRuntimeKey(p, skillName, 0);
								const QStringList targets = orderedTargets.split('+', Qt::SkipEmptyParts);
								for (int i = 0; i < targets.size(); ++i) {
									if (consumedEquipmentTargets.value(key).contains(targets.at(i))) continue;
									ServerPlayer *effectTarget = room->findPlayerByObjectName(targets.at(i), true);
									if (!effectTarget) continue;
									SkillContext ctx;
									ctx.skill_name = skillName + "->" + targets.at(i) + '&' + QString::number(i + 1);
									ctx.owner = p;
									ctx.invoker = target;
									ctx.targets << effectTarget;
									ctx.preferredTarget = effectTarget;
									ctx.preferredTargetSeat = effectTarget->getSeat();
									ctx.original_data = &data;
									ctx.current_event = triggerEvent;
									ctx.amount = v2->getBaseAmount();
									ctx.trigger_count = triggerCounts.value(key);
									if (v2->prepareSource(room, ctx)) skillContexts << ctx;
									// Selecting a later target declines the preceding targets.
									equipmentTargetPrefixes.insert(p->objectName() + '|' + ctx.skill_name,
										targets.mid(0, i + 1));
									if (v2->getFrequency(p) == Skill::Compulsory && p->hasShownSkill(v2)) break;
								}
								continue;
							}
							instanceIds << 0;
						} else if (instanceId > 0) {
							if (p->hasSkillInstance(skillName, instanceId)
								&& !p->isSkillInvalid(skillName, instanceId))
								instanceIds << instanceId;
						} else {
							instanceIds = p->getValidSkillInstanceIds(skillName);
						}

						foreach (int resolvedId, instanceIds) {
							const SkillInstanceRef ref(p->objectName(), SkillInstanceKey(skillName, resolvedId));
                            // Accepted continuation leaves authorize only their response prompt.
                            if (room->isAcceptedViewAsEffect(ref)) continue;
							QString key = skillInstanceRuntimeKey(p, skillName, resolvedId);
							int currentTriggerCount = triggerCounts.value(key, 0);
							int effectiveMultiplier = qMax(multiplier, maxMultipliers.value(key, 0));
							if (currentTriggerCount >= effectiveMultiplier)
								continue;
							for (int i = 0; i < effectiveMultiplier - currentTriggerCount; ++i) {
								SkillContext ctx;
								ctx.skill_name = skillName;
								ctx.owner = p;
								ctx.invoker = target;
								ctx.instanceID = resolvedId;
								if (!v2->isEquipSkill() && !removalSource) ctx.activationRef = ref;
								ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
                                if (ctx.activationRef.isValid() && !ctx.sourceRef.isValid()) continue;
								bool amountOk = false;
								if (!v2->isEquipSkill())
									ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &amountOk);
								if (!amountOk) ctx.amount = v2->getBaseAmount();
								ctx.trigger_count = currentTriggerCount + i;
								ctx.multiplier = effectiveMultiplier;
								ctx.original_data = &data;
								ctx.current_event = triggerEvent;
								if (v2->prepareSource(room, ctx)) skillContexts << ctx;
							}
						}
					}
				}
			}
		}


        if (allowedSources) {
            // A helper belongs to its exact root grant; a sibling with the same
            // name is never initialized merely because the definition matches.
            for (int i = skillContexts.size() - 1; i >= 0; --i) {
                const SkillInstanceRef ref = skillContexts.at(i).activationRef;
                const SkillInstanceRef root = SkillInstanceUtils::resolveRootRef(ref,
                    [room](const SkillInstanceRef &source) -> const SkillInstance * {
                        const ServerPlayer *owner = room->findPlayerByObjectName(source.ownerObjectName, true);
                        return owner ? owner->findSkillInstance(source.key.skillName, source.key.instanceID) : nullptr;
                    }, false);
                if (!allowedSources->contains(ref) && !allowedSources->contains(root))
                    skillContexts.removeAt(i);
            }
        }
		if (skillContexts.isEmpty())
			break;

		// Each owner orders only its own skills, in seat order from the current
		// player, even when another player's event triggers them.
		ServerPlayer *chooser = nullptr;
		for (ServerPlayer *owner : room->getAllPlayers(true)) {
			for (const SkillContext &ctx : skillContexts) {
				const auto *definition = dynamic_cast<const TriggerSkillV2 *>(
					Sanguosha->getTriggerSkill(TriggerSkillV2::parseSkillName(ctx.skill_name)));
				if (definition && definition->triggerOrderPlayer(room, ctx) == owner) { chooser = owner; break; }
			}
			if (chooser) break;
		}
		if (!chooser) break;
		for (int i = skillContexts.size() - 1; i >= 0; --i) {
			const SkillContext &ctx = skillContexts.at(i);
			const auto *definition = dynamic_cast<const TriggerSkillV2 *>(
				Sanguosha->getTriggerSkill(TriggerSkillV2::parseSkillName(ctx.skill_name)));
			if (!definition || definition->triggerOrderPlayer(room, ctx) != chooser)
				skillContexts.removeAt(i);
		}

		foreach (const SkillContext &ctx, skillContexts) {
			// A concealed compulsory copy remains optional. A revealed sibling
			// must not force this source to reveal as well.
			if (room->isGeneralHiddenForSkill(ctx.activationRef)) continue;
			const QString definitionName = TriggerSkillV2::parseSkillName(ctx.skill_name);
			const TriggerSkill *ts = Sanguosha->getTriggerSkill(definitionName, ctx.instanceID);
			// Awakening and this invocation's forced flag also remove the order
			// dialog's cancel option; concealed sources keep the gate above.
			const Skill::Frequency frequency = ts ? ts->getFrequency(ctx.owner) : Skill::NotFrequent;
			if (ctx.is_forced || frequency == Skill::Compulsory || frequency == Skill::Wake) {
				has_compulsory = true;
				break;
			}
			if (ctx.owner) {
				foreach (const QString &mark, ctx.owner->getMarkNames()) {
					if (mark.contains(definitionName) && mark.contains("_force")) {
						has_compulsory = true;
						break;
					}
				}
				if (has_compulsory) break;
			}
		}

		// Return format: "skillName" or "skillName:ownerObjectName".
		QString reason = "GameRule:TriggerOrder";
		QString name;
		bool globalRule = false;
		// Global rule/record/cleanup callbacks are not player ordering choices,
		// even when Lua leaves their frequency at the default NotFrequent.
		// Resolve them through the normal cost/effect path before offering skills;
		// a concealed general source still requires its owner's reveal consent.
		for (const SkillContext &ctx : skillContexts) {
			const TriggerSkill *skill = Sanguosha->getTriggerSkill(TriggerSkillV2::parseSkillName(ctx.skill_name));
			if (skill && skill->isGlobal()
				&& !room->isGeneralHiddenForSkill(ctx.activationRef)) {
				name = SkillInstanceUtils::formatName(ctx.skill_name, ctx.instanceID);
				if (ctx.owner != chooser) name += ':' + ctx.owner->objectName();
				globalRule = true;
				break;
			}
		}
		if (name.isEmpty())
			name = room->askForTriggerOrder(chooser, reason, skillContexts, !has_compulsory, data);

		if (name == "cancel" || name.isEmpty()) {
			// Declining ends only this owner's skills; later owners still choose.
			declinedOwners.insert(chooser);
			continue;
		}

		// Parse the return value into skillName and ownerObjectName.
		QString ownerObjectName;
		QString skillName = name;
		int split = -1;
		if ((split = name.indexOf(':')) != -1) {
			skillName = name.left(split);           // Format 2: skillName:ownerObjectName.
			ownerObjectName = name.mid(split + 1);
		}

		if ((split = skillName.indexOf('*')) != -1)
			skillName = skillName.left(split);

		QString baseName;
		int instanceId = SkillInstanceUtils::parseName(skillName, baseName);
		skillName = baseName;
		const QString selectedName = skillName;
		skillName = TriggerSkillV2::parseSkillName(selectedName);

		const TriggerSkill *result_skill = Sanguosha->getTriggerSkill(skillName, instanceId);
		if (!result_skill) continue;

		TriggerSkillV2 *v2 = const_cast<TriggerSkillV2 *>(qobject_cast<const TriggerSkillV2 *>(result_skill));
		if (!v2) continue;

		// Format 2: match selected_ctx by ownerObjectName.
		SkillContext *selected_ctx = nullptr;
		for (int i = 0; i < skillContexts.size(); ++i) {
			if (skillContexts[i].skill_name == selectedName &&
				skillContexts[i].instanceID == instanceId) {
				// Format 1: ownerObjectName is empty; match owner == chooser.
				// Format 2: ownerObjectName is set; match owner->objectName() == ownerObjectName.
				if (ownerObjectName.isEmpty()) {
					if (v2->triggerOrderPlayer(room, skillContexts[i]) == chooser) {
						selected_ctx = &skillContexts[i];
						break;
					}
				} else {
					if (skillContexts[i].owner && skillContexts[i].owner->objectName() == ownerObjectName) {
						selected_ctx = &skillContexts[i];
						break;
					}
				}
			}
		}
		if (!selected_ctx) {
			continue;
		}

		ServerPlayer *skill_owner = selected_ctx->owner;
		if (!skill_owner) continue;
		const bool equipment = v2->isEquipSkill();
		// Hidden global rule/record callbacks are not player skill invocations. They keep
		// cost/effect and every context state, but skip the six skill observer events.
		const bool silentRule = globalRule && v2->isGlobal() && !v2->isVisible();
		const SkillInstanceRef selectedSource = selected_ctx->activationRef;
        // Keep the admitted source immutable across cost/interceptor callbacks.
        const SkillContext sourceContext = *selected_ctx;
        const auto sourceAvailable = [&]() {
            return v2->isSourceAvailable(room, sourceContext);
        };
		if (!sourceAvailable()) continue;

		QString key = skillInstanceRuntimeKey(skill_owner, skillName, instanceId);
        if (contextSelectors.contains(v2)) {
            key += '|' + (selected_ctx->invoker ? selected_ctx->invoker->objectName() : QString());
            const QString orderedKey = key + '|' + QString::number(selected_ctx->trigger_count);
            consumedContextTargets[orderedKey] << contextTargetPrefixes.value(orderedKey + '|' + selected_ctx->skill_name);
            selected_ctx->skill_name = skillName;
        }
		if (equipment) {
			consumedEquipmentTargets[key] << equipmentTargetPrefixes.value(
				skill_owner->objectName() + '|' + selected_ctx->skill_name);
			// UI choices include an ordered target; callbacks use the definition ID.
			selected_ctx->skill_name = skillName;
		}
		triggerCounts[key] = triggerCounts.value(key, 0) + 1;
		triggeredSkills.insert(key);

		// Format 2: use selected_ctx->owner (the skill owner) as the player for cost.
		Room::ResolutionScope resolution(*room, skillName);
		ResolutionHistoryEventGuard skillHistory(
			room->resolutionHistory(), QStringLiteral("skill"),
			room->historySkillContext(*selected_ctx), room->historyRecordingEnabled());
		bool do_cost = v2->cost(triggerEvent, room, skill_owner, *selected_ctx);
        restoreIdentity();
        selected_ctx->physicalEquipSource = sourceContext.physicalEquipSource;
		if (!do_cost) {
			skillHistory.finish(QStringLiteral("cancelled"));
			continue;
		}

		selected_ctx->current_event = EventSkillWillInvoke;
		QVariant ctx_data = QVariant::fromValue(*selected_ctx);
		if (!silentRule) trigger(EventSkillWillInvoke, room, skill_owner, ctx_data);
		*selected_ctx = ctx_data.value<SkillContext>();
		skillHistory.update(room->historySkillContext(*selected_ctx));
		maxMultipliers[key] = qMax(maxMultipliers.value(key, 0), selected_ctx->multiplier);
		if (selected_ctx->is_canceled) {
			skillHistory.finish(QStringLiteral("cancelled"));
			continue;
		}

		if (!selected_ctx->bypass_cost) {
			selected_ctx->current_event = EventSkillPay;
			ctx_data = QVariant::fromValue(*selected_ctx);
			if (!silentRule) trigger(EventSkillPay, room, skill_owner, ctx_data);
			*selected_ctx = ctx_data.value<SkillContext>();
			skillHistory.update(room->historySkillContext(*selected_ctx));

			// Cost/interceptor callbacks may retire or block the selected source.
			// Do not pay for a replacement instance or reveal a different sibling.
			if (selected_ctx->is_canceled) {
				skillHistory.finish(QStringLiteral("cancelled"));
				continue;
			}
			if (!sourceAvailable()) {
				skillHistory.finish(QStringLiteral("source_unavailable"));
				continue;
			}
			bool do_pay = v2->pay(triggerEvent, room, skill_owner, *selected_ctx);
            restoreIdentity();
            selected_ctx->physicalEquipSource = sourceContext.physicalEquipSource;
			if (!do_pay) {
				skillHistory.finish(QStringLiteral("pay_failed"));
				continue;
			}
		}

		selected_ctx->current_event = EventSkillTargetConfirming;
		selected_ctx->updated_targets = selected_ctx->targets;
		ctx_data = QVariant::fromValue(*selected_ctx);
		if (!silentRule) trigger(EventSkillTargetConfirming, room, skill_owner, ctx_data);
		*selected_ctx = ctx_data.value<SkillContext>();
		selected_ctx->targets = selected_ctx->updated_targets;
		skillHistory.update(room->historySkillContext(*selected_ctx));

		const bool sourceWasAlive = skill_owner->isAlive();
        if (selectedSource.isValid() && (!sourceAvailable() || !room->showGeneralForSkill(selectedSource)
            || (sourceWasAlive && !skill_owner->isAlive()) || !sourceAvailable())) {
			skillHistory.finish(QStringLiteral("source_unavailable"));
			continue;
		}
        bool invocationStarted = false;
        bool completionStarted = false;
        bool contextEventInFlight = false;
        const auto finishInvocation = [&]() {
            if (!invocationStarted || completionStarted) return;
            // Set before notifying: a completion handler may itself throw or
            // invoke another skill. This invocation must never finish twice.
            completionStarted = true;
            if (contextEventInFlight) *selected_ctx = ctx_data.value<SkillContext>();
            selected_ctx->sourceRef = sourceContext.sourceRef;
            selected_ctx->physicalEquipSource = sourceContext.physicalEquipSource;
            selected_ctx->activationRef = sourceContext.activationRef;
            selected_ctx->owner = sourceContext.owner;
            selected_ctx->instanceID = sourceContext.instanceID;
            selected_ctx->current_event = EventSkillEffectFinished;
            QVariant finishedData = QVariant::fromValue(*selected_ctx);
            // Finished observers can mutate the final targets and then throw.
            // Read back on both exits while all context storage is still live.
            const auto historyGuard = qScopeGuard([&]() {
                *selected_ctx = finishedData.value<SkillContext>();
                selected_ctx->sourceRef = sourceContext.sourceRef;
                selected_ctx->physicalEquipSource = sourceContext.physicalEquipSource;
                selected_ctx->activationRef = sourceContext.activationRef;
                selected_ctx->owner = sourceContext.owner;
                selected_ctx->instanceID = sourceContext.instanceID;
                skillHistory.update(room->historySkillContext(*selected_ctx));
            });
            if (!silentRule) trigger(EventSkillEffectFinished, room, skill_owner, finishedData);
        };
        const auto completionGuard = qScopeGuard([&]() {
            // Normal exits finish explicitly below. On unwinding, preserve the
            // original control/exception even if a cleanup observer also throws.
            try {
                finishInvocation();
            } catch (...) {
            }
        });
        selected_ctx->current_event = EventSkillInvoking;
        ctx_data = QVariant::fromValue(*selected_ctx);
        invocationStarted = true;
        contextEventInFlight = true;
        if (!silentRule) trigger(EventSkillInvoking, room, skill_owner, ctx_data);
        *selected_ctx = ctx_data.value<SkillContext>();
        contextEventInFlight = false;
        skillHistory.update(room->historySkillContext(*selected_ctx));

        selected_ctx->current_event = EventSkillEffect;
        ctx_data = QVariant::fromValue(*selected_ctx);
        contextEventInFlight = true;
        bool skip_effect = !silentRule && trigger(EventSkillEffect, room, skill_owner, ctx_data);
        *selected_ctx = ctx_data.value<SkillContext>();
        contextEventInFlight = false;
        // Acceptance already committed payment/quota. Cancellation suppresses
        // the effect without rolling back that accepted invocation.
        skip_effect = skip_effect || selected_ctx->is_canceled;

        // Interceptors may remove the exact grant after revelation as well.
        if (!sourceAvailable()) {
            finishInvocation();
            skillHistory.finish(QStringLiteral("source_unavailable"));
            continue;
        }
        if (!skip_effect) {
            broken = v2->effect(triggerEvent, room, skill_owner, *selected_ctx);
            restoreIdentity();
            selected_ctx->physicalEquipSource = sourceContext.physicalEquipSource;

            if (!broken && !selected_ctx->manual_effect && !selected_ctx->targets.isEmpty()) {
                foreach (ServerPlayer *t, selected_ctx->targets) {
                    bool target_broken = v2->skillEffect(triggerEvent, room, skill_owner, *selected_ctx, t);
                    restoreIdentity();
                    selected_ctx->physicalEquipSource = sourceContext.physicalEquipSource;
                    if (target_broken)
                        broken = true;
                }
            }
        }
        finishInvocation();
		skillHistory.finish(skip_effect ? QStringLiteral("skipped")
			: (broken ? QStringLiteral("broken") : QStringLiteral("completed")));
	}

	return broken;
}

void RoomThread::refreshDistanceCacheIfDirty(Room *room)
{
	if (!room || !m_distanceCacheDirty) return;

	// Clear before rebuilding so nested events can schedule a later refresh.
	m_distanceCacheDirty = false;
	QList<ServerPlayer *> players = room->getAlivePlayers();
	foreach (ServerPlayer *player, players)
		distancePropertyName(player);

	foreach(ServerPlayer *from, players) {
		// Shutdown need not finish a presentation-only all-pairs refresh.
		if (isInterruptionRequested()) return;
		QHash<const ServerPlayer *, int> &lastDistances = m_lastBroadcastDistances[from];
		foreach(ServerPlayer *to, players) {
			if (from == to) continue;

			const int distance = from->distanceTo(to, 0);

			const QByteArray &propertyName = distancePropertyName(to);
			const auto lastDistance = lastDistances.constFind(to);
			const bool unchanged = lastDistance != lastDistances.constEnd()
				&& lastDistance.value() == distance;
			if (unchanged) continue;

			room->safeSetPlayerProperty(from, propertyName.constData(), distance);
			room->broadcastProperty(from, propertyName.constData());
			lastDistances.insert(to, distance);
		}
	}
}

void RoomThread::preparePlayers()
{
    // Keep skill creation and notifications immediate, but project only the
    // completed roster at startGame's existing final presentation boundary.
    QScopedValueRollback<bool> preparing(m_preparingPlayerUiState, true);
    m_playerUiStateDirty = true;
    room->preparePlayers();
}

bool RoomThread::deferPlayerUiState(ServerPlayer *player)
{
    // Shutdown must not spend another turn rebuilding Lua-backed presentation.
    if (isInterruptionRequested()) return true;
    if (m_flushingPlayerUiState || (event_stack.isEmpty() && !m_preparingPlayerUiState)) return false;
    m_pendingPlayerUiState.insert(player);
    return true;
}

void RoomThread::flushPlayerUiState()
{
    if (isRunning() && QThread::currentThread() != this) return;
    if (!room || isInterruptionRequested() || m_flushingPlayerUiState || m_preparingPlayerUiState) return;
    const bool allPlayers = m_playerUiStateDirty;
    const bool descriptions = m_skillDescriptionsDirty.exchange(false);
    const auto pending = m_pendingPlayerUiState;
    m_playerUiStateDirty = false;
    m_pendingPlayerUiState.clear();
    if (!allPlayers && !descriptions && pending.isEmpty()) return;
    QScopedValueRollback<bool> flushing(m_flushingPlayerUiState, true);
    QScopedValueRollback<bool> refreshing(m_refreshingSkillDescriptions, true);
    // Full UI refresh already includes descriptions. Evaluate each player once,
    // retaining the live-roster order and clearing dead players' stale summaries.
    foreach (ServerPlayer *player, room->getAllPlayers(true)) {
        if (isInterruptionRequested()) return;
        if (player->isAlive() && player->getGeneral() && (allPlayers || pending.contains(player)))
            player->refreshUIState();
        else if (descriptions || allPlayers)
            player->refreshSkillDescriptionState();
    }
}

void RoomThread::markSkillDescriptionsDirty()
{
    // Signals may originate outside the room worker. Only mark here, never run Lua.
    m_skillDescriptionsDirty.store(true);
}

void RoomThread::refreshSkillDescriptions()
{
    if (isRunning() && QThread::currentThread() != this) return;
    if (m_refreshingSkillDescriptions) return;
    // Requests use the same batch boundary as ordinary deferred presentation.
    flushPlayerUiState();
}

void RoomThread::flushOutermostDeferredWork(Room *room)
{
    if (!room || !event_stack.isEmpty() || isInterruptionRequested()) return;
    room->processPendingPreshows();
    room->flushHegemonyReveals();
    flushPlayerUiState();

	refreshDistanceCacheIfDirty(room);

	if (room->hasPendingSummons())
		room->processPendingSummons();

	room->processPendingAnytimeSkills();
}

// Pre-deferral trigger entry: recompute and broadcast the hand limit before
// skills run. Waiting for an empty event stack holds the number until turn end.
static bool syncsHandLimit(TriggerEvent triggerEvent)
{
	switch (triggerEvent) {
	case HpChanged:
	case MaxHpChanged:
	case CardsMoveOneTime:
	case EventAcquireSkill:
	case EventLoseSkill:
	case MarkChanged:
	case KingdomChanged:
	case Death:
	case Revive:
	case TurnStart:
	case GameStart:
		return true;
	default:
		return false;
	}
}

static bool invalidatesDistanceCache(TriggerEvent triggerEvent)
{
	switch (triggerEvent) {
	case HpChanged:
	case MaxHpChanged:
	case CardsMoveOneTime:
	case EventAcquireSkill:
	case EventLoseSkill:
	case GeneralShown:
	case GeneralHidden:
	case GeneralRemoved:
	case EventSkillAmountChanged:
	case MarkChanged:
	case KingdomChanged:
	case Death:
	case Revive:
	case TurnStart:
	case GameStart:
		return true;
	default:
		return false;
	}
}

namespace {
void recordTurnHpSnapshot(Room *room, qint64 turnId, const QString &boundary, const QString &completion);
}
bool RoomThread::trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*target, QVariant &data)
{
    // Physical provenance is native admission data. Hooks may change effect state,
    // but cannot replace this receipt with another equipment or an empty identity.
    const QVariant physicalIdentity = data;
    const auto restorePhysicalIdentity = qScopeGuard([&] {
        restorePhysicalEquipmentIdentity(data, physicalIdentity);
    });
	// Room APIs used by TakeoverRule must update containers and notifications
	// without replaying historical gameplay triggers during reconstruction.
	if (room && room->isRestoringTakeoverSnapshot())
		return false;
	if (!room)
		return dispatchTrigger(triggerEvent, room, target, data);
	room->processPendingPreshows();

    // Record accepted invocations, not skill scopes opened before cost/payment.
    // Legacy adapters also emit SkillTriggered; record that path only once.
    if (room->historyRecordingEnabled()
        && (triggerEvent == EventSkillInvoking || triggerEvent == SkillTriggered)) {
        QVariantMap invocation;
        if (triggerEvent == EventSkillInvoking) {
            const SkillContext context = data.value<SkillContext>();
            if ((context.activationRef.isValid() || context.physicalEquipSource.isValid() || context.use_card)
                && !context.extra_data.toMap().value(QStringLiteral("legacy_activation")).toBool()) {
                invocation = room->historySkillContext(context);
                invocation.insert(QStringLiteral("invoked_skill"), context.activationRef.isValid()
                    ? context.activationRef.key.skillName : context.skill_name);
            }
        } else {
            invocation.insert(QStringLiteral("invoked_skill"), data.toString());
            // The legacy notification identifies its actor, not an exact source.
            invocation.insert(QStringLiteral("attribution_complete"), false);
        }
        if (!invocation.isEmpty() && target) {
            invocation.insert(QStringLiteral("player"), target->objectName());
            room->resolutionHistory().appendFact(room->currentHistoryEventId(),
                QStringLiteral("skill_invoked"), invocation);
        }
    }

	const bool outerTurn = triggerEvent == TurnStart && event_stack.isEmpty();
	QVariantMap turnData;
	if (triggerEvent == TurnStart && room) {
		turnData.insert(QStringLiteral("player"), target ? target->objectName() : QString());
		turnData.insert(QStringLiteral("extra_turn"), room->isCurrentExtraTurn());
		if (room->isCurrentExtraTurn()) {
			turnData.insert(QStringLiteral("reason"), room->getCurrentExtraTurnReason());
			const SkillInstanceRef source = room->getCurrentExtraTurnSourceRef();
			turnData.insert(QStringLiteral("cause_event_id"), room->getCurrentExtraTurnCauseEventId());
			turnData.insert(QStringLiteral("skill_name"), source.key.skillName);
			turnData.insert(QStringLiteral("skill_owner"), source.ownerObjectName);
			turnData.insert(QStringLiteral("instance_id"), source.key.instanceID);
		}
	}
	ResolutionHistoryEventGuard turnHistory(room->resolutionHistory(), QStringLiteral("turn"),
		turnData, room->historyRecordingEnabled() && triggerEvent == TurnStart);
    if (turnHistory.id() != 0) recordTurnHpSnapshot(room, turnHistory.id(), "start", "running");
	bool broken = false;
	// Capture the enclosing use before callbacks can open nested uses. The
	// accepted-use fact keeps its original targets; completion is a new fact.
	const qint64 targetUseEventId = triggerEvent == TargetSpecified && room->historyRecordingEnabled()
		? room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong() : 0;
    // A stage fact records reaching this dispatch, even when a listener later
    // prevents its result or interrupts it. Do not infer it from actual damage.
    if (room->historyRecordingEnabled()
        && (triggerEvent == DamageCaused || triggerEvent == DamageInflicted)
        && data.canConvert<DamageStruct>()) {
        const DamageStruct damage = data.value<DamageStruct>();
        const qint64 eventId = room->historyParent(room->currentHistoryEventId(),
            QStringLiteral("damage"), true).value(QStringLiteral("id")).toLongLong();
        if (eventId > 0) {
            QVariantMap fact = room->historyCause(CardMoveReason(CardMoveReason::S_REASON_UNKNOWN,
                damage.from ? damage.from->objectName() : QString(), damage.reason, QString()));
            fact.insert(QStringLiteral("damage_event_id"), eventId);
            fact.insert(QStringLiteral("player"), target ? target->objectName() : QString());
            fact.insert(QStringLiteral("from"), damage.from ? damage.from->objectName() : QString());
            fact.insert(QStringLiteral("to"), damage.to ? damage.to->objectName() : QString());
            fact.insert(QStringLiteral("requested_amount"), damage.damage);
            fact.insert(QStringLiteral("nature"), int(damage.nature));
            fact.insert(QStringLiteral("chain"), damage.chain);
            fact.insert(QStringLiteral("transfer"), damage.transfer);
            fact.insert(QStringLiteral("card"), room->historyCardSnapshot(damage.card));
            room->resolutionHistory().appendFact(eventId,
                triggerEvent == DamageCaused ? QStringLiteral("damage_caused") : QStringLiteral("damage_inflicted"), fact);
        }
    } else if (room->historyRecordingEnabled() && triggerEvent == TargetConfirmed
               && target && data.canConvert<CardUseStruct>()) {
        const CardUseStruct use = data.value<CardUseStruct>();
        const qint64 eventId = room->historyParent(room->currentHistoryEventId(),
            QStringLiteral("use_card"), true).value(QStringLiteral("id")).toLongLong();
        // TargetConfirmed is broadcast to observers too. Only actual recipients
        // contribute a confirmation, using this seat's pre-dispatch target list.
        if (eventId > 0 && use.card && use.to.contains(target)) {
            QVariantList targets;
            for (const ServerPlayer *recipient : use.to)
                if (recipient) targets << recipient->objectName();
            CardMoveReason reason(CardMoveReason::S_REASON_USE,
                use.from ? use.from->objectName() : QString(), use.card->getSkillName(), QString());
            reason.m_useStruct = use;
            QVariantMap fact = room->historyCause(reason);
            fact.insert(QStringLiteral("use_event_id"), eventId);
            fact.insert(QStringLiteral("player"), target->objectName());
            fact.insert(QStringLiteral("to"), target->objectName());
            fact.insert(QStringLiteral("from"), use.from ? use.from->objectName() : QString());
            fact.insert(QStringLiteral("card"), room->historyCardSnapshot(use.card));
            fact.insert(QStringLiteral("targets"), targets);
            room->resolutionHistory().appendFact(eventId, QStringLiteral("target_confirmed"), fact);
        }
    }
	try {
		broken = dispatchTrigger(triggerEvent, room, target, data);
	} catch (TriggerEvent) {
		if (triggerEvent == TurnStart && turnHistory.id() != 0)
			rememberInterruptedTurn(turnHistory.id());
		throw;
	}
	if (targetUseEventId > 0 && data.canConvert<CardUseStruct>()) {
		const CardUseStruct use = data.value<CardUseStruct>();
		if (use.from && use.card) {
			QVariantList targets;
			for (const ServerPlayer *recipient : use.to)
				if (recipient) targets << recipient->objectName();
			room->resolutionHistory().appendFact(targetUseEventId, "use_card_targets",
				{{"use_event_id", targetUseEventId}, {"from", use.from->objectName()},
				 {"player", use.from->objectName()}, {"card", room->historyCardSnapshot(use.card)},
				 {"targets", targets}, {"attribution_complete", true}});
		}
	}
	if (turnHistory.id() != 0)
        recordTurnHpSnapshot(room, turnHistory.id(), "end", broken ? "broken" : "completed");
    turnHistory.finish(broken ? QStringLiteral("broken") : QStringLiteral("completed"));
	// dispatchTrigger's CardLifetimeScope and deferred work must finish first.
	// Exceptions skip this point: the guard records an aborted turn while the
	// mode-specific phase cleanup remains responsible for control flow.
	if (outerTurn)
		reclaimCompletedTurn();
	// Only completed effects may settle work objectives. This is outside
	// dispatchTrigger's catch/pop cleanup; gameOver throws GameFinished.
	if (room->isWorkSession()) {
		bool enclosingEffect = false;
		for (const auto &event : event_stack) {
			// Turn/phase drivers enclose user actions; other nested effects must
			// return to their caller before a goal is allowed to terminate play.
			if (event.event() != TurnStart && event.event() != EventPhaseProceeding)
				enclosingEffect = true;
		}
		if (!enclosingEffect && (triggerEvent == CardFinished || triggerEvent == DamageComplete
			|| triggerEvent == GameReady || triggerEvent == EventPhaseStart
			|| triggerEvent == EventPhaseEnd || triggerEvent == EventPhaseProceeding || outerTurn))
			room->evaluateWorkObjectives();
	}
	return broken;
}

void RoomThread::reclaimCompletedTurn()
{
	if (room && event_stack.isEmpty() && room->roomRuntime())
		room->roomRuntime()->reclaimTurnCards();
}

namespace {
void recordTurnHpSnapshot(Room *room, qint64 turnId, const QString &boundary,
                          const QString &completion)
{
    if (!room || !turnId || !room->historyRecordingEnabled()) return;
    ResolutionHistoryContextGuard context(room->resolutionHistory(), turnId, true);
    const QString actor = room->historyEvent(turnId).value("data").toMap().value("player").toString();
    for (ServerPlayer *player : room->getAllPlayers(true)) {
        room->resolutionHistory().appendFact(turnId, "turn_hp_snapshot",
            {{"player", player->objectName()}, {"hp", player->getHp()}, {"alive", player->isAlive()},
             {"phase", int(player->getPhase())}, {"turn_owner", actor}, {"boundary", boundary},
             {"completion", completion}, {"attribution_complete", true}});
    }
}
}

void RoomThread::rememberInterruptedTurn(qint64 eventId)
{
	m_interruptedTurnEventId = eventId;
}

qint64 RoomThread::takeInterruptedTurn()
{
	const qint64 eventId = m_interruptedTurnEventId;
	m_interruptedTurnEventId = 0;
	return eventId;
}

qint64 RoomThread::interruptedTurn() const
{
	return m_interruptedTurnEventId;
}

void RoomThread::clearInterruptedTurn()
{
    recordTurnHpSnapshot(room, m_interruptedTurnEventId, "end", "interrupted");
	m_interruptedTurnEventId = 0;
}

void RoomThread::rememberInterruptedPhase(qint64 eventId)
{
	m_interruptedPhaseEventId = eventId;
}

qint64 RoomThread::takeInterruptedPhase()
{
	const qint64 eventId = m_interruptedPhaseEventId;
	m_interruptedPhaseEventId = 0;
	return eventId;
}

qint64 RoomThread::interruptedPhase() const
{
	return m_interruptedPhaseEventId;
}

void RoomThread::clearInterruptedPhase()
{
	m_interruptedPhaseEventId = 0;
}

bool RoomThread::dispatchTrigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*target, QVariant &data)
{
    const QVariant physicalIdentity = data;
	if (room)
		room->throwIfStopRequested();
	CardLifetimeScope cardScope(globalCardLifetimeManager());
	if (m_perfTraceEnabled)
		++m_triggerDispatchProfile.triggerCount;
	// push it to event stack
	EventTriplet triplet(triggerEvent, room, target);
	event_stack.push_back(triplet);
	bool broken = false;/*
	QList<ServerPlayer*>players = room->getAllPlayers(true);
	foreach(ServerPlayer*p,players){
		QList<TriggerSkill*>triggered;
		while(broken==false){
			if(triggerEvent==EnterDying||triggerEvent==Dying||triggerEvent==AskForPeaches){
				if(!data.value<DyingStruct>().who->hasFlag("Global_Dying"))
					break;
			}
			int x = -99;
			QHash<int,QList<TriggerSkill*> >i2ss;
			foreach(TriggerSkill*ts,skill_table[triggerEvent]){
				if(triggered.contains(ts)) continue;
				if(ts->triggerable(target,room,triggerEvent,p,data)){
					int n = ts->getPriority(triggerEvent);
					i2ss[n] << ts;
					x = qMax(x,n);
				}
			}
			if(i2ss.isEmpty()) break;
			QStringList choices;
			foreach(TriggerSkill*ts,i2ss[x]){
				if(p->hasSkill(ts->objectName(),true)){
					if(ts->isVisible()){
						choices << ts->objectName();
						continue;
					}
				}if(p!=players.first())
					continue;
				triggered << ts;
				room->tryPause();
				broken = ts->trigger(triggerEvent,room,target,data,p);
				i2ss.clear();
				break;
			}
			if(i2ss.isEmpty()) continue;
			if(choices.isEmpty()) break;
			QString choice = choices.first();
			if(choices.length()>1) choice = room->askForChoice(p,"triggered",choices.join("+"),data);
			foreach(TriggerSkill*ts,i2ss[x]){
				if(ts->objectName()==choice){
					triggered << ts;
					room->tryPause();
					broken = ts->trigger(triggerEvent,room,target,data,p);
					break;
				}
			}
		}
	}*/
	sortTriggerSkills(triggerEvent, room, false);
	if (invalidatesDistanceCache(triggerEvent)) {
		m_playerUiStateDirty = true;
		markDistanceCacheDirty();
	}
	if (syncsHandLimit(triggerEvent)) {
		foreach (ServerPlayer *player, room->getAlivePlayers())
			player->broadcastHandMax();
	}
	try {
		broken = triggerV2Skills(triggerEvent, room, target, data);
		if (broken) {
			event_stack.pop_back();
			flushOutermostDeferredWork(room);
			return broken;
		}
		// This is membership only; event order still comes from skill_table.
		QSet<TriggerSkill *> triggered;
		for (int i = 0; i < skill_table[triggerEvent].length(); i++) {
			TriggerSkill*ts = skill_table[triggerEvent][i];
			if (m_perfTraceEnabled)
				++m_triggerDispatchProfile.mainTableCandidateVisitCount;
			if (m_triggerSkillTraits.value(ts).v2 && !usesV2EventPriority(ts)) continue;
			if (triggered.contains(ts)) continue;
			triggered << ts;
            const quint64 tableRevision = m_triggerTableRevision[triggerEvent];
            if (triggerEvent == EnterDying || triggerEvent == Dying || triggerEvent == AskForPeaches) {
                if (!data.value<DyingStruct>().who->hasFlag("Global_Dying")) break;
            }
            if (m_triggerSkillTraits.value(ts).v2 && usesV2EventPriority(ts)) {
                QList<TriggerSkill *> group;
                for (TriggerSkill *candidate : skill_table[triggerEvent]) {
                    if (m_triggerSkillTraits.value(candidate).v2 && usesV2EventPriority(candidate)
                        && candidate->getPriority(triggerEvent) == ts->getPriority(triggerEvent)
                        && (candidate == ts || !triggered.contains(candidate))) {
                        group << candidate;
                        triggered << candidate;
                    }
                }
                broken = triggerV2Skills(triggerEvent, room, target, data, &group);
                if (broken) break;
                if (tableRevision != m_triggerTableRevision[triggerEvent]) i = -1;
                continue;
            }
			if (ts->triggerable(target,room,triggerEvent,target,data)) {
				if(ts->getFrequency(target)==Skill::Wake&&!ts->canWake(triggerEvent,target,data,room)) continue;
				room->tryPause();
				Room::ResolutionScope resolution(*room, ts->objectName());
				const bool isRule = ts->inherits("GameRule")
					|| ts->objectName() == QLatin1String("game_rule")
					|| ts->objectName() == QLatin1String("hulaopass_mode");
				const QVariantMap legacySkillData{
					{QStringLiteral("skill_name"), ts->objectName()},
					{QStringLiteral("skill_owner"), QString()},
					{QStringLiteral("invoker"), QString()},
					{QStringLiteral("instance_id"), 0},
					{QStringLiteral("execution_id"), 0},
					{QStringLiteral("attribution_complete"), false}};
				ResolutionHistoryEventGuard skillHistory(
					room->resolutionHistory(), QStringLiteral("skill"), legacySkillData,
					room->historyRecordingEnabled() && !isRule);
                LegacyExecutionFrame legacyFrame;
                legacyFrame.skillName = ts->objectName();
                legacyFrame.event = triggerEvent;
                legacyFrame.target = target;
                legacyFrame.data = &data;
                for (ServerPlayer *owner : room->getAllPlayers(true)) {
                    for (int instanceId : owner->getSkillInstanceIds(legacyFrame.skillName)) {
                        const SkillInstance *instance = owner->findSkillInstance(legacyFrame.skillName, instanceId);
                        if (instance && instance->source == SourceAttached
                            && owner->getSkillInstanceStateValue(legacyFrame.skillName, instanceId,
                                QStringLiteral("legacy_activation_lifecycle")).toBool()) {
                            legacyFrame.sources[owner->objectName()] << SkillInstanceRef(
                                owner->objectName(), instance->key());
                        }
                    }
                }
                m_legacyExecutionFrames << legacyFrame;
                const auto legacyFrameGuard = qScopeGuard([&]() { m_legacyExecutionFrames.removeLast(); });
                broken = ts->trigger(triggerEvent,room,target,data);
                restorePhysicalEquipmentIdentity(data, physicalIdentity);
				skillHistory.finish(broken ? QStringLiteral("broken") : QStringLiteral("completed"));
				if(triggerEvent!=SkillTriggered&&room->getTag("notifyInvoked:"+ts->objectName()).toBool()){
					room->removeTag("notifyInvoked:"+ts->objectName());
					QVariant skillName = ts->objectName();
					trigger(SkillTriggered,room,target,skillName);
				}
				if(broken) break;
				// Nested dispatch or newly registered skills may reorder this table.
				// A stable table can continue directly instead of rescanning its prefix.
				if (tableRevision != m_triggerTableRevision[triggerEvent]) i = -1;
			}
		}
		room->recordAiEvent(int(triggerEvent), target, data);
		if (target) target->getSmartAI()->filterEvent(triggerEvent, target, data);
		event_stack.pop_back();// pop event stack
		flushOutermostDeferredWork(room);

    }catch (TriggerEvent throwed_event) {
		room->recordAiEvent(int(triggerEvent), target, data);
		if (target) target->getSmartAI()->filterEvent(triggerEvent, target, data);
		event_stack.pop_back();// pop event stack
		flushOutermostDeferredWork(room);
        throw throwed_event;
	}
	//room->tryPause();
	return broken;
}

bool RoomThread::isLegacySkillActivationActive(const SkillInstanceRef &source) const
{
    return source.isValid() && m_activeLegacySources.contains(source);
}

bool LegacySkillActivation::isAvailable(Room *room, ServerPlayer *owner, const QString &skillName)
{
    if (!room || !owner || !room->getThread()) return true;
    const RoomThread *thread = room->getThread();
    if (thread->m_legacyExecutionFrames.isEmpty()) return true;
    const RoomThread::LegacyExecutionFrame &frame = thread->m_legacyExecutionFrames.last();
    if (frame.skillName != skillName || !frame.sources.contains(owner->objectName())) return true;
    for (const SkillInstanceRef &ref : frame.sources.value(owner->objectName())) {
        if (owner->getValidSkillInstanceIds(ref.key.skillName).contains(ref.key.instanceID)) return true;
    }
    return false;
}

LegacySkillActivation::LegacySkillActivation(Room *room, ServerPlayer *owner,
                                           const QString &skillName)
    : m_room(room), m_uncaughtExceptions(std::uncaught_exceptions())
{
    if (!room || !owner || !room->getThread()) return;
    RoomThread *thread = room->getThread();
    if (thread->m_legacyExecutionFrames.isEmpty()) return;
    // A helper called by another skill must not claim that skill's borrowed grant.
    const RoomThread::LegacyExecutionFrame frame = thread->m_legacyExecutionFrames.last();
    if (frame.skillName != skillName || !frame.sources.contains(owner->objectName())) return;
    const QList<SkillInstanceRef> pinned = frame.sources.value(owner->objectName());
    QList<SkillInstanceRef> available;
    for (const SkillInstanceRef &ref : pinned) {
        if (owner->getValidSkillInstanceIds(ref.key.skillName).contains(ref.key.instanceID))
            available << ref;
    }
    // The previous iteration may have consumed this projection. Never silently
    // fall back to a newly attached same-name source during the old callback.
    if (available.isEmpty()) { m_allowed = false; return; }
    SkillInstanceRef activation = available.first();
    if (available.size() > 1) {
        QStringList choices;
        for (const SkillInstanceRef &ref : available)
            choices << SkillInstanceUtils::formatName(ref.key.skillName, ref.key.instanceID);
        const QString choice = room->askForChoice(owner, skillName, choices.join("+"));
        const int index = choices.indexOf(choice);
        if (index < 0) { m_allowed = false; return; }
        activation = available.at(index);
    }
    const SkillInstanceRef source = room->resolveSkillInstanceRootRef(activation);
    if (!source.isValid() || !owner->getValidSkillInstanceIds(skillName).contains(activation.key.instanceID)
        || !room->showGeneralForSkill(activation)
        || !owner->getValidSkillInstanceIds(skillName).contains(activation.key.instanceID)) {
        m_allowed = false;
        return;
    }
    std::unique_ptr<SkillContext> context(new SkillContext);
    context->skill_name = skillName;
    context->owner = owner;
    context->invoker = owner;
    context->initiator = frame.target;
    context->instanceID = activation.key.instanceID;
    context->activationRef = activation;
    context->sourceRef = source;
    context->original_data = frame.data;
    context->extra_data = QVariantMap{{QStringLiteral("trigger_event"), int(frame.event)},
                                    {QStringLiteral("legacy_activation"), true}};
    context->current_event = EventSkillInvoking;
    QVariant payload = QVariant::fromValue(*context);
    try {
        thread->trigger(EventSkillInvoking, room, owner, payload);
    } catch (...) {
        // An interrupted accepted activation still completes exactly once.
        *context = payload.value<SkillContext>();
        context->activationRef = activation;
        context->sourceRef = source;
        context->current_event = EventSkillEffectFinished;
        QVariant finished = QVariant::fromValue(*context);
        try { thread->trigger(EventSkillEffectFinished, room, owner, finished); } catch (...) {}
        throw;
    }
    *context = payload.value<SkillContext>();
    context->activationRef = activation;
    context->sourceRef = source;
    context->owner = owner;
    context->instanceID = activation.key.instanceID;
    m_context = context.release();
    thread->m_activeLegacySources << activation;
    m_allowed = owner->getValidSkillInstanceIds(skillName).contains(activation.key.instanceID);
}

LegacySkillActivation::~LegacySkillActivation() noexcept(false)
{
    if (!m_context) return;
    std::unique_ptr<SkillContext> context(m_context);
    RoomThread *thread = m_room->getThread();
    const int activeIndex = thread->m_activeLegacySources.lastIndexOf(context->activationRef);
    if (activeIndex >= 0) thread->m_activeLegacySources.removeAt(activeIndex);
    context->current_event = EventSkillEffectFinished;
    QVariant finished = QVariant::fromValue(*context);
    if (std::uncaught_exceptions() > m_uncaughtExceptions) {
        // Completion must never replace the original interruption exception.
        try { thread->trigger(EventSkillEffectFinished, m_room, context->owner, finished); } catch (...) {}
    } else {
        thread->trigger(EventSkillEffectFinished, m_room, context->owner, finished);
    }
}

bool RoomThread::trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*target)
{
	QVariant data;
	//QVariant data = QVariant::fromValue(target);
	return trigger(triggerEvent, room, target, data);
}

void RoomThread::addTriggerSkill(const TriggerSkill*skill)
{
	if (!skill || skillSet.contains(skill)) return;
	skillSet << skill;
	TriggerSkillTraits traits;
	traits.v2 = skill->inherits("TriggerSkillV2");
	traits.gameRule = skill->inherits("GameRule");
	traits.equipOrRule = traits.gameRule || skill->inherits("WeaponSkill")
		|| skill->inherits("ArmorSkill") || skill->inherits("TreasureSkill")
		|| (traits.v2 && skill->isEquipSkill());
	m_triggerSkillTraits.insert(skill, traits);
	foreach (TriggerEvent event, skill->getTriggerEvents()) {
		TriggerSkill *registeredSkill = const_cast<TriggerSkill *>(skill);
		skill_table[event] << registeredSkill;
		++m_triggerTableRevision[event];
		if (traits.v2)
			v2_skill_table[event] << registeredSkill;
		if(skill_table[event].length()<2) continue;
		if (traits.gameRule || room->getTag("TurnLengthCount").toInt() > 0)
			sortTriggerSkills(event, room, true);
	}
	if (skill->isVisible()) {
		foreach (const Skill*rs, Sanguosha->getRelatedSkills(skill->objectName()))
			addTriggerSkill(qobject_cast<const TriggerSkill*>(rs));
	}
}

void RoomThread::delay(long secs)
{
	//Q_ASSERT(secs >= 0);
	if (secs<0) secs = Config.AIDelay;
	if (Config.AIDelay>0&&room->property("to_test").isNull())
		msleep(secs);
	room->throwIfStopRequested();
	// Frequent single-player surrender check: AI passes here each step. Outside single-player or without a signal, this costs one bool check.
	room->trySinglePlayerSurrender();
}
