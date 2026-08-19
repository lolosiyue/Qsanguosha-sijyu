#ifndef CARD_MOVEMENT_SERVICE_H
#define CARD_MOVEMENT_SERVICE_H

#include "structs.h"

#include <QList>
#include <QMap>

class Card;
class Room;
class ServerPlayer;

class CardLocationIndex
{
public:
    void set(int cardId, ServerPlayer *owner, Player::Place place);
    ServerPlayer *owner(int cardId) const;
    Player::Place place(int cardId) const;

private:
    QMap<int, Player::Place> m_places;
    QMap<int, ServerPlayer *> m_owners;
};

class CardMovementService
{
public:
    explicit CardMovementService(Room &room);

    QList<int> &drawPile();
    const QList<int> &drawPile() const;
    QList<int> &discardPile();
    const QList<int> &discardPile() const;
    QList<int> &tableCards();
    const QList<int> &tableCards() const;

    void setCardMapping(int cardId, ServerPlayer *owner, Player::Place place);
    ServerPlayer *getCardOwner(int cardId) const;
    Player::Place getCardPlace(int cardId) const;

    QList<int> getNCards(int n, bool updatePileNumber, bool isTop);
    int drawCard(bool isTop);
    void swapPile();
    int getCardFromPile(const QString &cardPattern);
    void returnToTopDrawPile(QList<int> cards);
    void returnToEndDrawPile(QList<int> cards);

    QList<int> drawCardsList(ServerPlayer *player, int n, const QString &reason,
                             bool isTop, bool visible);
    void drawCards(ServerPlayer *player, int n, const QString &reason,
                   bool isTop, bool visible);
    void drawCards(QList<ServerPlayer *> players, int n, const QString &reason,
                   bool isTop, bool visible);
    void drawCards(QList<ServerPlayer *> players, QList<int> nList,
                   const QString &reason, bool isTop, bool visible);

    void obtainCard(ServerPlayer *target, const Card *card,
                    const CardMoveReason &reason, bool visible);
    void obtainCard(ServerPlayer *target, const Card *card, bool visible);
    void obtainCard(ServerPlayer *target, int cardId, bool visible);
    void obtainCard(ServerPlayer *target, const Card *card,
                    const QString &skillName, bool visible);
    void obtainCard(ServerPlayer *target, int cardId,
                    const QString &skillName, bool visible);

    void throwCard(int cardId, ServerPlayer *who, ServerPlayer *thrower);
    void throwCard(const Card *card, ServerPlayer *who, ServerPlayer *thrower);
    void throwCard(const Card *card, const CardMoveReason &reason,
                   ServerPlayer *who, ServerPlayer *thrower);
    void throwCard(int cardId, const QString &skillName,
                   ServerPlayer *who, ServerPlayer *thrower);
    void throwCard(const Card *card, const QString &skillName,
                   ServerPlayer *who, ServerPlayer *thrower);
    void throwCard(QList<int> cardIds, const QString &skillName,
                   ServerPlayer *who, ServerPlayer *thrower);
    void throwCard(QList<int> cardIds, const CardMoveReason &reason,
                   ServerPlayer *who, ServerPlayer *thrower);

    void recastCard(ServerPlayer *player, const Card *card, const QString &skillName);
    void recastCard(ServerPlayer *player, int cardId, const QString &skillName);
    void recastCards(ServerPlayer *player, const QList<int> &cardIds,
                     const QString &skillName);
    void recastCardWithDraw(ServerPlayer *player, const Card *card, int drawCount,
                            const QString &skillName);
    void recastCardWithDraw(ServerPlayer *player, int cardId, int drawCount,
                            const QString &skillName);
    void recastCardsWithDraw(ServerPlayer *player, const QList<int> &cardIds,
                             int drawCount, const QString &skillName);

    void moveCardTo(const Card *card, ServerPlayer *dstPlayer,
                    Player::Place dstPlace, bool visible, bool guanxin);
    void moveCardTo(const Card *card, ServerPlayer *dstPlayer,
                    Player::Place dstPlace, const CardMoveReason &reason,
                    bool visible, bool guanxin);
    void moveCardTo(const Card *card, ServerPlayer *srcPlayer,
                    ServerPlayer *dstPlayer, Player::Place dstPlace,
                    const CardMoveReason &reason, bool visible, bool guanxin);
    void moveCardTo(const Card *card, ServerPlayer *srcPlayer,
                    ServerPlayer *dstPlayer, Player::Place dstPlace,
                    const QString &pileName, const CardMoveReason &reason,
                    bool visible, bool guanxin);
    void moveCardsAtomic(CardsMoveStruct cardsMove, bool visible, bool guanxing);
    void moveCardsAtomic(QList<CardsMoveStruct> cardsMoves, bool visible, bool guanxing);

    QList<CardsMoveStruct> normalizeMoves(QList<CardsMoveStruct> cardsMoves);

    void moveCardsToEndOfDrawpile(ServerPlayer *player, QList<int> cardIds,
                                  const QString &skillName, bool visible, bool guanxing);
    void moveCardsInToDrawpile(ServerPlayer *player, const Card *card,
                               const QString &skillName, int n, bool visible);
    void moveCardsInToDrawpile(ServerPlayer *player, int cardId,
                               const QString &skillName, int n, bool visible);
    void moveCardsInToDrawpile(ServerPlayer *player, QList<int> cardIds,
                               const QString &skillName, int n, bool visible);
    void shuffleIntoDrawPile(ServerPlayer *player, QList<int> cardIds,
                             const QString &skillName, bool visible);
    void removeDerivativeCards();

private:
    friend class Room;
    friend struct CardMovementServiceTestAccess;

    struct _MoveSourceClassifier
    {
        explicit _MoveSourceClassifier(const CardsMoveStruct &move);
        void copyTo(CardsMoveStruct &move) const;
        bool operator==(const _MoveSourceClassifier &other) const;
        bool operator<(const _MoveSourceClassifier &other) const;

        Player *m_from;
        Player::Place m_from_place;
        QString m_from_pile_name;
        QString m_from_player_name;
    };

    struct _MoveMergeClassifier
    {
        explicit _MoveMergeClassifier(const CardsMoveStruct &move);
        bool operator==(const _MoveMergeClassifier &other) const;
        bool operator<(const _MoveMergeClassifier &other) const;

        Player *m_from;
        Player *m_to;
        Player::Place m_to_place;
        QString m_to_pile_name;
        CardMoveReason m_reason;
        bool m_is_last_handcard;
    };

    struct _MoveSeparateClassifier
    {
        _MoveSeparateClassifier(const CardsMoveOneTimeStruct &moveOneTime, int index);
        bool operator==(const _MoveSeparateClassifier &other) const;
        bool operator<(const _MoveSeparateClassifier &other) const;

        Player *m_from;
        Player *m_to;
        Player::Place m_from_place;
        Player::Place m_to_place;
        QString m_from_pile_name;
        QString m_to_pile_name;
        bool m_open;
        CardMoveReason m_reason;
    };

    void fillMoveInfo(CardsMoveStruct &move, int id) const;
    QList<CardsMoveOneTimeStruct> mergeMoves(QList<CardsMoveStruct> cardsMoves);
    QList<CardsMoveStruct> splitMoves(QList<CardsMoveOneTimeStruct> moveOneTimes);
    QList<int> &primaryPile();

    Room &m_room;
    CardLocationIndex m_locations;
    QList<int> m_pile1;
    QList<int> m_pile2;
    QList<int> m_tableCards;
    QList<int> *m_drawPile;
    QList<int> *m_discardPile;
};

#endif
