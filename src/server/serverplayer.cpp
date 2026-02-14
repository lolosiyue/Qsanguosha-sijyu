#include "serverplayer.h"
//#include "skill.h"
#include "engine.h"
//#include "standard.h"
#include "maneuvering.h"
//#include "ai.h"
#include "settings.h"
#include "recorder.h"
#include "banpair.h"
//#include "lua-wrapper.h"
//#include "json.h"
#include "gamerule.h"
//#include "util.h"
#include "exppattern.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "socket.h"

using namespace QSanProtocol;

const int ServerPlayer::S_NUM_SEMAPHORES = 6;

ServerPlayer::ServerPlayer(Room *room)
	: Player(room), m_isClientResponseReady(false), m_isWaitingReply(false),
	socket(nullptr), room(room), ai(nullptr), trust_ai(new TrustAI(this)),
	recorder(nullptr), _m_phases_index(NotActive), next(nullptr)
{
	semas = new QSemaphore *[S_NUM_SEMAPHORES];
	for (int i = 0; i < S_NUM_SEMAPHORES; i++)
		semas[i] = new QSemaphore(0);
	onsole_owner = this;
}

ServerPlayer::~ServerPlayer()
{
	for (int i = 0; i < S_NUM_SEMAPHORES; i++)
		delete semas[i];
	delete[] semas;
	delete trust_ai;
}

/*void ServerPlayer::drawCard(const Card *card)
{
	handcards << card;
}*/

Room *ServerPlayer::getRoom() const
{
	return room;
}

void ServerPlayer::setOnsoleOwner(ServerPlayer *owner)
{
	if(owner){
		onsole_owner = owner;
		//room->setPlayerProperty(this,"onsole_owner",owner->objectName());
	}else{
		onsole_owner = this;
		//room->setPlayerProperty(this,"onsole_owner","");
	}
}

ServerPlayer *ServerPlayer::getOnsoleOwner() const
{
	return onsole_owner;
}

void ServerPlayer::broadcastSkillInvoke(const QString &card_name) const
{
	room->broadcastSkillInvoke(card_name, getGender()!=General::Female, -1);
}

void ServerPlayer::broadcastSkillInvoke(const Card *card) const
{
	if (card->isMute()) return;
	QString skill_name = card->getSkillName(false);
	if(skill_name.isEmpty()||skill_name.startsWith("_")){
		skill_name = card->getCommonEffectName();
		if (skill_name.isEmpty()) broadcastSkillInvoke(card->objectName());
		else room->broadcastSkillInvoke(skill_name, "common");
	}else{
		const Skill *skill = Sanguosha->getSkill(skill_name);
		if (skill) {
			int index = skill->getEffectIndex(this, card);
			if (index == 0) return;
			if ((index == -1 && skill->getSources().isEmpty()) || index == -2) {
				skill_name = card->getCommonEffectName();
				if (skill_name.isEmpty()) broadcastSkillInvoke(card->objectName());
				else room->broadcastSkillInvoke(skill_name, "common");
			} else
				room->broadcastSkillInvoke(skill_name, index, this);
		}else if(QFile::exists("audio/card/male/"+skill_name+".ogg"))
			broadcastSkillInvoke(skill_name);
		else
			broadcastSkillInvoke(card->objectName());
	}
}

void ServerPlayer::peiyin(const Skill *skill, int type)
{
	room->broadcastSkillInvoke(skill, type, this);
}

void ServerPlayer::peiyin(const QString &skillName, int type)
{
	room->broadcastSkillInvoke(skillName, type, this);
}

/*int ServerPlayer::getRandomHandCardId() const
{
	const Card * c = getRandomHandCard();
	if (c) return c->getEffectiveId();
	return -1;
}

const Card *ServerPlayer::getRandomHandCard() const
{
	if (handcards.isEmpty()) return nullptr;
	return handcards.at(qrand()%handcards.length());
}*/

void ServerPlayer::obtainCard(const Card *card, bool visible)
{
	room->obtainCard(this, card, CardMoveReason(CardMoveReason::S_REASON_GOTCARD, objectName()), visible);
}

void ServerPlayer::throwAllEquips(const QString &reason)
{
	QList<int>ids;
	foreach (const Card *equip, getEquips())
		if (!isJilei(equip)) ids << equip->getId();
	room->throwCard(ids, reason, this);
}

void ServerPlayer::throwAllHandCards(const QString &reason)
{
	int card_length = getHandcardNum();
	room->askForDiscard(this, reason, card_length, card_length);
}

void ServerPlayer::throwAllHandCardsAndEquips(const QString &reason)
{
	int card_length = getCardCount();
	room->askForDiscard(this, reason, card_length, card_length, false, true);
}

void ServerPlayer::throwAllMarks(bool visible_only)
{
	// throw all marks
	foreach (QString m, marks.keys()) {
		if(m=="@bossExp"||m.endsWith("-Keep")) continue;
		if(visible_only){
			if(m.startsWith("@")||m.startsWith("&")){
			}else continue;
		}
		room->setPlayerMark(this, m, 0);
	}
}

void ServerPlayer::clearOnePrivatePile(const QString &pile_name)
{
	if (piles.contains(pile_name))
		room->throwCard(piles[pile_name], CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, objectName()), nullptr);
}

void ServerPlayer::clearPrivatePiles()
{
	foreach(QString pile_name, piles.keys())
		clearOnePrivatePile(pile_name);
}

void ServerPlayer::bury()
{
	room->setPlayerFlag(this,".");
	room->addPlayerHistory(this,".");
	throwAllCards("bury");
	throwAllMarks();
	clearPrivatePiles();

	room->clearPlayerCardLimitation(this, false);
}

void ServerPlayer::throwAllCards(const QString &reason)
{
	room->throwCard(handCards()+getEquipsId(), reason, this);
	room->throwCard(getJudgingAreaID(), CardMoveReason(CardMoveReason::S_REASON_THROW, objectName(), reason, ""), nullptr);
}

void ServerPlayer::drawCards(int n, const QString &reason, bool isTop, bool visible)
{
	room->drawCards(this, n, reason, isTop, visible);
}

QList<int> ServerPlayer::drawCardsList(int n, const QString &reason, bool isTop, bool visible)
{
	return room->drawCardsList(this, n, reason, isTop, visible);
}

bool ServerPlayer::askForSkillInvoke(const QString &skill_name, const QVariant &data, bool notify)
{
	return room->askForSkillInvoke(this, skill_name, data, notify);
}

bool ServerPlayer::askForSkillInvoke(const Skill *skill, const QVariant &data, bool notify)
{
	//Q_ASSERT(skill != nullptr);
	return skill&&askForSkillInvoke(skill->objectName(), data, notify);
}

bool ServerPlayer::askForSkillInvoke(const QString &skill_name, ServerPlayer *player, bool notify)
{
	return askForSkillInvoke(skill_name, QVariant::fromValue(player), notify);
}

bool ServerPlayer::askForSkillInvoke(const Skill *skill, ServerPlayer *player, bool notify)
{
	//Q_ASSERT(skill != nullptr);
	return skill&&askForSkillInvoke(skill->objectName(), player, notify);
}

QList<int> ServerPlayer::forceToDiscard(int discard_num, bool include_equip, bool is_discard, const QString &pattern)
{
	QList<const Card *> all_cards = getHandcards();
	if (include_equip) all_cards << getEquips();
	qShuffle(all_cards);

	QList<int> to_discard;
	ExpPattern exp_pattern(pattern);
	foreach (const Card *c, all_cards){
		if(exp_pattern.match(this, c)){
			if(is_discard&&isJilei(c)) continue;
			to_discard << c->getId();
			if (to_discard.length() >= discard_num)
				break;
		}
	}
	return to_discard;
}

int ServerPlayer::aliveCount() const
{
	return room->alivePlayerCount();
}

/*int ServerPlayer::getHandcardNum() const
{
	return handcards.size();
}*/

void ServerPlayer::setSocket(ClientSocket *socket)
{
	if (socket) {
		connect(socket, SIGNAL(disconnected()), this, SIGNAL(disconnected()));
		connect(socket, SIGNAL(message_got(const char *)), this, SLOT(getMessage(const char *)));
		connect(this, SIGNAL(message_ready(QString)), this, SLOT(sendMessage(QString)));
	} else {
		if (this->socket) {
			this->disconnect(this->socket);
			this->socket->disconnect(this);
			this->socket->disconnectFromHost();
			this->socket->deleteLater();
		}

		disconnect(this, SLOT(sendMessage(QString)));
	}

	this->socket = socket;
}

void ServerPlayer::kick()
{
	room->notifyProperty(this, this, "flags", "is_kicked");
	if (socket != nullptr) socket->disconnectFromHost();
	setSocket(nullptr);
}

void ServerPlayer::getMessage(const char *message)
{
	QString request = message;
	if (request.endsWith("\n"))
		request.chop(1);

	emit request_got(request);
}

void ServerPlayer::unicast(const QString &message)
{
	emit message_ready(message);

	if (recorder)
		recorder->recordLine(message);
}

void ServerPlayer::startNetworkDelayTest()
{
	test_time = QDateTime::currentDateTime();
	Packet packet(S_SRC_ROOM | S_TYPE_NOTIFICATION | S_DEST_CLIENT, S_COMMAND_NETWORK_DELAY_TEST);
	invoke(&packet);
}

qint64 ServerPlayer::endNetworkDelayTest()
{
	return test_time.msecsTo(QDateTime::currentDateTime());
}

void ServerPlayer::startRecord()
{
	recorder = new Recorder(this);
}

void ServerPlayer::saveRecord(const QString &filename)
{
	if (recorder)
		recorder->save(filename);
}

void ServerPlayer::addToSelected(const QString &general)
{
	selected.append(general);
}

QStringList ServerPlayer::getSelected() const
{
	return selected;
}

QString ServerPlayer::findReasonable(const QStringList &generals, bool no_unreasonable)
{
	QStringList ban_list;
	if (Config.EnableBasara)
		ban_list << Config.value("Banlist/Basara").toStringList();
	if (Config.GameMode == "zombie_mode")
		ban_list << Config.value("Banlist/Zombie").toStringList();
	if (Config.GameMode.endsWith("p")
		|| Config.GameMode.endsWith("pd")
		|| Config.GameMode.endsWith("pz")
		|| Config.GameMode.contains("_mini_")
		|| Config.GameMode == "custom_scenario") {
		ban_list << Config.value("Banlist/Roles").toStringList();
	}
	foreach (QString name, generals) {
		if (Config.Enable2ndGeneral) {
			if (getGeneral()) {
				if (!BanPair::isBanned(getGeneralName()) && BanPair::isBanned(getGeneralName(), name)) continue;
				if (Config.EnableHegemony && getGeneral()->getKingdom() != Sanguosha->getGeneral(name)->getKingdom())
					continue;
			} else if(BanPair::isBanned(name)) continue;
		}
		if (ban_list.contains(name)) continue;
		return name;
	}

	if (no_unreasonable)
		return "";

	return generals.first();
}

void ServerPlayer::clearSelected()
{
	selected.clear();
}

void ServerPlayer::sendMessage(const QString &message)
{
	if (socket) {
#ifndef QT_NO_DEBUG
		printf("%s", qPrintable(objectName()));
#endif
#ifdef LOGNETWORK
		emit Sanguosha->logNetworkMessage("send "+this->objectName()+":"+message);
#endif
		socket->send(message);
	}
}

void ServerPlayer::invoke(const AbstractPacket *packet)
{
	unicast(packet->toString());
}

QString ServerPlayer::reportHeader() const
{
	return QString("%1 ").arg(objectName().isEmpty()?tr("Anonymous"):objectName());
}

void ServerPlayer::removeCard(int id, Place place)
{
	if(place==PlaceEquip)
		qobject_cast<const EquipCard *>(Sanguosha->getCard(id)->getRealCard())->onUninstall(this);
	Player::removeCard(id, place);
	/*switch (place) {
	case PlaceHand: {
		handcards.removeAll(card);
		break;
	}case PlaceEquip: {
		WrappedCard *wrapped = Sanguosha->getWrappedCard(card->getEffectiveId());
		const EquipCard *equip = qobject_cast<const EquipCard *>(wrapped->getRealCard());
		if (!equip) equip = qobject_cast<const EquipCard *>(wrapped);
			//equip = qobject_cast<const EquipCard *>(Sanguosha->getEngineCard(card->getEffectiveId()));
		//Q_ASSERT(equip != nullptr);
		equip->onUninstall(this);
		removeEquip(wrapped);

		LogMessage log;
		log.type = "#Uninstall";
		log.card_str = wrapped->toString();
		log.from = this;
		room->sendLog(log);
		break;
	}case PlaceDelayedTrick: {
		removeDelayedTrick(card);
		break;
	}case PlaceSpecial: {
		int card_id = card->getEffectiveId();
		QString pile_name = getPileName(card_id);

		//@todo: sanity check required
		if (!pile_name.isEmpty())
			piles[pile_name].removeOne(card_id);

		break;
	}default:
		break;
	}*/
}

void ServerPlayer::addCard(int id, Place place)
{
	Player::addCard(id, place);
	if(place==PlaceEquip)
		qobject_cast<const EquipCard *>(Sanguosha->getCard(id)->getRealCard())->onInstall(this);
	/*switch (place) {
	case PlaceHand: {
		handcards << card;
		break;
	}case PlaceEquip: {
		WrappedCard *wrapped = Sanguosha->getWrappedCard(card->getEffectiveId());
		const EquipCard *equip = qobject_cast<const EquipCard *>(wrapped->getRealCard());
		setEquip(wrapped);
		equip->onInstall(this);
		break;
	}case PlaceDelayedTrick: {
		addDelayedTrick(card);
		break;
	}default:
		break;
	}*/
}

/*bool ServerPlayer::isLastHandCard(const Card *card, bool contain) const
{
	if(card->isVirtualCard()){
		QList<int> ids = card->getSubcards();
		if(ids.length()>0){
			if (contain) {
				foreach (const Card *h, handcards) {
					if (!ids.contains(h->getId()))
						return false;
				}
				return true;
			} else if(ids.length()>=handcards.length()){
				foreach (int id, ids) {
					if (!handcards.contains(Sanguosha->getCard(id)))
						return false;
				}
				return true;
			}
		}
	}else if(handcards.length() == 1)
		return handcards.contains(card);
	return false;
}

QList<const Card *> ServerPlayer::getHandcards() const
{
	return handcards;
}*/

QList<const Card *> ServerPlayer::getCards(const QString &flags) const
{
	QList<const Card *> cards;
	if (flags.contains("h"))
		cards << getHandcards();
	if (flags.contains("e"))
		cards << getEquips();
	if (flags.contains("j"))
		cards << getJudgingArea();

	return cards;
}

DummyCard *ServerPlayer::wholeHandCards() const
{
	return dummyCard(handCards());
}

QList<int> ServerPlayer::getHandPile() const
{
	QList<int> handpile = Player::getHandPile();
	if (tag["TaoxiHere"].toBool()) {
		bool ok = false;
		int id = tag.value("TaoxiId").toInt(&ok);
		if (ok) handpile << id;
	}
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if (p!=this&&getMark("&bshaoshi+#"+p->objectName())>0)
			handpile << p->handCards();
	}
	return handpile;
}

bool ServerPlayer::hasNullification() const
{
	foreach (const Card *card, getHandcards()) {
		if (card->objectName() == "nullification")
			return true;
	}
	foreach (int id, getHandPile()) {
		if (Sanguosha->getCard(id)->objectName() == "nullification")
			return true;
	}
	foreach (const Skill *skill, getVisibleSkillList(true)) {
		if (skill->inherits("ViewAsSkill")) {
			const ViewAsSkill *vsskill = qobject_cast<const ViewAsSkill *>(skill);
			if(hasSkill(skill->objectName())&&vsskill->isEnabledAtResponse(this,"nullification")) return true;
			//if (vsskill->isEnabledAtNullification(this)) return true;
		} else if (skill->inherits("TriggerSkill")) {
			const ViewAsSkill *vsskill = qobject_cast<const TriggerSkill *>(skill)->getViewAsSkill();
			if(vsskill&&hasSkill(skill->objectName())&&vsskill->isEnabledAtResponse(this,"nullification")) return true;
			//if (vsskill && vsskill->isEnabledAtNullification(this)) return true;
		}
	}
	return false;
}

bool ServerPlayer::pindian(ServerPlayer *target, const QString &reason, const Card *card1)
{
	PindianStruct *pindian_struct = PinDian(target, reason, card1);
	if (pindian_struct) return pindian_struct->success;
	return false;
}

int ServerPlayer::pindianInt(ServerPlayer *target, const QString &reason, const Card *card1)
{
	PindianStruct *pindian_struct = PinDian(target, reason, card1);
	if (pindian_struct->success) return 1;
	else if (pindian_struct->from_number == pindian_struct->to_number) return 0;
	else if (pindian_struct->from_number < pindian_struct->to_number) return -1;
	return -2;
}

PindianStruct *ServerPlayer::PinDian(ServerPlayer *target, const QString &reason, const Card *card1)
{
	//Q_ASSERT(canPindian(target, false));

	LogMessage log;
	log.type = "#Pindian";
	log.from = this;
	log.to << target;
	room->sendLog(log);

	PindianStruct *pindian_struct = new PindianStruct;
	pindian_struct->from = this;
	pindian_struct->to = target;
	pindian_struct->from_card = card1;
	pindian_struct->to_card = nullptr;
	pindian_struct->reason = reason;

	QVariant data = QVariant::fromValue(pindian_struct);
	room->getThread()->trigger(AskforPindianCard, room, this, data);
	pindian_struct = data.value<PindianStruct *>();

	if (!pindian_struct->from_card && !pindian_struct->to_card) {
		QList<const Card *> cards = room->askForPindianRace(this, target, reason);
		pindian_struct->from_card = cards.first();
		pindian_struct->to_card = cards.last();
	} else if (!pindian_struct->to_card)
		pindian_struct->to_card = room->askForPindian(target, this, reason);
	else if (!pindian_struct->from_card)
		pindian_struct->from_card = room->askForPindian(this, this, reason);

	if (!pindian_struct->from_card || !pindian_struct->to_card) return nullptr;

	pindian_struct->from_number = pindian_struct->from_card->getNumber();
	pindian_struct->to_number = pindian_struct->to_card->getNumber();

	CardsMoveStruct move1;
	move1.card_ids << pindian_struct->from_card->getEffectiveId();
	move1.from = room->getCardOwner(pindian_struct->from_card->getEffectiveId());
	move1.to = nullptr;
	move1.to_place = PlaceTable;
	move1.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->from->objectName(), pindian_struct->to->objectName(), reason, "");

	CardsMoveStruct move2;
	move2.card_ids << pindian_struct->to_card->getEffectiveId();
	move2.from = room->getCardOwner(pindian_struct->to_card->getEffectiveId());
	move2.to = nullptr;
	move2.to_place = PlaceTable;
	move2.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->to->objectName(), reason, "");

	log.type = "$PindianResult";
	log.from = pindian_struct->from;
	log.card_str = QString::number(pindian_struct->from_card->getEffectiveId());
	room->sendLog(log);

	log.from = pindian_struct->to;
	log.card_str = QString::number(pindian_struct->to_card->getEffectiveId());
	room->sendLog(log);

	QList<CardsMoveStruct> moves;
	moves << move1 << move2;
	room->moveCardsAtomic(moves, true);

	data.setValue(pindian_struct);
	room->getThread()->trigger(PindianVerifying, room, this, data);
	pindian_struct = data.value<PindianStruct *>();

	pindian_struct->success = pindian_struct->from_number > pindian_struct->to_number;

	log.type = pindian_struct->success ? "#PindianSuccess" : "#PindianFailure";
	log.from = this;
	room->sendLog(log);

	JsonArray arg;
	arg << S_GAME_EVENT_REVEAL_PINDIAN << objectName() << pindian_struct->from_card->getEffectiveId()
		<< target->objectName() << pindian_struct->to_card->getEffectiveId() << pindian_struct->success << reason;
	room->doBroadcastNotify(S_COMMAND_LOG_EVENT, arg);

	data.setValue(pindian_struct);
	room->getThread()->trigger(Pindian, room, this, data);

	moves.clear();
	if (room->getCardPlace(pindian_struct->from_card->getEffectiveId()) == PlaceTable) {
		CardsMoveStruct move1;
		move1.card_ids << pindian_struct->from_card->getEffectiveId();
		move1.from = pindian_struct->from;
		move1.to = nullptr;
		move1.to_place = DiscardPile;
		move1.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->from->objectName(), pindian_struct->to->objectName(), reason, "");
		moves.append(move1);
	}

	if (room->getCardPlace(pindian_struct->to_card->getEffectiveId()) == PlaceTable) {
		if (pindian_struct->to_card->getEffectiveId() != pindian_struct->from_card->getEffectiveId()) {
			CardsMoveStruct move2;
			move2.card_ids << pindian_struct->to_card->getEffectiveId();
			move2.from = pindian_struct->to;
			move2.to = nullptr;
			move2.to_place = DiscardPile;
			move2.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->to->objectName(), reason, "");
			moves.append(move2);
		}
	}
	room->moveCardsAtomic(moves, true);

	data = QString("pindian:%1:%2:%3:%4:%5").arg(reason).arg(objectName()).arg(pindian_struct->from_card->getEffectiveId())
		.arg(target->objectName()).arg(pindian_struct->to_card->getEffectiveId());
	room->getThread()->trigger(ChoiceMade, room, this, data);

	return pindian_struct;
}

void ServerPlayer::turnOver()
{
	if (room->getThread()->trigger(TurnOver, room, this)) return;
	setFaceUp(!faceUp());
	room->broadcastProperty(this, "faceup");

	LogMessage log;
	log.type = "#TurnOver";
	log.from = this;
	log.arg = faceUp() ? "face_up" : "face_down";
	room->sendLog(log);

	room->getThread()->trigger(TurnedOver, room, this);
}

bool ServerPlayer::changePhase(Phase from, Phase to)
{
	setPhase(PhaseNone);

	PhaseChangeStruct phase_change;
	phase_change.from = from;
	phase_change.to = to;
	QVariant data = QVariant::fromValue(phase_change);

	bool skip = room->getThread()->trigger(EventPhaseChanging, room, this, data);
	phase_change = data.value<PhaseChangeStruct>();

	setPhase(phase_change.to);
	if(phase_change.to == NotActive){
		room->broadcastProperty(this, "phase");
		room->getThread()->trigger(EventPhaseStart, room, this);
		return false;
	}
	//if (!phases.isEmpty()) phases.removeFirst();
	if (skip) {
		setPhase(from);
		return true;
	}
	room->broadcastProperty(this, "phase");

	if (!room->getThread()->trigger(EventPhaseStart, room, this, data))
		room->getThread()->trigger(EventPhaseProceeding, room, this, data);
	room->getThread()->trigger(EventPhaseEnd, room, this, data);
	return false;
}

void ServerPlayer::play(QList<Phase> set_phases)
{
	static QList<Phase> all_phases;
	if(all_phases.isEmpty()) all_phases << RoundStart << Start << Judge << Draw << Play << Discard << Finish;
	if (set_phases.isEmpty()) {
		if(getMark("@extra_turn")>0){
			foreach (QVariant v, tag["extraTurnPhases"].toList())
				set_phases << Phase(v.toInt());
		}else
			set_phases = all_phases;
		room->setTag("Global_AllTurnsNum",room->getTag("Global_AllTurnsNum").toInt()+1);
	}
	if (!set_phases.contains(NotActive))
		set_phases << NotActive;

	room->setPlayerFlag(this,"CurrentPlayer");

	_m_phases_state.clear();
	static QStringList phase_names;
	if(phase_names.isEmpty()) phase_names << "roundstart" << "start" << "judge" << "draw" << "play" << "discard" << "finish";
	foreach (Phase pha, set_phases) {
		int n = all_phases.indexOf(pha);
		if (n>=0&&getMark("LostPlayerPhase_"+phase_names.at(n))>0)
			set_phases.removeOne(pha);
		else{
			PhaseStruct _phase;
			_phase.phase = pha;
			_m_phases_state << _phase;
		}
	}
	phases = set_phases;

	PhaseChangeStruct phase_change;
	for (int i = 0; i < _m_phases_state.length(); i++) {
		if (isDead()) {
			changePhase(getPhase(), NotActive);
			break;
		}
		phase_change.from = getPhase();
		phase_change.to = phases[i];
		_m_phases_index = i;
		setPhase(PhaseNone);
		QVariant data = QVariant::fromValue(phase_change);
		bool skip = room->getThread()->trigger(EventPhaseChanging, room, this, data);
		_m_phases_state[i].phase = phases[i] = data.value<PhaseChangeStruct>().to;
		setPhase(phases[i]);
		room->broadcastProperty(this, "phase");
		if(phases[i] == NotActive){
			room->getThread()->trigger(EventPhaseStart, room, this, data);
			break;
		}
		if (skip || _m_phases_state[i].skipped != 0) {
			data = _m_phases_state[i].skipped < 0;
			if (!room->getThread()->trigger(EventPhaseSkipping, room, this, data)) {
				room->getThread()->trigger(EventPhaseSkipped, room, this, data);
				//Sanguosha->playSystemAudioEffect("skip");
				continue;
			}
			data = QVariant::fromValue(phase_change);
		}
		if (!room->getThread()->trigger(EventPhaseStart, room, this, data))
			room->getThread()->trigger(EventPhaseProceeding, room, this, data);
		room->getThread()->trigger(EventPhaseEnd, room, this, data);/*
		if (phases[i] != NotActive && (skip || _m_phases_state[i].skipped != 0)) {
			data = QVariant::fromValue(_m_phases_state[i].skipped < 0);
			if (!room->getThread()->trigger(EventPhaseSkipping, room, this, data)) {
				room->getThread()->trigger(EventPhaseSkipped, room, this);
				continue;
			}
		}
		skip = room->getThread()->trigger(EventPhaseStart, room, this);
		if (getPhase() == NotActive) break;
		if (!skip) room->getThread()->trigger(EventPhaseProceeding, room, this);
		room->getThread()->trigger(EventPhaseEnd, room, this);*/
	}
}

QList<Player::Phase> &ServerPlayer::getPhases()
{
	return phases;
}

void ServerPlayer::skip(Phase phase, bool isCost)
{
	static QStringList phase_strings;
	if (phase_strings.isEmpty())
		phase_strings << "round_start" << "start" << "judge" << "draw" << "play" << "discard" << "finish" << "not_active";
	for (int i = _m_phases_index; i < _m_phases_state.size(); i++) {
		if (_m_phases_state[i].phase == phase) {
			if (_m_phases_state[i].skipped != 0) {
				if (isCost && _m_phases_state[i].skipped == 1)
					_m_phases_state[i].skipped = -1;
				return;
			}
			_m_phases_state[i].skipped = (isCost ? -1 : 1);
			LogMessage log;
			log.type = "#SkipPhase";
			log.from = this;
			log.arg = phase_strings.at(phase);
			room->sendLog(log);
			break;
		}
	}
}

void ServerPlayer::insertPhase(Phase phase)
{
	PhaseStruct _phase;
	_phase.phase = phase;
	_m_phases_state.insert(_m_phases_index+1, _phase);
	phases.insert(_m_phases_index+1, phase);
}

bool ServerPlayer::isSkipped(Phase phase) const
{
	for (int i = _m_phases_index; i < _m_phases_state.size(); i++) {
		if (_m_phases_state[i].phase == phase)
			return _m_phases_state[i].skipped != 0;
	}
	return false;
}

void ServerPlayer::gainMark(const QString &mark, int n)
{
	if (n == 0) return;
	int value = getMark(mark) + n;

	LogMessage log;
	log.type = "#GetMark";
	log.from = this;
	log.arg = mark;
	if (mark.startsWith("&"))
		log.arg = log.arg.mid(1);
	if (log.arg.contains("+"))
		log.arg = log.arg.split("+").first();
	log.arg2 = QString::number(n);
	room->sendLog(log);
	room->setPlayerMark(this, mark, value);
}

void ServerPlayer::loseMark(const QString &mark, int n)
{
	if (n == 0 || getMark(mark) == 0) return;
	int value = getMark(mark) - n;
	if (value < 0) {
		value = 0;
		n = getMark(mark);
	}

	QString new_mark = mark;
	if (mark.startsWith("&"))
		new_mark = new_mark.mid(1);
	if (new_mark.contains("+"))
		new_mark = new_mark.split("+").at(0);

	LogMessage log;
	log.type = "#LoseMark";
	log.from = this;
	log.arg = new_mark;
	log.arg2 = QString::number(n);
	room->sendLog(log);
	room->setPlayerMark(this, mark, value);
}

void ServerPlayer::loseAllMarks(const QString &mark_name)
{
	loseMark(mark_name, getMark(mark_name));
}

void ServerPlayer::gainHujia(int n, int max_num)
{
	if (n <= 0) return;

	if (max_num > 0) {
		int hujia = getHujia();
		if (hujia >= max_num) return;
		n = qMin(n,max_num-hujia);
	}

	QVariant data = n;
	if (room->getThread()->trigger(GainHujia, room, this, data)) return;

	n = data.toInt();
	if (n <= 0) return;

	LogMessage log;
	log.type = "#GetHujia";
	log.from = this;
	log.arg = QString::number(n);

	room->sendLog(log);
	room->setPlayerMark(this, "@HuJia", getHujia()+n);

	room->getThread()->trigger(GainedHujia, room, this, data);
}

void ServerPlayer::loseHujia(int n)
{
	if (n<1||getHujia()<1) return;

	QVariant data = n;
	if (room->getThread()->trigger(LoseHujia, room, this, data)) return;

	n = data.toInt();
	if (n <= 0) return;

	n = qMin(n,getHujia());

	LogMessage log;
	log.type = "#LoseHuJia";
	log.from = this;
	log.arg = QString::number(n);

	room->sendLog(log);
	room->setPlayerMark(this, "@HuJia", getHujia()-n);

	room->getThread()->trigger(LostHujia, room, this, data);
}

void ServerPlayer::loseAllHujias()
{
	loseHujia(getHujia());
}

void ServerPlayer::addSkill(const QString &skill_name)
{
	if(room->getMode() == "03_1v2"){
		const Skill *skill = Sanguosha->getMainSkill(skill_name);
		if (skill && skill->isLordSkill()) return;
	}

	Player::addSkill(skill_name);
	JsonArray args;
	args << (int)QSanProtocol::S_GAME_EVENT_ADD_SKILL << objectName() << skill_name;
	room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
}

void ServerPlayer::loseSkill(const QString &skill_name)
{
	Player::loseSkill(skill_name);
	JsonArray args;
	args << (int)QSanProtocol::S_GAME_EVENT_LOSE_SKILL << objectName() << skill_name;
	room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
}

void ServerPlayer::setGender(General::Gender gender)
{
	if (gender == getGender())
		return;
	Player::setGender(gender);
	JsonArray args;
	args << (int)QSanProtocol::S_GAME_EVENT_CHANGE_GENDER << objectName() << (int)gender;
	room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
}

bool ServerPlayer::isOnline() const
{
	return getState() == "online";
}

void ServerPlayer::setAI(AI *ai)
{
	this->ai = ai;
}

AI *ServerPlayer::getAI() const
{
	if (getState()=="online"||onsole_owner->getState()=="online")
		return nullptr;
	else if (Config.EnableCheat||getState()!="trust")
		return ai;
	return trust_ai;
}

AI *ServerPlayer::getSmartAI() const
{
	return ai;
}

void ServerPlayer::addVictim(ServerPlayer *victim)
{
	victims.append(victim);
}

QList<ServerPlayer *> ServerPlayer::getVictims() const
{
	return victims;
}

void ServerPlayer::setNext(ServerPlayer *next)
{
	this->next = next;
}

ServerPlayer *ServerPlayer::getNext() const
{
	return next;
}

ServerPlayer *ServerPlayer::getNextAlive(int n) const
{
	ServerPlayer *next = const_cast<ServerPlayer *>(this);
	if (room->getAlivePlayers().length()<2) return next;
	for (int i = 0; i < n; i++) {
		do next = next->next; while (next->isDead());
	}
	return next;
}

int ServerPlayer::getGeneralMaxHp() const
{
	int max_hp = getGeneral()->getMaxHp();

	if (getGeneral2()){
		int plan = Config.MaxHpScheme;
		if (Config.GameMode.contains("_mini_") || Config.GameMode == "custom_scenario") plan = 1;
		int second = getGeneral2()->getMaxHp();

		switch (plan) {
		case 3: max_hp = (max_hp + second) / 2; break;
		case 2: max_hp = qMax(max_hp, second); break;
		case 1: max_hp = qMin(max_hp, second); break;
		default:
			max_hp += second - Config.Scheme0Subtraction; break;
		}
		max_hp = qMax(max_hp, 1);
	}

	if (room->hasWelfare(this))
		max_hp++;

	return max_hp;
}

int ServerPlayer::getGeneralStartHp() const
{
	int start_hp = getGeneral()->getStartHp();

	if (getGeneral2()){
		int plan = Config.MaxHpScheme;
		if (Config.GameMode.contains("_mini_") || Config.GameMode == "custom_scenario") plan = 1;
		int second = getGeneral2()->getStartHp();

		switch (plan) {
		case 3: start_hp = (start_hp + second) / 2; break;
		case 2: start_hp = qMax(start_hp, second); break;
		case 1: start_hp = qMin(start_hp, second); break;
		default:
			start_hp += second - Config.Scheme0Subtraction; break;
		}

		start_hp = qMax(start_hp, 1);
	}

	if (room->hasWelfare(this))
		start_hp++;

	return start_hp;
}

int ServerPlayer::getGeneralStartHujia() const
{
	int start_hujia = getGeneral()->getStartHujia();
	if (getGeneral2())
		start_hujia += getGeneral2()->getStartHujia();
	return start_hujia;
}

QString ServerPlayer::getGameMode() const
{
	return room->getMode();
}

QString ServerPlayer::getIp() const
{
	if (socket)
		return socket->peerAddress();
	return "";
}

void ServerPlayer::introduceTo(ServerPlayer *player)
{
	JsonArray introduce_str;/*
	QString screen_name = QString("[%1]%2").arg(getPlayerSeat()).arg(screenName());
	screen_name = screen_name.toUtf8().toBase64();*/
	QString screen_name = screenName().toUtf8().toBase64();
	introduce_str << objectName() << screen_name << property("avatar").toString();

	if (player)
		room->doNotify(player, S_COMMAND_ADD_PLAYER, introduce_str);
	else {
		QList<ServerPlayer *> players = room->getPlayers();
		players.removeOne(this);
		room->doBroadcastNotify(players, S_COMMAND_ADD_PLAYER, introduce_str);
	}
}

void ServerPlayer::marshal(ServerPlayer *player) const
{
	room->notifyProperty(player, this, "maxhp");
	room->notifyProperty(player, this, "hp");
	room->notifyProperty(player, this, "gender");
	room->notifyProperty(player, this, "player_seat");

	//if (getKingdom() != getGeneral()->getKingdom())
		room->notifyProperty(player, this, "kingdom");

	if (isAlive()) {
		room->notifyProperty(player, this, "seat");
		if (getPhase() != NotActive)
			room->notifyProperty(player, this, "phase");
	} else {
		room->notifyProperty(player, this, "alive");
		room->notifyProperty(player, this, "role");
		room->doNotify(player, S_COMMAND_KILL_PLAYER, objectName());
	}

	if (!faceUp())
		room->notifyProperty(player, this, "faceup");

	if (isChained())
		room->notifyProperty(player, this, "chained");

	foreach(const char *property_name, propertys)
		room->notifyProperty(player, this, property_name);

	QList<CardsMoveStruct> moves;

	CardsMoveStruct move;
	move.to = (Player*)this;
	move.to_player_name = objectName();
	move.from_place = DrawPile;
	move.card_ids = handCards();
	if (move.card_ids.length()>0) {
		if (player == this) {
			foreach (int id, move.card_ids) {
				WrappedCard *wrapped = qobject_cast<WrappedCard *>(Sanguosha->getCard(id));
				if (wrapped&&wrapped->isModified())
					room->notifyUpdateCard(player, id, wrapped);
			}
		}
		move.to_place = PlaceHand;
		moves << move;
	}

	move.card_ids = getEquipsId();
	if (move.card_ids.length()>0) {
		foreach (int id, move.card_ids) {
			WrappedCard *wrapped = qobject_cast<WrappedCard *>(Sanguosha->getCard(id));
			if (wrapped&&wrapped->isModified())
				room->notifyUpdateCard(player, id, wrapped);
		}
		move.to_place = PlaceEquip;
		moves << move;
	}

	move.card_ids = getJudgingAreaID();
	if (move.card_ids.length()>0) {
		move.to_place = PlaceDelayedTrick;
		moves << move;
	}

	if (piles.keys().length()>0) {
		move.to_place = PlaceSpecial;
		foreach (QString pile, piles.keys()) {
			move.card_ids = piles[pile];
			move.to_pile_name = pile;
			moves << move;
		}
	}

	if (moves.length()>0) {
		QList<ServerPlayer*> players;
		players << player;
		room->notifyMoveCards(true, moves, false, players);
		room->notifyMoveCards(false, moves, false, players);
	}

	foreach (QString mark_name, marks.keys()) {
		if (mark_name.startsWith("@") || mark_name.startsWith("&")) {
			JsonArray arg;
			arg << objectName() << mark_name << getMark(mark_name);
			room->doNotify(player, S_COMMAND_SET_MARK, arg);
		}
	}

	foreach (const Skill *skill, getVisibleSkillList(true)) {
		JsonArray args1;
		args1 << S_GAME_EVENT_ACQUIRE_SKILL << objectName() << skill->objectName();
		room->doNotify(player, S_COMMAND_LOG_EVENT, args1);
	}

	foreach(QString flag, flags)
		room->notifyProperty(player, this, "flags", flag);

	foreach (QString item, history.keys()) {
		JsonArray arg;
		arg << item << history.value(item);
		room->doNotify(player, S_COMMAND_ADD_HISTORY, arg);
	}

	/*if (hasShownRole())
		room->notifyProperty(player, this, "role");*/
}

void ServerPlayer::addToPile(const QString &pile_name, const Card *card, bool open, QList<ServerPlayer *> open_players)
{
	QList<int> card_ids;
	if (card->isVirtualCard())
		card_ids = card->getSubcards();
	else card_ids << card->getId();
	return addToPile(pile_name, card_ids, open, open_players);
}

void ServerPlayer::addToPile(const QString &pile_name, int card_id, bool open, QList<ServerPlayer *> open_players)
{
	return addToPile(pile_name, QList<int>()<<card_id, open, open_players);
}

void ServerPlayer::addToPile(const QString &pile_name, QList<int> card_ids, bool open, QList<ServerPlayer *> open_players)
{
	return addToPile(pile_name, card_ids, open, open_players, CardMoveReason());
}

void ServerPlayer::addToPile(const QString &pile_name, QList<int> card_ids,
	bool open, QList<ServerPlayer *> open_players, CardMoveReason reason)
{
	if(card_ids.isEmpty()) return;
	if(open)
		open_players = room->getAlivePlayers();
	else{
		setPileOpen(pile_name, ".");
		if (open_players.isEmpty()) {
			foreach (int id, card_ids) {
				ServerPlayer *owner = room->getCardOwner(id);
				if (owner && !open_players.contains(owner))
					open_players << owner;
			}
		}
	}
	foreach(ServerPlayer *p, open_players)
		setPileOpen(pile_name, p->objectName());
	piles[pile_name].append(card_ids);

	CardsMoveStruct move;
	move.card_ids = card_ids;
	move.to = this;
	move.to_place = PlaceSpecial;
	move.reason = reason;
	move.to_pile_name = pile_name;
	room->moveCardsAtomic(move, open);
}

void ServerPlayer::addToRenPile(const Card *card, const QString &skill_name)
{
	QList<int> card_ids;
	if (card->isVirtualCard())
		card_ids = card->getSubcards();
	else card_ids << card->getId();
	return addToRenPile(card_ids, skill_name);
}

void ServerPlayer::addToRenPile(int card_id, const QString &skill_name)
{
	return addToRenPile(QList<int>()<<card_id, skill_name);
}

void ServerPlayer::addToRenPile(QList<int> card_ids, const QString &skill_name)
{
	if(card_ids.isEmpty()) return;

	CardsMoveStruct move1;
	move1.to_place = DiscardPile;
	move1.from_pile_name = "ren_pile";
	move1.reason = CardMoveReason(CardMoveReason::S_REASON_RULEDISCARD, objectName(),skill_name,"removeRenPile");
	QVariantList ren = room->getTag("ren_pile").toList();
	foreach (int id, card_ids) {
		if(ren.length()>=6){
			move1.card_ids << ren.takeFirst().toInt();
			card_ids.removeAll(move1.card_ids.last());
		}
		ren << id;
	}
	//room->setTag("ren_pile",ren);
	CardsMoveStruct move2;
	move2.card_ids = card_ids;
	move2.to_place = PlaceTable;
	move2.reason = CardMoveReason(CardMoveReason::S_REASON_RECYCLE, objectName(),skill_name,"addRenPile");
	move2.to_pile_name = "ren_pile";
	QList<CardsMoveStruct> moves;
	moves << move1 << move2;
	room->moveCardsAtomic(moves, true);
}

void ServerPlayer::exchangeFreelyFromPrivatePile(const QString &skill_name, const QString &pile_name, int upperlimit, bool include_equip)
{
	QList<int> pile = getPile(pile_name);
	if (pile.isEmpty()) return;

	QString tempMovingFlag = QString("%1_InTempMoving").arg(skill_name);
	room->setPlayerFlag(this, tempMovingFlag);

	int ai_delay = Config.AIDelay;
	Config.AIDelay = 0;

	QList<int> will_to_handcard;
	room->fillAG(pile, this);
	while (!pile.isEmpty()) {
		int card_id = room->askForAG(this, pile, true, skill_name);
		if (card_id == -1) break;
		room->takeAG(this,card_id,false,QList<ServerPlayer*>()<<this);

		pile.removeOne(card_id);
		will_to_handcard << card_id;
		if (pile.length() >= upperlimit) break;
	}
	room->clearAG(this);
	Config.AIDelay = ai_delay;

	int n = will_to_handcard.length();
	if (n == 0) return;
	room->obtainCard(this, dummyCard(will_to_handcard), CardMoveReason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, objectName()), false);
	const Card *exchange_card = room->askForExchange(this, skill_name, n, n, include_equip);

	QList<int> duplicate, will_to_handcard_x = will_to_handcard, will_to_pile_x = exchange_card->getSubcards();
	foreach (int id, exchange_card->getSubcards()) {
		if (will_to_handcard_x.contains(id)) {
			will_to_handcard_x.removeOne(id);
			will_to_pile_x.removeOne(id);
			duplicate << id;
			n--;
		}
	}

	if (n == 0) {
		addToPile(pile_name, exchange_card->getSubcards(), false);
		room->setPlayerFlag(this, "-" + tempMovingFlag);
		return;
	}

	LogMessage log;
	log.type = "#QixingExchange";
	log.from = this;
	log.arg = QString::number(n);
	log.arg2 = skill_name;
	room->sendLog(log);

	addToPile(pile_name, duplicate, false);
	room->setPlayerFlag(this, "-" + tempMovingFlag);
	addToPile(pile_name, will_to_pile_x, false);

	room->setPlayerFlag(this, tempMovingFlag);
	addToPile(pile_name, will_to_handcard_x, false);
	room->setPlayerFlag(this, "-" + tempMovingFlag);

	DummyCard *dummy = new DummyCard(will_to_handcard_x);
	room->obtainCard(this, dummy, CardMoveReason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, objectName()), false);
	delete dummy;
}

void ServerPlayer::gainAnExtraTurn(QList<Phase> phases)
{
	ServerPlayer *current = room->getCurrent();
	try {
		room->setCurrent(this);
		if(phases.isEmpty()) phases << RoundStart << Start << Judge << Draw << Play << Discard << Finish;
		QVariantList Qphases;
		foreach (Phase p, phases)
			Qphases << (int)p;
		tag["extraTurnPhases"] = Qphases;
		room->setTag("Global_ExtraTurn" + objectName(), true);
		room->getThread()->trigger(TurnStart, room, this);
		room->removeTag("Global_ExtraTurn" + objectName());
		room->setCurrent(current);
	}catch (TriggerEvent triggerEvent) {
		if (triggerEvent == TurnBroken) {
			if (getPhase() != NotActive) {
				QString gameRule = "game_rule";
				if (room->getMode() == "04_1v3") gameRule = "hulaopass_mode";
				const GameRule *game_rule = qobject_cast<const GameRule *>(Sanguosha->getSkill(gameRule));
				if (game_rule) game_rule->trigger(EventPhaseEnd, room, this);
				changePhase(getPhase(), NotActive);
			}
			room->setCurrent(current);
		}
		throw triggerEvent;
	}
}

void ServerPlayer::copyFrom(ServerPlayer *sp)
{
	Player::copyFrom(sp);
	//handcards = QList<const Card *>(sp->handcards);
	phases = QList<ServerPlayer::Phase>(sp->phases);
	selected = QStringList(sp->selected);
}

bool ServerPlayer::CompareByActionOrder(ServerPlayer *a, ServerPlayer *b)
{
	return a->getRoom()->getFront(a, b) == a;
}

void ServerPlayer::throwEquipArea(int i)
{
	throwEquipArea(QList<int>() << i);
}

void ServerPlayer::throwEquipArea(QList<int> list)
{
	QList<int>ids;
	QVariantList newlist;
	static QList<const char*> areas;
	if(areas.isEmpty()) areas << "weapon_area" << "armor_area" << "defensive_horse_area" << "offensive_horse_area" << "treasure_area";
	foreach (int i, list) {
		if (i<0||i>4) continue;
		if (hasEquipArea(i)) {
			setEquipArea(i, false);
			if (getEquip(i)) ids << getEquip(i)->getId();
			room->broadcastProperty(this, areas[i]);
			newlist << i;

			LogMessage log;
			log.type = "#ThrowArea";
			log.from = this;
			log.arg = areas[i];
			room->sendLog(log);
		}
	}
	if (newlist.isEmpty()) return;
	room->throwCard(ids, CardMoveReason(CardMoveReason::S_REASON_THROW, objectName()), nullptr);
	QVariant data = newlist;
	room->getThread()->trigger(ThrowEquipArea, room, this, data);
}

void ServerPlayer::throwEquipArea()
{
	QVariantList list;
	static QList<const char*> areas;
	if(areas.isEmpty()) areas << "weapon_area" << "armor_area" << "defensive_horse_area" << "offensive_horse_area" << "treasure_area";
	for (int i = 0; i < 5; i++) {
		for (int n = 0; n < getEquipArea(i); n++) {
			setEquipArea(i, false);
			room->broadcastProperty(this, areas[i]);
			list << i;
		}
	}
	if (list.isEmpty()) return;
	LogMessage log;
	log.type = "#ThrowArea";
	log.from = this;
	log.arg = "equip_area";
	room->sendLog(log);
	room->throwCard(getEquipsId(), CardMoveReason(CardMoveReason::S_REASON_THROW, objectName()), nullptr);
	QVariant data = list;
	room->getThread()->trigger(ThrowEquipArea, room, this, data);
}

void ServerPlayer::addEquipArea(int i)
{
	JsonArray arg;
	arg << objectName() << i;
	room->doBroadcastNotify(S_COMMAND_ADD_EQUIP_AREA, arg);
	Player::addEquipArea(i);
}

void ServerPlayer::obtainEquipArea(int i)
{
	obtainEquipArea(QList<int>() << i);
}

void ServerPlayer::obtainEquipArea(QList<int> list)
{
	QVariantList newlist;
	static QList<const char*> areas;
	if(areas.isEmpty()) areas << "weapon_area" << "armor_area" << "defensive_horse_area" << "offensive_horse_area" << "treasure_area";
	foreach (int i, list) {
		if (i<0||i>4||hasEquipArea(i)) continue;
		setEquipArea(i, true);
		room->broadcastProperty(this, areas[i]);
		newlist << i;

		LogMessage log;
		log.type = "#ObtainArea";
		log.from = this;
		log.arg = areas[i];
		room->sendLog(log);
	}
	if (newlist.isEmpty()) return;
	QVariant data = newlist;
	room->getThread()->trigger(ObtainEquipArea, room, this, data);
}

void ServerPlayer::obtainEquipArea()
{
	QVariantList list;
	static QList<const char*> areas;
	if(areas.isEmpty())
		areas << "weapon_area" << "armor_area" << "defensive_horse_area" << "offensive_horse_area" << "treasure_area";
	for (int i = 0; i < 5; i++) {
		if (hasEquipArea(i)) continue;
		setEquipArea(i, true);
		room->broadcastProperty(this, areas[i]);
		list << i;
	}
	if (!list.isEmpty()) {
		LogMessage log;
		log.type = "#ObtainArea";
		log.from = this;
		log.arg = "equip_area";
		room->sendLog(log);
		QVariant data = list;
		room->getThread()->trigger(ObtainEquipArea, room, this, data);
	}
}

void ServerPlayer::throwJudgeArea()
{
	if (hasJudgeArea()) {
		setJudgeArea(false);
		room->broadcastProperty(this, "hasjudgearea");

		LogMessage log;
		log.type = "#ThrowArea";
		log.from = this;
		log.arg = "judge_area";
		room->sendLog(log);

		room->throwCard(getJudgingAreaID(), CardMoveReason(CardMoveReason::S_REASON_THROW, objectName()), nullptr);
		room->getThread()->trigger(ThrowJudgeArea, room, this);
	}
}

void ServerPlayer::obtainJudgeArea()
{
	if (!hasJudgeArea()) {
		setJudgeArea(true);
		room->broadcastProperty(this, "hasjudgearea");
		LogMessage log;
		log.type = "#ObtainArea";
		log.from = this;
		log.arg = "judge_area";
		room->sendLog(log);
		room->getThread()->trigger(ObtainJudgeArea, room, this);
	}
}

ServerPlayer *ServerPlayer::getSaver() const
{
	QStringList list = property("MyDyingSaver").toStringList();
	if (list.isEmpty()) return nullptr;
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if (p->objectName() == list.first())
			return p;
	}
	return nullptr;
}

bool ServerPlayer::isLowestHpPlayer(bool only)
{
	int hp = getHp();
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if (p->getHp() < hp || (only && p->getHp() <= hp))
			return false;
	}
	return true;
}

void ServerPlayer::ViewAsEquip(const QString &equip_name, bool can_duplication)
{
	if (equip_name.isEmpty()) return;
	QStringList equips = property("View_As_Equips_List").toString().split("+");
	if (!can_duplication && equips.contains(equip_name)) return;
	equips << equip_name;
	room->setPlayerProperty(this, "View_As_Equips_List", equips.join("+"));
	const ViewAsSkill *vsSkill = Sanguosha->getViewAsSkill(equip_name);
	if (vsSkill) room->attachSkillToPlayer(this,equip_name);
	//else room->acquireSkill(this,equip_name,true,true,false);
}

void ServerPlayer::removeViewAsEquip(const QString &equip_name, bool all_duplication)
{
	if (equip_name.length()<2)
		room->setPlayerProperty(this, "View_As_Equips_List", "");
	else{
		QStringList equips = property("View_As_Equips_List").toString().split("+");
		if (!equips.contains(equip_name)) return;
		if (all_duplication) equips.removeAll(equip_name);
		else equips.removeOne(equip_name);
		room->setPlayerProperty(this, "View_As_Equips_List", equips.join("+"));
		if (!equips.contains(equip_name)){
			const ViewAsSkill *vsSkill = Sanguosha->getViewAsSkill(equip_name);
			if (vsSkill) room->detachSkillFromPlayer(this,equip_name,true,true);
		}
	}
}

bool ServerPlayer::canUse(const Card *card, QList<ServerPlayer *> players, bool)
{
	if(isCardLimited(card,Card::MethodUse)) return false;
	if(players.isEmpty()) players = room->getAlivePlayers();
	addMark("&xiyan1-Clear");
	foreach (ServerPlayer *p, players) {
		int maxVotes = 0;
		if (card->targetFilter(QList<const Player *>(),p,this,maxVotes)||maxVotes>0){
			removeMark("&xiyan1-Clear");
			return true;
		}
	}
	removeMark("&xiyan1-Clear");
	return false;
}

bool ServerPlayer::canUse(const Card *card, ServerPlayer *player, bool player_must_be_target)
{
	if (player==nullptr) return canUse(card);
	return canUse(card, QList<ServerPlayer *>() << player, player_must_be_target);
}

void ServerPlayer::endPlayPhase(bool sendLog)
{
	if (getPhase() != Play || isDead()) return;
	if (hasFlag("Global_PlayPhaseTerminated")) return;
	if (sendLog) {
		LogMessage log;
		log.type = "#EndPlayPhase";
		log.from = this;
		log.arg = "play";
		room->sendLog(log);
	}
	room->setPlayerFlag(this, "Global_PlayPhaseTerminated");
}

void ServerPlayer::breakYinniState()
{
	QStringList names;
	if (getGeneralName() == "yinni_hide") {
		QString generalname = property("yinni_general").toString();
		if (!generalname.isEmpty()) names << generalname;
	}
	QString general2name = property("yinni_general2").toString();
	if (!general2name.isEmpty()&&getGeneral2Name() == "yinni_hide")
		names << general2name;
	if (names.isEmpty()) return;
	room->setPlayerProperty(this, "yinni_general", "");
	room->setPlayerProperty(this, "yinni_general2", "");

	LogMessage log;
	log.from = this;
	log.arg = names.first();
	log.type = "#BreakYinniState";
	if (names.length()>1) {
		log.type = "#BreakYinniState2";
		log.arg2 = names.last();
		room->sendLog(log);
		room->changeHero(this, log.arg, false, false, false, false);
		room->changeHero(this, log.arg2, false, false, true, false);
	} else{
		room->sendLog(log);
		room->changeHero(this, log.arg, false, false, log.arg==general2name, false);
	}

	Player::setMaxHp(getGeneralMaxHp());
	Player::setHp(getGeneralStartHp());
	room->broadcastProperty(this, "maxhp");
	room->broadcastProperty(this, "hp");

	room->getThread()->trigger(Appear, room, this);
}

void ServerPlayer::enterYinniState(int type)
{
	if(type>=0){
		room->setPlayerProperty(this, "yinni_general", getGeneralName());
		room->setPlayerProperty(this, "yinni_general_kingdom", getKingdom());
	}
	if (type > 0) {  //只变主将
		room->changeHero(this, "yinni_hide", false, false, false, false);
		return;
	} else if (type == 0)  //主将、副将都变
		room->changeHero(this, "yinni_hide", true, false, false, false);
	if(getGeneral2()){  //只变副将
		room->setPlayerProperty(this, "yinni_general2", getGeneral2Name());
		room->changeHero(this, "yinni_hide", false, false, true, false);
	}
}

int ServerPlayer::getDerivativeCard(const QString &card_name, Place place, bool visible) const
{
	foreach (int id, Sanguosha->getRandomCards(true)) {
		const Card *card = Sanguosha->getEngineCard(id);
		if (card->objectName() != card_name || room->getCardOwner(id)) continue;
		if (place == PlaceTable) return id;
		CardMoveReason reason(CardMoveReason::S_REASON_EXCLUSIVE, objectName());
		QList<CardsMoveStruct> moves;
		if(place == PlaceEquip){
			if(card->isKindOf("EquipCard")){
				const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
				if(!hasEquipArea(equip->location())) return id;
				equip = getEquip(equip->location());
				reason.m_reason = CardMoveReason::S_REASON_PUT;
				if(equip) moves << CardsMoveStruct(equip->getId(), nullptr, DiscardPile, reason);
			}else
				return id;
		}
		moves << CardsMoveStruct(id, (Player *)this, place, reason);
		room->moveCardsAtomic(moves, visible);
		return id;
	}
	return -1;
}

void ServerPlayer::setCanWake(const QString &skill_name, const QString &waked_skill_name)
{
	QStringList names = tag[waked_skill_name + "_SKILLCANWAKE"].toStringList();
	if (names.contains(skill_name)) return;
	names << skill_name;
	tag[waked_skill_name + "_SKILLCANWAKE"] = names;
	room->setPlayerMark(this, "&" + skill_name + "+:+" + waked_skill_name, 1);
}

bool ServerPlayer::canWake(const QString &waked_skill_name)
{
	QStringList names = tag[waked_skill_name + "_SKILLCANWAKE"].toStringList();
	if (names.isEmpty()) return false;
	tag.remove(waked_skill_name + "_SKILLCANWAKE");

	LogMessage log;
	log.type = "#WakeSkillCanWake";
	log.from = this;
	log.arg = names.first();
	log.arg2 = waked_skill_name;
	room->sendLog(log);
	//room->notifySkillInvoked(this, waked_skill_name);
	//room->broadcastSkillInvoke(waked_skill_name);

	foreach (QString skill_name, names)
		room->setPlayerMark(this, "&" + skill_name + "+:+" + waked_skill_name, 0);
	return true;
}

const Card *ServerPlayer::askForUseCard(const QString &pattern, const QString &prompt, ServerPlayer *who, const Card *whocard, QString flag)
{
	CardUseStruct card_use = room->askForUseCardStruct(this, pattern, prompt, -1, Card::MethodUse, true, who, whocard, flag);
	if (card_use.card) return card_use.card;
	return nullptr;
}

const Card *ServerPlayer::askForUseCard(const QString &pattern, const QString &prompt, bool addHistory, ServerPlayer *who, const Card *whocard, QString flag)
{
	CardUseStruct card_use = room->askForUseCardStruct(this, pattern, prompt, -1, Card::MethodUse, addHistory, who, whocard, flag);
	if (card_use.card) return card_use.card;
	return nullptr;
}

const Card *ServerPlayer::askForResponseCard(const QString &pattern, const QString &prompt, const QVariant &data, ServerPlayer *who, const Card *m_toCard)
{
	return room->askForCard(this, pattern, prompt, data, Card::MethodResponse, who, false, "", false, m_toCard);
}

const Card *ServerPlayer::askForResponseCard(const QString &pattern, const QString &prompt, const QVariant &data, ServerPlayer *who, bool isProvision, const Card *m_toCard)
{
	return room->askForCard(this, pattern, prompt, data, Card::MethodResponse, who, false, "", isProvision, m_toCard);
}

QList<ServerPlayer *> ServerPlayer::assignmentCards(QList<int> &cards, const QString &prompt, QList<ServerPlayer *> players, int max_num, int min_num, bool visible)
{
	if (max_num<0) max_num = cards.length();
	if (players.isEmpty()) players = room->getAlivePlayers();
	QList<int> ids,ids2;
	foreach(int id, cards){
		if (!hasCard(id))
			ids << id;
	}
	QList<CardsMoveStruct> moves,_moves;
	QList<ServerPlayer *> _guojia,tos;
	_guojia.append(this);
	if (!ids.isEmpty()){
		foreach (int id, ids)
			_moves << CardsMoveStruct(id,room->getCardOwner(id),this,PlaceTable,PlaceHand,CardMoveReason(CardMoveReason::S_REASON_PREVIEW,objectName()));
		room->notifyMoveCards(true, _moves, false, _guojia);
		room->notifyMoveCards(false, _moves, false, _guojia);
	}
	int n = 0;
	QString prompt1 = prompt, prompt2;
	if (prompt.contains("|")){
		prompt1 = prompt.split("|").first();
		prompt2 = prompt.split("|").last();
	}
	while (isAlive()&&n<max_num) {
		bool optional = n>=min_num;
		if (min_num<0&&n>0) optional = false;
		CardsMoveStruct yiji = room->askForYijiStruct(this, cards, prompt1,
			true, visible, optional||players.contains(this), max_num-n, players, CardMoveReason(), prompt2, false, false);
		if (yiji.card_ids.isEmpty()) break;
		ServerPlayer *to = (ServerPlayer *)yiji.to;
		if(!tos.contains(to)) tos << to;
		moves.append(yiji);
		QList<int> ids3;
		foreach (int id, yiji.card_ids) {
			if (hasCard(id)) ids2.append(id);
			ids.removeOne(id);
			ids3.append(id);
			n++;
		}
		if (ids3.isEmpty()) continue;
		_moves.clear();
		_moves << CardsMoveStruct(ids3,this,nullptr,PlaceHand,PlaceTable,CardMoveReason(CardMoveReason::S_REASON_PREVIEW,objectName()));
		room->notifyMoveCards(true, _moves, false, _guojia);
		room->notifyMoveCards(false, _moves, false, _guojia);
	}
	while (min_num>n&&cards.length()>0) {
		int id = cards.at(qrand()%cards.length());
		ServerPlayer *to = players.at(qrand()%players.length());
		if(players.contains(this)) to = this;
		if(!tos.contains(to)) tos << to;
		moves << CardsMoveStruct(id,to,PlaceHand,CardMoveReason(CardMoveReason::S_REASON_GIVE,objectName(),to->objectName(),prompt1.split("=").first(),""));
		cards.removeOne(id);
		n++;
	}
	_moves.clear();
	if (!ids2.isEmpty())
		_moves << CardsMoveStruct(ids2,nullptr,this,PlaceTable,PlaceHand,CardMoveReason(CardMoveReason::S_REASON_PREVIEW,objectName()));
	if (!ids.isEmpty())
		_moves << CardsMoveStruct(ids,this,nullptr,PlaceHand,PlaceTable,CardMoveReason(CardMoveReason::S_REASON_PREVIEW,objectName()));
	if (!_moves.isEmpty()){
		room->notifyMoveCards(true, _moves, false, _guojia);
		room->notifyMoveCards(false, _moves, false, _guojia);
	}
	room->moveCardsAtomic(moves, visible);
	return tos;
}

void ServerPlayer::skillInvoked(const QString &skill_name, int type, ServerPlayer *owner)
{
	LogMessage log;
	log.type = "#InvokeSkill";
	log.from = this;
	log.arg = skill_name;
	if(owner){
		log.type = "#InvokeOthersSkill";
		log.to << owner;
	}else
		owner = this;
	room->sendLog(log);
	room->broadcastSkillInvoke(skill_name,type,owner);
	room->notifySkillInvoked(owner,skill_name);
}

void ServerPlayer::skillInvoked(const Skill* skill, int type, ServerPlayer *owner)
{
	skillInvoked(skill->objectName(),type,owner);
}

QList<ServerPlayer *> ServerPlayer::getRandomTargets(const Card *card, QList<ServerPlayer *> players)
{
	if (players.isEmpty()) players = room->getAlivePlayers();
	qShuffle(players);
	QList<const Player *> tos;
	for (int i = 0; i < players.length(); i++) {
		int x = 0;
		if(card->targetFilter(tos,players.at(i),this,x)||x>0){
			tos << players.at(i);
			if(card->targetsFeasible(tos,this))
				break;
			i = 0;
		}
	}
	QList<ServerPlayer *> targets;
	foreach (const Player *p, tos)
		targets << (ServerPlayer *)p;
	return targets;
}

void ServerPlayer::setSkillDescriptionSwap(const QString &skill_name, const QString &key, const QString &value)
{
	JsonArray arg;
	arg << objectName();
	arg << skill_name;
	arg << key;
	arg << value;
	room->doBroadcastNotify(S_COMMAND_SKILL_DESCRIPTION_SWAP, arg);
	Player::setSkillDescriptionSwap(skill_name, key, value);
}

void ServerPlayer::setAvatarIcon(const QString &avatar_name, bool small)
{
	if(small) room->setPlayerProperty(this,"avatarIcon2",avatar_name);
	else room->setPlayerProperty(this,"avatarIcon",avatar_name);
	JsonArray args;
	args << (int)QSanProtocol::S_GAME_EVENT_AVATAR_ICON << objectName() << small << avatar_name;
	room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
}

bool ServerPlayer::damageRevises(QVariant &data, int n)
{
	DamageStruct damage = data.value<DamageStruct>();
	int x = qMax(0,damage.damage+n);
	LogMessage log;
	log.type = "$DamageRevises1";
	if(this!=damage.to) log.type = "$DamageRevises2";
	if(x<1) log.type = "$DamageRevises0";
	log.from = this;
	log.arg = QString::number(damage.damage);
	log.arg2 = QString::number(x);
	log.arg3 = "Damage+";
	if(n<1) log.arg3 = "Damage-";
	room->sendLog(log);
	damage.damage = x;
	damage.prevented = x<1;
	data.setValue(damage);
	return x<1;
}

