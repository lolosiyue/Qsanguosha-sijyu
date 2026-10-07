#include "clientplayer.h"
#include "client.h"
#include "engine.h"
#include "clientstruct.h"
#include "build-features.h"
#include <QFile>
#include <QSet>

ClientPlayer *Self = nullptr;

ClientPlayer::ClientPlayer(Client *client)
	: Player(client), handcard_num(0)
{
	mark_doc = new QTextDocument(this);
}

int ClientPlayer::aliveCount(bool includeRemoved) const
{
    if (includeRemoved)
        return ClientInstance->alivePlayerCount();
    int count = 0;
    foreach (const Player *p, getSiblings()) {
        if (p->isAlive() && !p->isRemoved())
            ++count;
    }
    return count + (isAlive() && !isRemoved() ? 1 : 0);
}

ClientPlayer *ClientPlayer::getNextAlive(int n) const
{
    ClientPlayer *p = const_cast<ClientPlayer *>(this);
    for (int i = 0; i < n; ++i) {
        do {
            p = ClientInstance->getNextPlayer(p);
        } while (!p->isAlive() || p->isRemoved());
    }
    return p;
}

ClientPlayer *ClientPlayer::getLastAlive(int n) const
{
    ClientPlayer *p = const_cast<ClientPlayer *>(this);
    for (int i = 0; i < n; ++i) {
        do {
            p = ClientInstance->getLastPlayer(p);
        } while (!p->isAlive() || p->isRemoved());
    }
    return p;
}

bool ClientPlayer::useExactHandInfo() const
{
	if (Self == nullptr)
		return false;
	return Self == this || Self->canSeeHandcard(this);
}

void ClientPlayer::addKnownHandCard(const Card *card)
{
	if (card == nullptr) return;
	foreach (const Card *kc, known_cards) {
		if(kc->getId()==card->getId())
			return;
	}
	known_cards << card;
}

void ClientPlayer::addCard(int id, Place place)
{
	if (place == PlaceHand) {
		addHandIds(QList<int>() << id);
		return;
	}
	if(id<0) return;
	Player::addCard(id, place);
}

void ClientPlayer::removeCard(int id, Place place)
{
	if (place == PlaceHand) {
		removeHandIds(QList<int>() << id);
		return;
	}
	if(id<0) return;
	Player::removeCard(id, place);
}

/*switch (place) {
	case PlaceHand: {
		handcard_num++;
		if (card){
			known_cards << card;
			if(!hand_ids.contains(card->getId())){
				hand_ids << card->getId();
				if (hand_ids.size()>1) qsanShuffle(hand_ids);
			}
		}
	}
}*/

int ClientPlayer::getMaxCards() const
{
    return m_uiState.handMax;
}

const PlayerUIState &ClientPlayer::uiState() const
{
    return m_uiState;
}

void ClientPlayer::setUIState(const PlayerUIState &state)
{
    m_uiState = state;
    setSkillDescriptionState(state.skillUsage, state.skillValidity, state.skillEffects);
}

bool ClientPlayer::isLastHandCard(const Card *card, bool contain) const
{
	if (!useExactHandInfo())
		return Player::isLastHandCard(card, contain);
	if (card == nullptr)
		return false;

	if(card->isVirtualCard()){
		QList<int> ids = card->getSubcards();
		if(ids.length()>0){
			if (contain) {
				foreach (int hid, hand_ids) {
					if (!ids.contains(hid))
						return false;
				}
				return true;
			} else if(ids.length()>=hand_ids.length()){
				foreach (int id, ids) {
					if (!hand_ids.contains(id))
						return false;
				}
				return true;
			}
		}
	}else if(hand_ids.length()==1)
		return hand_ids.contains(card->getId());
	return false;
}

QList<const Card *> ClientPlayer::getHandcards() const
{
	if (!useExactHandInfo())
		return Player::getHandcards();

	QList<const Card *> cards;
	foreach (int id, hand_ids) {
		if (id < 0) continue;
		const Card *card = Sanguosha->getCard(id);
		if (card != nullptr)
			cards << card;
	}

	return cards;
}

int ClientPlayer::getHandcardNum() const
{
	if (!useExactHandInfo())
		return handcard_num;
	return hand_ids.size();
}

QList<int> ClientPlayer::handCards() const
{
	return hand_ids;
}

QList<const Card *> ClientPlayer::getKnownCards() const
{
	return known_cards;
}

void ClientPlayer::retainVisibleKnownHandcards()
{
	QList<const Card *> visible_cards;
	foreach (const Card *card, known_cards) {
		if (card == nullptr)
			continue;
		if (!hand_ids.contains(card->getId()))
			continue;
		if (!card->hasFlag("visible"))
			continue;
		visible_cards << card;
	}
	known_cards = visible_cards;
}

void ClientPlayer::addHandIds(const QList<int> &card_ids)
{
	foreach(int id, card_ids){
		if (id < Card::S_UNKNOWN_CARD_ID) continue;
		// A redacted card contributes to the count, never to the pointer list.
		++handcard_num;
		if (id == Card::S_UNKNOWN_CARD_ID) continue;
		const Card *card = Sanguosha->getCard(id);
		if (card == nullptr) continue;
		Player::addCard(id,PlaceHand);
		if (this != Self) addKnownHandCard(card);
		if(hand_ids.contains(id)) continue;
		hand_ids << id;
	}
	if (hand_ids.size()>1)
		qsanShuffle(hand_ids);
}

void ClientPlayer::removeHandIds(const QList<int> &card_ids)
{
	foreach(int id, card_ids){
		if (id < Card::S_UNKNOWN_CARD_ID) continue;
		handcard_num = qMax(0, handcard_num - 1);
		if (id == Card::S_UNKNOWN_CARD_ID) {
			// An unidentified loss invalidates exact membership; retain only the count.
			foreach (const Card *card, Player::getHandcards()) {
				if (card) Player::removeCard(card->getId(), PlaceHand);
			}
			hand_ids.clear();
			if (!hasFlag("S_REASON_SWAP")) known_cards.clear();
			continue;
		}
		Player::removeCard(id,PlaceHand);
		hand_ids.removeAll(id);
		known_cards.removeAll(Sanguosha->getCard(id));
	}
	if(handcard_num == 0&&!hasFlag("S_REASON_SWAP"))
		known_cards.clear();
}

void ClientPlayer::setKnownCards(QList<int> card_ids)
{
	known_cards.clear();
	QList<int> exact_ids;
	foreach(int cardId, card_ids){
		if(cardId < 0) continue;
		const Card *card = Sanguosha->getCard(cardId);
		if (card == nullptr) continue;
		known_cards << card;
		exact_ids << cardId;
	}
	if (this == Self)
		return;
	hand_ids = exact_ids;
	if (hand_ids.size() > 1)
		qsanShuffle(hand_ids);
}

void ClientPlayer::setKnownCards(QList<const Card*> cards)
{
	known_cards.clear();
	QList<int> exact_ids;
	foreach (const Card *card, cards) {
		if (card == nullptr || card->getId() < 0) continue;
		known_cards << card;
		exact_ids << card->getId();
	}
	if (this == Self)
		return;
	hand_ids = exact_ids;
	if (hand_ids.size() > 1)
		qsanShuffle(hand_ids);
}

QTextDocument *ClientPlayer::getMarkDoc() const
{
	return mark_doc;
}

void ClientPlayer::changePile(const QString &name, bool add, QList<int> card_ids)
{
	if (add)
		piles[name].append(card_ids);
	else {
		foreach (int id, card_ids) {
			if (piles[name].contains(id))
				piles[name].removeOne(id);
			else if(piles[name].contains(Card::S_UNKNOWN_CARD_ID))
				piles[name].removeOne(Card::S_UNKNOWN_CARD_ID);
			else if (!piles[name].isEmpty())
				piles[name].removeAt(0);
			// An empty-pile removeAt(0) is out of bounds in Release; QList only
			// asserts in Debug, and the invalid state later crashes during memcpy.
		}
		if(piles[name].isEmpty())
			piles.remove(name);
	}
	if (name.startsWith("#")) return;
	emit pile_changed(name);
}

void ClientPlayer::syncPileCards(const QString &pile_name, QList<int> card_ids)
{
	piles[pile_name] = card_ids;
	emit pile_changed(pile_name);
}

void ClientPlayer::changeGeneralPile(const QString &name, bool add, QStringList general_names)
{
	if (add)
		general_piles[name].append(general_names);
	else {
		foreach (QString general_name, general_names) {
			general_piles[name].removeOne(general_name);
		}
		if (general_piles[name].isEmpty())
			general_piles.remove(name);
	}
	if (!name.startsWith("#"))
		emit general_pile_changed(name);
}

QString ClientPlayer::getDeathPixmapPath() const
{
	QString basename = getRole();
	if (ServerInfo.GameMode == "06_3v3" || ServerInfo.GameMode == "06_XMode") {
		if (basename == "lord" || basename == "renegade")
			basename = "marshal";
		else
			basename = "guard";
	}

	if (ServerInfo.EnableHegemony)
		basename = "unknown";

	if (property("RestPlayer").toBool())
		basename = "rest";

	return QString("image/system/death/%1.png").arg(basename);
}

/*void ClientPlayer::setHandcardNum(int n)
{
	handcard_num = n;
}*/

QString ClientPlayer::getGameMode() const
{
	return ServerInfo.GameMode;
}

void ClientPlayer::setFlags(const QString &flag)
{
	Player::setFlags(flag);

	if (flag.endsWith("actioned"))
		emit state_changed();
}

void ClientPlayer::setMark(const QString &mark, int value)
{
	if (marks[mark] == value && mark != "@substitute")
		return;
	if(value==0) marks.remove(mark);
	else marks[mark] = value;

	if (mark == "drank")
		emit drank_changed();
	else if (mark.startsWith("@")) {
		// @todo: consider move all the codes below to PlayerCardContainerUI.cpp
		// set mark doc
		static QStringList marklist;
		if (marklist.isEmpty())
			marklist << "@huashen" << "@yongsi_test" << "@jushou_test"
			<< "@max_cards_test" << "@defensive_distance_test" << "@offensive_distance_test"
			<< "@bossExp" << "@HuJia";
		QStringList keys = marks.keys();
		foreach (QString key, marklist) {
			if (keys.contains(key)) {
				keys.removeOne(key);
				keys.prepend(key);
			}
		}
		QString text;
		foreach (QString key, keys) {
#if QSAN_ENABLE_QML
			// A QML-bound mark is drawn by QmlTableLayer; builds without QML keep the stock text.
			if (key.startsWith("@")&&marks[key]>0&&!Sanguosha->isQmlMark(key)) {
#else
			if (key.startsWith("@")&&marks[key]>0) {
#endif
				QString filename = QString("image/mark/%1.png").arg(key);
				if (!QFile::exists(filename))
					filename = QString("image/mark/@default.png");
				text.append(QString("<img src='%1' />").arg(filename));
				if (marks[key]>1) text.append(QString("%1").arg(marks[key]));
				if (this != Self) text.append("<br>");
				if (key == "@substitute") {
					QString hp_str = property("tishen_hp").toString();
					if (hp_str.isEmpty()) continue;
					text.append(QString("<img src='image/mark/@substitute_hp.png' />%1").arg(hp_str));
					if (this != Self) text.append("<br>");
				}
			}
		}
		mark_doc->setHtml(text);
		if (mark == "@duanchang")
			emit duanchang_invoked();
	} else if (mark.startsWith("&"))
		emit Mark_changed(mark, value);
}

void ClientPlayer::resetForManagedSync()
{
	// Replace card zones without letting ClientPlayer interpret the old hand as
	// incremental card losses while the authoritative snapshot is replayed.
	QSet<int> oldHand;
	for (const Card *card : Player::getHandcards())
		if (card) oldHand.insert(card->getId());
	for (int id : hand_ids) oldHand.insert(id);
	for (int id : oldHand) Player::removeCard(id, PlaceHand);
	handcard_num = 0;
	hand_ids.clear();
	known_cards.clear();
	for (int id : getEquipsId()) Player::removeCard(id, PlaceEquip);
	for (int id : getJudgingAreaID()) Player::removeCard(id, PlaceDelayedTrick);
	const QStringList oldPiles = piles.keys();
	for (const QString &pile : oldPiles)
		for (int id : piles.value(pile)) Player::removeCard(id, PlaceSpecial);
	for (const QString &pile : oldPiles) Player::setPileOpen(pile, QStringLiteral("."));
	piles.clear();
	for (const QString &pile : oldPiles)
		if (!pile.startsWith(QLatin1Char('#'))) emit pile_changed(pile);

	const QStringList oldGeneralPiles = general_piles.keys();
	general_piles.clear();
	general_pile_open.clear();
	for (const QString &pile : oldGeneralPiles)
		if (!pile.startsWith(QLatin1Char('#'))) emit general_pile_changed(pile);
	for (const QString &mark : marks.keys()) setMark(mark, 0);
	marks.clear();
	mark_doc->clear();
	history.clear();
	clearFlags();
	clearTags();
	clearCardLimitation();
	clearSkillInstances();
	setSkillDescriptionState({}, {}, {});
	description_s2k2v.clear();
	card_description_swaps.clear();
	m_skillValidityCache.clear();

	QList<int> empty;
	setShownHandcards(empty);
	setBrokenEquips(empty);
	setUIState(PlayerUIState());

	// Dynamic gameplay properties are replayed by the following complete
	// ServerPlayer::marshal projection. Keep the signup avatar as connection
	// identity; the rest belongs to the timeline.
	for (const QByteArray &name : dynamicPropertyNames()) {
		if (name == "avatar" || name == "avatarIcon" || name == "avatarIcon2") continue;
		QObject::setProperty(name.constData(), QVariant());
	}
	emit state_changed();
	emit skill_state_changed();
	emit mark_changed();
	emit gameplay_property_changed();
}
