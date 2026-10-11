#ifndef AI_OWNER_RANGE_QUERY_SCOPE_H
#define AI_OWNER_RANGE_QUERY_SCOPE_H

#include <QString>

class Player;

// Read-only owner hypothesis. No Player field, signal or revision is changed.
// Unlike Tianyi this scope contributes no targets or target-mod skill identity.
class AIOwnerRangeQueryScope final {
public:
    enum Flag { InfinityAttackRange, SlashNoDistanceLimit };
    AIOwnerRangeQueryScope(const Player *owner, Flag flag, bool value = true, const QString &sourceKey = {}, bool observerOnly = false);
    ~AIOwnerRangeQueryScope();
    static bool lookup(const Player *owner, const QString &flag, bool &value);
    static QString sourceKey(const Player *owner);
    static bool isActive() { return s_current != nullptr; }
    AIOwnerRangeQueryScope(const AIOwnerRangeQueryScope &) = delete;
    AIOwnerRangeQueryScope &operator=(const AIOwnerRangeQueryScope &) = delete;
private:
    const Player *m_owner;
    Flag m_flag;
    bool m_value;
    QString m_sourceKey;
    bool m_observerOnly;
    AIOwnerRangeQueryScope *m_previous;
    static thread_local AIOwnerRangeQueryScope *s_current;
};

#endif
