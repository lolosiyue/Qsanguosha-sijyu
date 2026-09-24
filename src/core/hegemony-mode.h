#ifndef _HEGEMONY_MODE_H
#define _HEGEMONY_MODE_H

#include <QString>

class Player;

// In-game national-war rules. Seat draft lives on RoomThreadHegemony;
// turn flow stays on HegemonyRule. Callers ask this layer instead of
// re-deriving faction, reveal, and preshow from the global flag.
class HegemonyMode
{
public:
    // Server configuration. Client-only views use enabledFor / enabledForCatalog.
    static bool enabled();
    static bool enabledFor(const Player *player);
    static bool enabledForCatalog(bool inRoom);

    static QString mappedRole(const QString &kingdom);
    static QString seemingKingdom(const Player *player);
    static bool isFriendWith(const Player *self, const Player *other, bool considerAnjiang);
    static bool willBeFriendWith(const Player *self, const Player *other);
    static bool hasShownSkill(const Player *player, const QString &skillName);

    // bindHead 1 is the head general, 2 is the deputy. Any other slot is neither.
    static bool innateGeneralShown(const Player *owner, int bindHead);
    static bool innateGeneralConcealed(const Player *owner, int bindHead);
};

#endif
