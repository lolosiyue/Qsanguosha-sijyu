#ifndef CLIENT_PLAYER_MODEL_H
#define CLIENT_PLAYER_MODEL_H

// card.h supplies the Qt prerequisites required by player.h/general.h when this
// header is included outside the desktop precompiled-header path.
#include "card.h"
#include "player.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>

class ClientGameState;
class ClientPlayerModel;

// The engine's own Self, defined in src/core/player-self.cpp and written
// through setEngineSelf(). Runtime frontends use this bridge instead of the
// desktop ClientPlayer global.
namespace QSanEngine {
extern Player *Self;
}

// Keep this exact global Qt meta-object name. Engine client-side branches use
// inherits("ClientPlayer") to select cached/client-visible rule paths rather
// than server-only evaluation. Desktop ClientPlayer and this runtime class are
// never linked into the same product.
class ClientPlayer final : public Player
{
    Q_OBJECT

public:
    ClientPlayer(const ClientGameState *state, const ClientPlayerModel *model,
                 QObject *parent = nullptr);

    int aliveCount(bool includeRemoved = false) const override;
    QString getGameMode() const override;
    Player *getNextAlive(int n = 1) const override;
    Player *getLastAlive(int n = 1) const override;
    int getHandcardNum() const override;
    QList<const Card *> getHandcards() const override;
    int getMaxCards() const override;

    // Full visible snapshots used by the browser runtime. The incremental TUI
    // projection keeps its existing behavior until it adopts this boundary.
    void applyVisibleZones(const QVariantMap &data);

private:
    Player *seatStep(int step) const;

    const ClientGameState *m_state;
    const ClientPlayerModel *m_model;
};

// Presentation-neutral projection of ClientGameState into the Player object
// graph expected by native card/skill rules. TUI, native fixtures and the
// future WASM runtime all use this same model.
class ClientPlayerModel
{
public:
    explicit ClientPlayerModel(const ClientGameState *state);
    ~ClientPlayerModel();

    void sync();
    void clear();

    ClientPlayer *player(const QString &objectName) const;
    ClientPlayer *self() const;
    const Player *cardOwner(int cardId) const;

private:
    struct Entry
    {
        ClientPlayer *player = nullptr;
        QVariantMap applied;
        QList<int> equipped;
    };

    void syncPlayer(Entry *entry, const QVariantMap &data, const QList<int> &equipped);

    const ClientGameState *m_state;
    // Player::getSiblings() walks parent()->findChildren<Player *>(), so all
    // projected players must share one QObject parent.
    QObject m_root;
    QHash<QString, Entry> m_players;
    QString m_selfName;
};

#endif
