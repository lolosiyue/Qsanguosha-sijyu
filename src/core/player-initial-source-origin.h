#ifndef PLAYER_INITIAL_SOURCE_ORIGIN_H
#define PLAYER_INITIAL_SOURCE_ORIGIN_H
class Player;
// Native source-history facts only, NEVER sufficient authorization to export a
// foreign value or to choose a policy path. Consumers must FIRST prove the full
// current R56 request, synchronous graph and async lineage, plus public roster
// and skill visibility. Unknown ingress retires that enclosing proof before
// private eligibility. These APIs read no marks/skills/current private values.
// A fresh Player's actual default-constructed maps issue the only receipt.
// Any writer attempt, including equal, zero, removal, failed eligibility and
// whole-map replacement, clears it permanently. No public begin/reseed API.
bool hasAIRound56PlayerEmptyMarksOrigin(const Player&) noexcept;
bool hasAIRound56PlayerEmptySkillsOrigin(const Player&) noexcept;
void retireAIRound56PlayerMarksOrigin(const Player*) noexcept;
void retireAIRound56PlayerSkillsOrigin(const Player*) noexcept;
// Structural source ancestry only. All three containers were default-born
// empty and have had no writer attempt. This is not metadata/effect authority.
bool hasAIRound56PlayerEmptyPublicCardsOrigin(const Player&) noexcept;
void retireAIRound56PlayerPublicCardsOrigin(const Player*) noexcept;
#endif
