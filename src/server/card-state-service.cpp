#include "card-state-service.h"

#include "card.h"
#include "engine.h"
#include "json.h"
#include "protocol.h"
#include "room-notifier.h"
#include "serverplayer.h"

using namespace QSanProtocol;

CardStateService::CardStateService(RoomNotifier &notifier)
	: m_notifier(notifier)
{
}

void CardStateService::addCardMark(int cardId, const QString &mark, int addNum,
	ServerPlayer *who)
{
	addCardMark(Sanguosha->getCard(cardId), mark, addNum, who);
}

void CardStateService::addCardMark(const Card *card, const QString &mark,
	int addNum, ServerPlayer *who)
{
	if (!card) return;
	setCardMark(card, mark, card->getMark(mark) + addNum, who);
}

void CardStateService::removeCardMark(int cardId, const QString &mark, int removeNum)
{
	removeCardMark(Sanguosha->getCard(cardId), mark, removeNum);
}

void CardStateService::removeCardMark(const Card *card, const QString &mark,
	int removeNum)
{
	if (!card) return;
	setCardMark(card, mark, qMax(0, card->getMark(mark) - removeNum), nullptr);
}

void CardStateService::setCardMark(const Card *card, const QString &mark,
	int value, ServerPlayer *who)
{
	card->setMark(mark, value);
	if (card->isVirtualCard()) return;
	setCardMark(card->getId(), mark, value, who);
}

void CardStateService::setCardMark(int cardId, const QString &mark,
	int value, ServerPlayer *who)
{
	Sanguosha->getCard(cardId)->setMark(mark, value);
	JsonArray arg;
	arg << cardId << mark << value;
	if (who)
		m_notifier.doNotify(who, S_COMMAND_CARD_MARK, arg);
	else
		m_notifier.doBroadcastNotify(S_COMMAND_CARD_MARK, arg);
}

void CardStateService::setCardFlag(const Card *card, const QString &flag,
	ServerPlayer *who)
{
	card->setFlags(flag);
	if (card->isVirtualCard()) return;
	setCardFlag(card->getId(), flag, who);
}

void CardStateService::setCardFlag(int cardId, const QString &flag,
	ServerPlayer *who)
{
	Sanguosha->getCard(cardId)->setFlags(flag);
	JsonArray arg;
	arg << cardId << flag;
	if (who)
		m_notifier.doNotify(who, S_COMMAND_CARD_FLAG, arg);
	else
		m_notifier.doBroadcastNotify(S_COMMAND_CARD_FLAG, arg);
}

void CardStateService::clearCardFlag(const Card *card, ServerPlayer *who)
{
	setCardFlag(card, ".", who);
}

void CardStateService::clearCardFlag(int cardId, ServerPlayer *who)
{
	clearCardFlag(Sanguosha->getCard(cardId), who);
}

void CardStateService::setCardTip(int cardId, const QString &tip)
{
	if (tip.isEmpty()) return;
	if (tip.startsWith("-"))
		setCardFlag(cardId, "-cardTip:" + tip.mid(1), nullptr);
	else
		setCardFlag(cardId, "cardTip:" + tip, nullptr);
}

void CardStateService::clearCardTip(int cardId)
{
	Card *card = Sanguosha->getCard(cardId);
	if (!card) return;
	foreach (const QString &flag, card->getFlags()) {
		if (flag.startsWith("cardTip:"))
			setCardFlag(cardId, "-" + flag, nullptr);
	}
}
