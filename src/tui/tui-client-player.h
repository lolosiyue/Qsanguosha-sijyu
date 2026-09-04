#ifndef TUI_CLIENT_PLAYER_H
#define TUI_CLIENT_PLAYER_H

// player.h names QObject/QString without including them.
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include "player.h"

class ClientGameState;
class TuiPlayerModel;

// The engine's own Self, defined in src/core/player-self.cpp and written
// through setEngineSelf(). The desktop reaches it via clientplayer.h's
// QSAN_ENGINE_BUILD branch, which the text client must not include.
namespace QSanEngine {
extern Player *Self;
}

// Named ClientPlayer, at global scope, on purpose. The engine asks
// inherits("ClientPlayer") in fifteen places to decide whether it is talking to
// a client: Player::distanceTo() then reads the distance the server pushed as a
// distanceTo_<name> property instead of walking distance skills, and the
// correct*() / isProhibited() / isCardLimited() family answers with a cached or
// degraded value rather than blocking on the Lua mutex a UI thread must not
// wait for. A differently named subclass silently takes the *server* path
// through all of it.
//
// This is a different class from the desktop's src/client/clientplayer.h. The
// two never appear in one binary and the text client must not include that
// header, which declares a ClientPlayer(Client *) and its own global Self.
class ClientPlayer final : public Player
{
    Q_OBJECT

public:
    ClientPlayer(const ClientGameState *state, const TuiPlayerModel *model,
                 QObject *parent = nullptr);

    int aliveCount(bool includeRemoved = false) const override;
    QString getGameMode() const override;
    Player *getNextAlive(int n = 1) const override;
    Player *getLastAlive(int n = 1) const override;
    int getHandcardNum() const override;
    QList<const Card *> getHandcards() const override;
    // The server computes the hand limit and ships it in PlayerUIState; the
    // desktop's ClientPlayer returns that value too rather than re-deriving it.
    int getMaxCards() const override;

private:
    // Walks the alive seat ring; a negative step goes counter-clockwise.
    Player *seatStep(int step) const;

    const ClientGameState *m_state;
    const TuiPlayerModel *m_model;
};

// Keeps one ClientPlayer per seat in step with ClientGameState and tells the
// engine which of them is us.
class TuiPlayerModel
{
public:
    explicit TuiPlayerModel(const ClientGameState *state);
    ~TuiPlayerModel();

    // Rebuilds every player from the current state. Cheap enough to run on each
    // state change: the reduced state is the truth, so there is no incremental
    // bookkeeping to get wrong.
    void sync();
    void clear();

    ClientPlayer *player(const QString &objectName) const;
    ClientPlayer *self() const;
    const Player *cardOwner(int cardId) const;

private:
    // What was last written into a player, so a sync can skip everything the
    // server did not touch. Applying the whole map on every message means
    // re-adding every skill and re-equipping every card several hundred times a
    // game, which is slow enough to be felt.
    struct Entry
    {
        ClientPlayer *player = nullptr;
        QVariantMap applied;
        QList<int> equipped;
    };

    void syncPlayer(Entry *entry, const QVariantMap &data, const QList<int> &equipped);

    const ClientGameState *m_state;
    // Player::getSiblings() is parent()->findChildren<Player *>(), so every
    // player has to hang off one common parent or nobody can see anybody --
    // Slash::IsAvailable() and the rest of the engine ask that way. The desktop
    // gets this for free by parenting each ClientPlayer to its Client.
    QObject m_root;
    QHash<QString, Entry> m_players;
    QString m_selfName;
};

#endif
