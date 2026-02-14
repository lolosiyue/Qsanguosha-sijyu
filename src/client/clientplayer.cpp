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

int ClientPlayer::aliveCount() const
{
	return ClientInstance->alivePlayerCount();
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
		known_cards << Sanguosha->getCard(id);
		if(!hand_ids.contains(id)) hand_ids << id;
		if(hand_ids.size()>1) qShuffle(hand_ids);
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
		break;
	}
	case PlaceEquip: {
		setEquip(card);
		break;
	}
	case PlaceDelayedTrick: {
		addDelayedTrick(card);
		break;
	}
	default:
		break;
	}*/
}

void ClientPlayer::removeCard(int id, Place place)
{
	if(id<0) return;
	Player::removeCard(id,place);
	if(place==PlaceHand){
		hand_ids.removeAll(id);
		if(hand_ids.isEmpty()&&!hasFlag("S_REASON_SWAP"))
			known_cards.clear();
		else{
			foreach (const Card *kc, known_cards) {
				if(kc->getId()==id) known_cards.removeAll(kc);
			}
		}
	}
	/*switch (place) {
	case PlaceHand: {
		handcard_num--;
		if (card){
			known_cards.removeAll(card);
			hand_ids.removeAll(card->getId());
		}
		break;
	}
	case PlaceEquip:{
		removeEquip(card);
		break;
	}
	case PlaceDelayedTrick:{
		removeDelayedTrick(card);
		break;
	}
	default:
		break;
	}*/
}

/*
bool ClientPlayer::isLastHandCard(const Card *card, bool contain) const
{
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
	QList<const Card *> cards;
	foreach (int id, hand_ids)
		cards << Sanguosha->getCard(id);

	return cards;
}

int ClientPlayer::getHandcardNum() const
{
	return hand_ids.size();
}*/

QList<int> ClientPlayer::handCards() const
{
	return hand_ids;
}

QList<const Card *> ClientPlayer::getKnownCards() const
{
	return known_cards;
}

void ClientPlayer::addHandIds(JsonArray args)
{
	QList<int> ids;
	JsonUtils::tryParse(args.first(), ids);
	foreach(int id, ids){
		Player::addCard(id,PlaceHand);
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
	foreach(int cardId, card_ids){
		if(cardId < 0) continue;
		known_cards << Sanguosha->getCard(cardId);
	}
}

void ClientPlayer::setKnownCards(QList<const Card*> cards)
{
	known_cards = cards;
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

QString ClientPlayer::getDeathPixmapPath() const
{
	QString basename = getRole();
	if (ServerInfo.GameMode == "06_3v3" || ServerInfo.GameMode == "06_XMode") {
		if (basename == "lord" || basename == "renegade")
			basename = "marshal";
		else
			basename = "guard";
	}

	if (ServerInfo.EnableHegemony||property("RestPlayer").toBool())
		basename = "unknown";

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

