#include "ai-card-query-scope.h"
namespace { thread_local bool originalCardQuerySource = true; }
bool hasAIRound56CardQueryOriginalSourceOrigin() noexcept { return originalCardQuerySource; }
thread_local AICardQueryScope *AICardQueryScope::s_current = nullptr;
AICardQueryScope::AICardQueryScope(const Room *room, const RoomState *state, int id, Card *shadow,
                                 ServerPlayer *owner, int place)
    : m_room(room), m_state(state), m_id(id), m_shadow(shadow), m_owner(owner), m_place(place), m_previous(s_current)
{ originalCardQuerySource = false; s_current = this; }
AICardQueryScope::~AICardQueryScope() { s_current = m_previous; }
Card *AICardQueryScope::card(const RoomState *state, int id, Card *actual)
{
    for (auto *scope = s_current; scope; scope = scope->m_previous)
        if (scope->m_state == state && scope->m_id == id) return scope->m_shadow;
    return actual;
}
ServerPlayer *AICardQueryScope::owner(const Room *room, int id, ServerPlayer *actual)
{
    for (auto *scope = s_current; scope; scope = scope->m_previous)
        if (scope->m_room == room && scope->m_id == id) return scope->m_owner;
    return actual;
}
int AICardQueryScope::place(const Room *room, int id, int actual)
{
    for (auto *scope = s_current; scope; scope = scope->m_previous)
        if (scope->m_room == room && scope->m_id == id) return scope->m_place;
    return actual;
}
