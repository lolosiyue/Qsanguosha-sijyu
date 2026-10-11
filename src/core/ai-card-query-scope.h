#ifndef AI_CARD_QUERY_SCOPE_H
#define AI_CARD_QUERY_SCOPE_H
class Card;
class Room;
class RoomState;
class ServerPlayer;
// A narrowly scoped shadow for owner-authorized FilterSkill evaluation. Native
// callbacks see a disposable card and hypothetical mapping, never the live card.
class AICardQueryScope final {
public:
    AICardQueryScope(const Room *, const RoomState *, int id, Card *shadow, ServerPlayer *owner, int place);
    ~AICardQueryScope();
    static Card *card(const RoomState *, int id, Card *actual);
    static ServerPlayer *owner(const Room *, int id, ServerPlayer *actual);
    static int place(const Room *, int id, int actual);
    AICardQueryScope(const AICardQueryScope &) = delete;
    AICardQueryScope &operator=(const AICardQueryScope &) = delete;
private:
    const Room *m_room;
    const RoomState *m_state;
    int m_id;
    Card *m_shadow;
    ServerPlayer *m_owner;
    int m_place;
    AICardQueryScope *m_previous;
    static thread_local AICardQueryScope *s_current;
};
// Original TLS source history, not current-stack emptiness. Once a shadow
// scope has been attempted on this thread, destruction never restores this bit.
bool hasAIRound56CardQueryOriginalSourceOrigin() noexcept;
#endif
