#ifndef AI_STRATEGY_ROUND54_PUBLIC_SKILL_H
#define AI_STRATEGY_ROUND54_PUBLIC_SKILL_H
#include <QString>
class Skill;
// Native-only effect proofs. They never inspect a player's ownership/state.
bool aiRound54PublicHasSkill(const QString&name,bool compound=false) noexcept;
bool aiRound54PublicHasSkill(const Skill*,bool includeLose=false) noexcept;
bool aiRound54PublicIsSkillInvalid(const QString&name) noexcept;
bool aiRound54PublicIsSkillInvalid(const Skill*) noexcept;
#endif
