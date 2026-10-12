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
bool hasAIPlayerEmptyMarksOrigin(const Player&) noexcept;
bool hasAIPlayerEmptySkillsOrigin(const Player&) noexcept;
// Actual finite public ol_liubei/sujiang preparePlayers source, issued once.
// All skill/general writer attempts retire it; it is not an execution grant.
bool hasAIPlayerPublicGeneralSkillsOrigin(const Player&) noexcept;
void retireAIPlayerMarksOrigin(const Player*) noexcept;
void retireAIPlayerSkillsOrigin(const Player*) noexcept;
// Structural source ancestry only. All three containers were default-born
// empty and have had no writer attempt. This is not metadata/effect authority.
bool hasAIPlayerEmptyPublicCardsOrigin(const Player&) noexcept;
void retireAIPlayerPublicCardsOrigin(const Player*) noexcept;
// Source-owned hand additions only. This fact is independent of hand contents,
// and never recovers after an unknown writer or canonical map replacement.
// Receiver lifetime and coherent native execution remain separate obligations.
bool hasAIPlayerCanonicalHandOrigin(const Player&) noexcept;
void retireAIPlayerCanonicalHandOrigin(const Player*) noexcept;
// Constructor-empty limitation source. Every public writer attempt retires it,
// including clears/removals/copy, regardless of current contents or equality.
bool hasAIPlayerEmptyLimitationsOrigin(const Player&) noexcept;
void retireAIPlayerLimitationsOrigin(const Player*) noexcept;
#endif
