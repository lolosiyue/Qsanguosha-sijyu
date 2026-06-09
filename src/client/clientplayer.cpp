#include "clientplayer.h"
#include "client.h"
#include "engine.h"
#include "clientstruct.h"

ClientPlayer *Self = nullptr;

ClientPlayer::ClientPlayer(Client *client)
	: Player(client)//, handcard_num(0)
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
	foreach (const Card *kc, known_cards) {
		if(kc->getId()==card->getId())
			return;
	}
	known_cards << card;
}

void ClientPlayer::addCard(int id, Place place)
{
	if(id<0) return;
	Player::addCard(id, place);
	if(place==PlaceHand){
		if (this != Self)
			addKnownHandCard(Sanguosha->getCard(id));
		if(!hand_ids.contains(id)) hand_ids << id;
		if(hand_ids.size()>1) qShuffle(hand_ids);
	}
}

void ClientPlayer::removeCard(int id, Place place)
{
	if(id<0) return;
	Player::removeCard(id, place);
	if(place==PlaceHand){
		known_cards.removeAll(Sanguosha->getCard(id));
		hand_ids.removeAll(id);
	}
}

/*switch (place) {
	case PlaceHand: {
		handcard_num++;
		if (card){
			known_cards << card;
			if(!hand_ids.contains(card->getId())){
				hand_ids << card->getId();
				if (hand_ids.size()>1) qShuffle(hand_ids);
			}
		}
	}
}*/

int ClientPlayer::getMaxCards() const
{
    QVariant handMaxVar = getTag("UI_Hand_Max");
    if (handMaxVar.isValid()) {
        return handMaxVar.toInt();
    }
    return qMax(getHp(), 0);
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
		return Player::getHandcardNum();
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

void ClientPlayer::addHandIds(JsonArray args)
{
	QList<int> ids;
	JsonUtils::tryParse(args.first(), ids);
	foreach(int id, ids){
		Player::addCard(id,PlaceHand);
		if(hand_ids.contains(id)) continue;
		hand_ids << id;
	}
	if (hand_ids.size()>1)
		qShuffle(hand_ids);
}

void ClientPlayer::removeHandIds(JsonArray args)
{
	QList<int> ids;
	JsonUtils::tryParse(args.first(), ids);
	foreach(int id, ids){
		Player::removeCard(id,PlaceHand);
		hand_ids.removeAll(id);
	}
	if(hand_ids.isEmpty()&&!hasFlag("S_REASON_SWAP"))
		known_cards.clear();
}

void ClientPlayer::setKnownCards(QList<int> card_ids)
{
	known_cards.clear();
	QList<int> exact_ids;
	foreach(int cardId, card_ids){
		if(cardId < 0) continue;
		known_cards << Sanguosha->getCard(cardId);
		exact_ids << cardId;
	}
	if (this == Self)
		return;
	hand_ids = exact_ids;
	if (hand_ids.size() > 1)
		qShuffle(hand_ids);
}

void ClientPlayer::setKnownCards(QList<const Card*> cards)
{
	known_cards.clear();
	known_cards = cards;
	QList<int> exact_ids;
	foreach (const Card *card, cards) {
		if (card == nullptr || card->getId() < 0) continue;
		exact_ids << card->getId();
	}
	if (this == Self)
		return;
	hand_ids = exact_ids;
	if (hand_ids.size() > 1)
		qShuffle(hand_ids);
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
			else
				piles[name].removeAt(0);
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
			if (key.startsWith("@")&&marks[key]>0) {
				text.append(QString("<img src='image/mark/%1.png' />").arg(key));
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

void ClientPlayer::setIntMark(const QString &mark, QList<int> value)
{
    if (value.isEmpty())
        int_marks.remove(mark);
    else
        int_marks[mark] = value;
}

QStringList ClientPlayer::getBigKingdoms(const QString &, MaxCardsType::MaxCardsCount) const
{
    QMap<QString, int> kingdom_map;
    kingdom_map.insert("wei", 0);
    kingdom_map.insert("shu", 0);
    kingdom_map.insert("wu", 0);
    kingdom_map.insert("qun", 0);

    QList<const Player *> players = getAliveSiblings();
    players.prepend(this);
    foreach (const Player *p, players) {
        if (!p->hasShownOneGeneral())
            continue;
        if (p->getRole() == "careerist") {
            kingdom_map["careerist"] = 1;
            continue;
        }
        ++kingdom_map[p->getKingdom()];
    }

    QStringList big_kingdoms;
    foreach (const QString &key, kingdom_map.keys()) {
        if (kingdom_map[key] == 0)
            continue;
        if (big_kingdoms.isEmpty()) {
            if (kingdom_map[key] > 1)
                big_kingdoms << key;
            continue;
        }
        if (kingdom_map[key] == kingdom_map[big_kingdoms.first()]) {
            big_kingdoms << key;
        } else if (kingdom_map[key] > kingdom_map[big_kingdoms.first()]) {
            big_kingdoms.clear();
            big_kingdoms << key;
        }
    }

    const Player *jade_seal_owner = nullptr;
    foreach (const Player *p, players) {
        if (p->hasTreasure("JadeSeal") && p->hasShownOneGeneral()) {
            jade_seal_owner = p;
            break;
        }
    }

    if (jade_seal_owner != nullptr) {
        if (jade_seal_owner->getRole() == "careerist") {
            big_kingdoms.clear();
            big_kingdoms << jade_seal_owner->objectName();
        } else {
            QString kingdom = jade_seal_owner->getKingdom();
            big_kingdoms.clear();
            big_kingdoms << kingdom;
        }
    }

    return big_kingdoms;
}

