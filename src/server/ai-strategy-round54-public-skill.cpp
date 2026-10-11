#include "ai-strategy-round54-public-skill.h"
#include "engine.h"
#include "skill.h"
#include "skill-instance-utils.h"
#include <QMetaObject>
#include <QVariant>
namespace {
// Public definition identity, not a private hasSkill result. There is no Lua,
// native correction, event emission or yield between proof and source branch.
// QObject dynamic properties cannot invoke authored property getters. Declared
// Q_PROPERTY values and custom QVariant conversions are deliberately refused.
bool ignore(const Skill*s,bool&safe){
 safe=false;if(!s||s->metaObject()->indexOfProperty("IgnoreInvalidity")>=0)return false;
 const QVariant v=s->QObject::property("IgnoreInvalidity");
 if(!v.isValid()){safe=true;return false;}
 if(v.userType()!=QMetaType::Bool)return false;
 safe=true;return v.toBool();
}
const Skill*definition(const QString&name){QString base;SkillInstanceUtils::parseName(name,base);return Sanguosha->getSkill(base);}
bool has(const Skill*s){
 // A publicly absent definition returns after the SSOT ownership test, before
 // Invalidity in every possible private ownership branch.
 if(!s)return true;
 if(s->isAttachedLordSkill())return true; // first source OR operand, no property read
 bool safe=false;const bool ignored=ignore(s,safe);
 return safe&&(ignored||!s->isVisible());
}
bool current(const Skill*s){return !s||Sanguosha->getSkill(s->objectName())==s;}
}
bool aiRound54PublicHasSkill(const QString&name,bool compound) noexcept {
 try{if(!Sanguosha||!Sanguosha->currentRoom())return false;
  if(!compound)return has(definition(name));
  // Prove the complete public conjunction/disjunction union; never inspect
  // which branch the player's private ownership would short circuit.
  for(const auto&term:name.split('|'))for(const auto&part:term.split('+'))if(!has(definition(part)))return false;
  return true;
 }catch(...){return false;}
}
bool aiRound54PublicHasSkill(const Skill*s,bool includeLose) noexcept {
 try{return Sanguosha&&Sanguosha->currentRoom()&&current(s)&&(!s||(!s->isLordSkill()&&(includeLose||has(s))));}catch(...){return false;}
}
bool aiRound54PublicIsSkillInvalid(const Skill*s) noexcept {
 try{if(!Sanguosha||!Sanguosha->currentRoom()||!current(s))return false;
  if(!s)return true;bool safe=false;return ignore(s,safe)&&safe;
 }catch(...){return false;}
}
bool aiRound54PublicIsSkillInvalid(const QString&name) noexcept {
 try{return Sanguosha&&Sanguosha->currentRoom()&&aiRound54PublicIsSkillInvalid(Sanguosha->getSkill(name));}catch(...){return false;}
}
