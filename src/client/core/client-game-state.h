#ifndef CLIENT_GAME_STATE_H
#define CLIENT_GAME_STATE_H

// ClientCore 持有嘅客戶端遊戲狀態。
//
// 只依賴 Qt Core,亦刻意唔認識 engine 型別:呢度唔係第二個 RoomState,而係
// 「驗證 reply 同 snapshot 需要嘅最小事實」——邊個係自己、局入面有邊啲玩家、
// 卡 id 值域去到邊。真正嘅牌堆／技能狀態仍然由 Client 嘅 RoomState 保管。
//
// 為咗唔會有 false rejection,呢度嘅事實一律係「寬」嘅:未知就唔攔。

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

class ClientGameState
{
public:
    void reset();

    void setSelfName(const QString &name);
    QString selfName() const { return m_selfName; }

    // 座位次序,同 Client::getPlayers() 一致。
    void setPlayerNames(const QStringList &names);
    QStringList playerNames() const { return m_playerNames; }
    void addPlayer(const QString &name);
    void removePlayer(const QString &name);
    bool hasPlayer(const QString &name) const;

    void setPlayerAlive(const QString &name, bool alive);
    bool isPlayerAlive(const QString &name) const;

    // Engine::getCardCount()。卡 id 係卡表嘅 index,所以任何合法 id 都細過佢。
    // 0 或負數 = 未知,咁樣就淨係攔負數 id。
    void setCardIdSpace(int count);
    int cardIdSpace() const { return m_cardIdSpace; }
    bool isKnownCardId(int cardId) const;

    QJsonObject toJson() const;

private:
    QString m_selfName;
    QStringList m_playerNames;
    QHash<QString, bool> m_alive;
    int m_cardIdSpace = 0;
};

#endif
