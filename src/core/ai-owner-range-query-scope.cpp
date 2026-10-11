#include "ai-owner-range-query-scope.h"

thread_local AIOwnerRangeQueryScope *AIOwnerRangeQueryScope::s_current = nullptr;

AIOwnerRangeQueryScope::AIOwnerRangeQueryScope(const Player *owner, Flag flag, bool value, const QString &sourceKey, bool observerOnly)
    : m_owner(owner), m_flag(flag), m_value(value), m_sourceKey(sourceKey), m_observerOnly(observerOnly), m_previous(s_current)
{
    s_current = this;
}

AIOwnerRangeQueryScope::~AIOwnerRangeQueryScope() { s_current = m_previous; }

bool AIOwnerRangeQueryScope::lookup(const Player *owner, const QString &flag, bool &value)
{
    Flag kind;
    if (flag == QLatin1String("InfinityAttackRange")) kind = InfinityAttackRange;
    else if (flag == QLatin1String("slashNoDistanceLimit")) kind = SlashNoDistanceLimit;
    else return false;
    for (auto *scope = s_current; scope; scope = scope->m_previous) {
        if (scope->m_owner == owner && scope->m_flag == kind && !scope->m_observerOnly) {
            value = scope->m_value;
            return true;
        }
    }
    return false;
}

QString AIOwnerRangeQueryScope::sourceKey(const Player *owner)
{
    for(auto *scope=s_current;scope;scope=scope->m_previous)
        if(scope->m_owner==owner)return scope->m_sourceKey;
    return {};
}
