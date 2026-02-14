#ifndef _ROOM_STATE_H
#define _ROOM_STATE_H

//#include "player.h"
#include "structs.h"
//#include "wrapped-card.h"

class Player;
class WrappedCard;

// RoomState is a singleton that stores virtual generals, cards (versus factory loaded
// generals, cards in the Engine). Each room or roomscene should have one and only one
// associated RoomState.

class RoomState
{
public:
    inline RoomState(bool isClient)
    {
        m_isClient = isClient;
    }
    ~RoomState();
    inline bool isClient() const
    {
        return m_isClient;
    }
    Card *getCard(int cardId) const;
    inline void setCurrentPlayer(Player *player)
    {
        m_currentPlayer = player;
    }
    inline QString getCurrentCardUsePattern() const
    {
        return m_currentCardUsePattern;
    }
    inline void setCurrentCardUsePattern(const QString &newPattern)
    {
        m_currentCardUsePattern = newPattern;
    }
    inline void setMFlags(const QString &id, const QString &flag)
    {
		if(flag.isEmpty()) return;
		if(flag == ".") m_flags[id].clear();
        if(flag.startsWith("-")){
			QString copy = flag.mid(1);
			m_flags[id].removeAll(copy);
		}else
			m_flags[id] << flag;
    }
    inline bool hasMFlags(const QString &id, const QString &flag) const
    {
        return m_flags[id].contains(flag);
    }
    inline Player *getCurrentPlayer() const
    {
        return m_currentPlayer;
    }
    inline CardUseStruct::CardUseReason getCurrentCardUseReason() const
    {
        return m_currentCardUseReason;
    }
    inline void setCurrentCardUseReason(CardUseStruct::CardUseReason reason)
    {
        m_currentCardUseReason = reason;
    }

    // Update a card in the room.
    // @param cardId
    //        Id of card to be updated.
    // @param newCard
    //        Card to be updated in the room.
    // @return
    void resetCard(int cardId) const;
    // Reset all cards, generals' states of the room instance
    void reset();

protected:
    QHash<int, WrappedCard *> m_cards;
    bool m_isClient;
    Player *m_currentPlayer;
    QString m_currentCardUsePattern;
    CardUseStruct::CardUseReason m_currentCardUseReason;
    QHash<QString, QStringList> m_flags;
};

#endif