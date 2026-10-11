#include "ai-strategy-round44-range.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-owner-range-query-scope.h"
#include "ai-strategy-card-identity.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <cmath>
#include <limits>
namespace {
bool owner(const AIRequest&r,Room&room,const ServerPlayer*p){return p&&p->getRoom()==&room&&!room.roomRuntime()->isClosing()&&r.decisionId!=0&&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()&&aiRound44RangeRequestCurrent(room,r,p);}
const Card* hand(const AIRequest&r,Room&room,const ServerPlayer*p,int id){
 if(id<0||room.getCardOwner(id)!=p||room.getCardPlace(id)!=Player::PlaceHand||!p->handCards().contains(id))return nullptr;
 const Card*c=Sanguosha->getCard(id);if(!c)return nullptr;
 for(const auto&v:r.worldView.handCards)if(v.effectiveId==id&&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c))return c;
 return nullptr;
}
struct Tuple {QString skill;int suit;int number;int costs;};
const QMap<QString,Tuple>&tuples(){static const QMap<QString,Tuple>v{
 {"sfofl_sitong",{"sfofl_sitong",int(Card::NoSuit),0,0}},
 {"sfofl_jiaozheng",{"sfofl_jiaozheng",int(Card::NoSuit),0,0}},
 {"sfofl_qingjin",{"sfofl_qingjin",int(Card::NoSuit),0,0}},
 {"BearingDownBorder",{"BearingDownBorder",int(Card::SuitToBeDecided),-1,0}},
 {"@sfofl_bearing_down_border",{"BearingDownBorder",int(Card::SuitToBeDecided),-1,1}},
 {"sfofl_jubing",{"_sfofl_jubing",int(Card::SuitToBeDecided),-1,0}},
 {"@@sfofl_qifeng",{"sfofl_qifeng",int(Card::NoSuit),0,0}},
 {"sfofl_juedou",{"sfofl_juedou",int(Card::NoSuit),0,0}}};return v;}
}
int aiRound44RangeExtra(const QString&source){return source=="mouwusheng"||source=="sfofl_sitong"||source=="sfofl_qingjin"||source=="BearingDownBorder"?99:source=="#sfofl_sixi"?1:0;}
bool aiRound44RangeConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!owner(r,room,p))return false;
 const QString source=AIOwnerRangeQueryScope::sourceKey(p);const auto it=tuples().constFind(source);
 if(it==tuples().cend())return false;
 static const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto j=s.begin();j!=s.end();++j)if(!fields.contains(j.key()))return false;
 const auto&t=it.value();
 if(s.value("source_key")!=QJsonValue("owner_range:"+source)||s.value("name")!="slash"||s.value("skill")!=t.skill||s.value("suit")!=t.suit||s.value("number")!=t.number||!s.value("subcards").isArray())return false;
 const auto ids=s.value("subcards").toArray();if(ids.size()!=t.costs)return false;
 for(const auto&v:ids){const double n=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(n)||n<0||n!=std::floor(n)||n>std::numeric_limits<int>::max()||!hand(r,room,p,int(n)))return false;}
 return true;
}
QJsonObject writeAIRound44RangeQinggang(const AIRequest&r,Room&room,ServerPlayer*p,const QJsonObject&s){
 const auto fail=[](const char*e){return QJsonObject{{"error",e},{"key","owner_range.Qinggang"}};};
 if(!owner(r,room,p)||r.kind!=AIRequest::UseCard||r.pattern!="@@f_yonghunSlash"||AIOwnerRangeQueryScope::sourceKey(p)!="@@f_yonghunSlash")return fail("Qinggang requires its actual owner source scope");
 if(s.size()!=3||s.value("card_flag")!="Qinggang"||!s.value("enabled").isBool()||!s.value("id").isDouble())return fail("Qinggang source fields differ");
 const double n=s.value("id").toDouble(-1);if(n<0||n!=std::floor(n)||n>std::numeric_limits<int>::max())return fail("Qinggang source ID invalid");
 const Card*c=hand(r,room,p,int(n));if(!c||!c->isKindOf("Slash")||!c->hasFlag("f_yonghun"))return fail("Qinggang source requires its current marked owner Slash");
 // Fixed Genius468-472 changes this very physical Card, not a cloned Slash.
 // Card::setFlags has no room-property signal/revision/payment side effect.
 const auto before=room.roomRuntime()->stateRevision();
 c->setFlags(s.value("enabled").toBool()?"Qinggang":"-Qinggang");
 if(room.roomRuntime()->stateRevision()!=before||!aiRound44RangeRequestCurrent(room,r,p))return fail("Qinggang setter unexpectedly changed native revision");
 return{{"kind","owner_range_card_flag"},{"viewer",p->objectName()},{"decision_id",r.getDecisionId()},{"state_revision",r.getStateRevision()},{"id",int(n)},{"enabled",s.value("enabled")}};
}
