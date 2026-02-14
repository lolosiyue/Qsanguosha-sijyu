#ifndef _CLIENT_PLAYER_H
#define _CLIENT_PLAYER_H

#include "player.h"
//#include "clientstruct.h"
#include "json.h"

class Client;
class QTextDocument;

class ClientPlayer : public Player
{
    Q_OBJECT
    //Q_PROPERTY(int handcard READ getHandcardNum WRITE setHandcardNum)

public:
    explicit ClientPlayer(Client *client);
    //QList<const Card *> getHandcards() const;
    QList<int> handCards() const;
    void addHandIds(JsonArray args);
    void removeHandIds(JsonArray args);
    void setKnownCards(QList<int> card_ids);
    void setKnownCards(QList<const Card*> cards);
    QList<const Card *> getKnownCards() const;
    QTextDocument *getMarkDoc() const;
    void changePile(const QString &name, bool add, QList<int> card_ids);
    QString getDeathPixmapPath() const;
    //void setHandcardNum(int n);
    QString getGameMode() const;

    void setFlags(const QString &flag);
    int aliveCount() const;
    //int getHandcardNum() const;
    void removeCard(int id, Place place);
    void addCard(int id, Place place);
    void addKnownHandCard(const Card *card);
    //bool isLastHandCard(const Card *card, bool contain = false) const;
    void setMark(const QString &mark, int value);

private:
    int handcard_num;
    QList<const Card *> known_cards;
    QList<int> hand_ids;
    QTextDocument *mark_doc;

signals:
    void pile_changed(const QString &name);
    void drank_changed();
    void action_taken();
    void duanchang_invoked();
    void Mark_changed(const QString &name, int mark_num);
};

extern ClientPlayer *Self;

#endif

