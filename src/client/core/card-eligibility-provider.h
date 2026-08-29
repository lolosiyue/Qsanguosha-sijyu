#ifndef CLIENT_CARD_ELIGIBILITY_PROVIDER_H
#define CLIENT_CARD_ELIGIBILITY_PROVIDER_H

#include "interaction-model.h"

struct CardEligibilityResult
{
    QList<int> suggestedCards;
    QList<int> suggestedDisabledCards;
    QString diagnostic;
};

// Optional adapter for engine-dependent card filtering. ClientCore owns no
// gameplay rules and treats provider output as presentation hints only.
class ICardEligibilityProvider
{
public:
    virtual ~ICardEligibilityProvider() = default;
    virtual CardEligibilityResult resolve(const InteractionRequest &request) const = 0;
};

#endif
