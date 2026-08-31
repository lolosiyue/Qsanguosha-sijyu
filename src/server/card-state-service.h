#ifndef CARD_STATE_SERVICE_H
#define CARD_STATE_SERVICE_H

#include <QString>

class Card;
class RoomNotifier;
class ServerPlayer;

class CardStateService
{
public:
	explicit CardStateService(RoomNotifier &notifier);

	void addCardMark(int cardId, const QString &mark, int addNum, ServerPlayer *who);
	void addCardMark(const Card *card, const QString &mark, int addNum, ServerPlayer *who);
	void removeCardMark(int cardId, const QString &mark, int removeNum);
	void removeCardMark(const Card *card, const QString &mark, int removeNum);
	void setCardMark(const Card *card, const QString &mark, int value, ServerPlayer *who);
	void setCardMark(int cardId, const QString &mark, int value, ServerPlayer *who);

	void setCardFlag(const Card *card, const QString &flag, ServerPlayer *who);
	void setCardFlag(int cardId, const QString &flag, ServerPlayer *who);
	void clearCardFlag(const Card *card, ServerPlayer *who);
	void clearCardFlag(int cardId, ServerPlayer *who);

	void setCardTip(int cardId, const QString &tip);
	void clearCardTip(int cardId);

private:
	RoomNotifier &m_notifier;
};

#endif
