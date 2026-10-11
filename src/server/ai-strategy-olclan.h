#ifndef AI_STRATEGY_OLCLAN_H
#define AI_STRATEGY_OLCLAN_H
#include "ai.h"

// Read-only evaluation authorization, never payment/submit authority. The caller
// must separately bind this immutable request to its active viewer and revision.
// No live cards, private tags, piles or Lua callbacks are read here.
bool authorizeAIOlClanNotifiedEvaluation(const AIRequest &request, int cardId,
    QJsonObject *disclosedCard = nullptr, QString *error = nullptr);
// Exact two zero-selection marked physical producers. Validates immutable
// viewer/mark/current disclosed face and presence of the expected native skill
// scope. The caller still validates the selected activation/source refs, quotas,
// current Room revision, createCard result, limits and target legality. This
// function reads no live card, Room tag or historical AG guess.
bool authorizeAIOlClanMarkedPhysicalProduct(const AIRequest &request, int productId,
    QJsonObject *disclosedCard = nullptr, bool *printedProduct = nullptr,
    QString *error = nullptr);
#endif
