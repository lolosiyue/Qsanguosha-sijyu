#ifndef TUI_ROOM_CONTEXT_H
#define TUI_ROOM_CONTEXT_H

// The engine headers below name QObject/QString without including them; every
// consumer is expected to have pulled Qt in first.
#include <QObject>
#include <QVariantMap>

#include <functional>

#include "engine-runtime-context.h"
#include "room-state.h"

class ClientGameState;

namespace QSanProtocol {
struct ProtocolMessage;
}

// The engine answers getCard() / getCardOwner() / getCardPlace() through
// whichever EngineRuntimeContext is registered for the calling thread. The
// desktop client is its own context (Client::startGame() calls registerRoom);
// the text client registered nothing, so every one of those queries came back
// null here and anything built on them degraded silently -- a card the server
// had rewritten still rendered with its original printed face.
//
// This is the text client's context. It owns the same client-side RoomState the
// desktop keeps, so the cards the engine hands out are the room's live copies.
class TuiRoomContext final : public QObject, public EngineRuntimeContext
{
public:
    explicit TuiRoomContext(const ClientGameState *state, QObject *parent = nullptr);
    ~TuiRoomContext() override;

    // Mirrors Client::startGame() / Client::gameOver(): registers for the
    // calling thread and clones the engine card table into the room, then hands
    // the thread back when the game ends.
    void enterGame();
    void leaveGame();
    bool isActive() const;

    // GAME_START, GAME_OVER and UPDATE_CARD are the three messages that own the
    // room's card table; everything else is ignored.
    void applyMessage(const QSanProtocol::ProtocolMessage &message);

    // Who holds a card is a question only the client-side player model can
    // answer; without one the engine gets the same null it always got here.
    using OwnerResolver = std::function<const Player *(int cardId)>;
    void setOwnerResolver(OwnerResolver resolver);

    // Card legality reads these off the room: Slash::targetFilter() only counts
    // the per-turn slash limit when the reason is PLAY, and view-as skills ask
    // for the pattern. The desktop keeps them in step with its own status
    // (Client::setStatus / Client::activate); here they follow the request the
    // player is answering.
    void setCardUseContext(CardUseStruct::CardUseReason reason, const QString &pattern);

    QObject *runtimeObject() override;
    RoomState *roomState() override;
    const Player *cardOwner(int cardId) const override;
    Player::Place cardPlace(int cardId) const override;
    Card *card(int cardId) const override;

private:
    void applyCardUpdate(const QVariantMap &payload);

    const ClientGameState *m_state;
    OwnerResolver m_ownerResolver;
    RoomState m_roomState;
    bool m_active = false;
};

#endif
