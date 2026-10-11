#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round53-ainoto.h"
#include "ai-strategy-round46-inovation.h"
#include "ai-strategy-round50-effects.h"
#include "ai-strategy-round45-inovation-state.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "standard.h"
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QWeakPointer>
#include <limits>

thread_local AIRound44RangeRequestAlias *AIRound44RangeRequestAlias::s_current=nullptr;
AIRound44RangeRequestAlias::AIRound44RangeRequestAlias(const AIRequest &original,const AIRequest &copy)
    :m_original(&original),m_copy(&copy),m_previous(s_current) {
    // Identity fields are immutable even in an audited native readonly copy.
    if(original.decisionId!=copy.decisionId||original.stateRevision!=copy.stateRevision
        ||original.viewerObjectName!=copy.viewerObjectName||original.conversionLedger!=copy.conversionLedger
        ||original.worldView.weaponRangeTracked!=copy.worldView.weaponRangeTracked
        ||original.worldView.weaponRangeRevision!=copy.worldView.weaponRangeRevision)
        m_copy=nullptr;
    s_current=this;
}
AIRound44RangeRequestAlias::~AIRound44RangeRequestAlias(){s_current=m_previous;}
const AIRequest *AIRound44RangeRequestAlias::canonical(const AIRequest &r) {
    const AIRequest *value=&r;
    for(auto *scope=s_current;scope;scope=scope->m_previous)
        if(scope->m_copy==value)value=scope->m_original;
    return value;
}
const AIWorldView *AIRound44RangeRequestAlias::canonical(const AIWorldView &w) {
    const AIWorldView *value=&w;
    for(auto *scope=s_current;scope;scope=scope->m_previous)
        if(scope->m_copy && &scope->m_copy->worldView==value)value=&scope->m_original->worldView;
    return value;
}

namespace {
struct RetiredRangeDecision {
    quint64 decision=0;
    QWeakPointer<AIConversionLedger> ledger;
    const AIRequest *request=nullptr;
    const AIWorldView *world=nullptr;
};
class RangeCertificate final : public QObject {
public:
    explicit RangeCertificate(Room &r) : QObject(&r) {}
    QPointer<ServerPlayer> owner;
    const AIRequest *request=nullptr;
    const AIWorldView *world=nullptr;
    const void *luaRequest=nullptr;
    QSharedPointer<AIConversionLedger> ledger;
    const void *ledgerIdentity=nullptr;
    quint64 generation=0;
    quint64 decision=0, original=0, expected=0, transitions=0, writes=0;
    QString viewer;
    bool active=false, valid=false, writing=false;
    QSet<QString> expectedFlags;
    QList<RetiredRangeDecision> retired;
};
RangeCertificate *find(Room &r, bool create=false) {
    for(QObject *child:r.children()) if(auto *p=dynamic_cast<RangeCertificate *>(child))return p;
    return create?new RangeCertificate(r):nullptr;
}
void retire(RangeCertificate *s) {
    for(auto it=s->retired.begin();it!=s->retired.end();)
        if(it->ledger.isNull())it=s->retired.erase(it);else ++it;
    if(s->request && s->ledger && s->decision) {
        for(const auto &old:s->retired)if(old.decision==s->decision)return;
        s->retired.append({s->decision,s->ledger.toWeakRef(),s->request,s->world});
    }
}
bool retired(const RangeCertificate *s,const AIRequest &r) {
    if(!s)return false;
    for(const auto &old:s->retired)if(old.decision==r.decisionId) {
        const auto ledger=old.ledger.toStrongRef();
        if(ledger && (ledger==r.conversionLedger||old.request==AIRound44RangeRequestAlias::canonical(r)))return true;
    }
    return false;
}
QSet<QString> flags(const ServerPlayer *p) {
    const auto values=p->getFlagList();return QSet<QString>(values.begin(),values.end());
}
bool weaponRangeCurrent(const AIWorldView &world) {
    return !world.weaponRangeTracked || Weapon::rangeRevisionCurrent(world.weaponRangeRevision);
}
bool matches(Room &room,const AIRequest &r,const RangeCertificate *s,const ServerPlayer *p) {
    return weaponRangeCurrent(r.worldView)&&s&&s->request==AIRound44RangeRequestAlias::canonical(r)&&s->valid&&!s->writing&&s->owner&&s->owner->getRoom()==&room
        &&(!p||p==s->owner)&&r.decisionId==s->decision&&r.stateRevision==s->original
        &&r.viewerObjectName==s->viewer&&r.worldView.self.objectName==s->viewer
        &&r.conversionLedger&&r.conversionLedger==s->ledger
        &&room.roomRuntime()->stateRevision()==s->expected
        &&flags(s->owner)==s->expectedFlags;
}
}
bool aiRound44RangeSource(const QString &flag,const QString &source) {
    static const QSet<QString> infinity{"GodShijun","kejieshengzhuihun","mouwusheng","@mouxianchou-slash",
        "@@f_yonghunSlash","BearingDownBorder","@sfofl_bearing_down_border","sfofl_jubing","@@sfofl_qifeng"};
    static const QSet<QString> noDistance{"shenghua","shijun","@@luazhuixi","@@luayonglue","@PlusZhongyong",
        "SixQiaoxie","#sfofl_shuangren","sfofl_sitong","sfofl_jiaozheng","sfofl_qingjin",
        "#sfofl_zainei","#sfofl_sixi","sfofl_juedou"};
    return (flag=="InfinityAttackRange"&&infinity.contains(source))
        ||(flag=="slashNoDistanceLimit"&&noDistance.contains(source));
}
void beginAIRound44RangeDecision(Room &room,const AIRequest &r,const void *luaRequest) {
    auto *s=find(room,true);retire(s);++s->generation;s->valid=false;s->active=false;s->writing=false;s->ledger.clear();
    auto *p=room.findPlayerByObjectName(r.viewerObjectName,true);
    if(!room.roomRuntime()||room.roomRuntime()->isClosing()||!p||!p->isAlive()||r.decisionId==0
        ||r.worldView.self.objectName!=r.viewerObjectName||!r.conversionLedger
        ||r.stateRevision!=room.roomRuntime()->stateRevision()||!weaponRangeCurrent(r.worldView))return;
    s->request=AIRound44RangeRequestAlias::canonical(r);s->world=&s->request->worldView;s->owner=p;s->viewer=r.viewerObjectName;s->ledger=r.conversionLedger;s->ledgerIdentity=r.conversionLedger.get();
    s->decision=r.decisionId;s->original=r.stateRevision;s->expected=r.stateRevision;
    s->luaRequest=luaRequest;s->transitions=0;s->writes=0;s->expectedFlags=flags(p);s->valid=true;s->active=true;
}
void finishAIRound44RangeDecision(Room &room,const AIRequest &r) {
    auto *s=find(room);if(!s||s->request!=AIRound44RangeRequestAlias::canonical(r)||s->decision!=r.decisionId)return;
    if(!matches(room,r,s,nullptr))s->valid=false;
    s->active=false;
}
void invalidateAIRound44RangeDecision(Room &room) {
    if(auto *s=find(room)){retire(s);++s->generation;s->valid=false;s->active=false;s->ledger.clear();}
}
bool aiRound44RangeRequestCurrent(Room &room,const AIRequest &r,const ServerPlayer *p) {
    if(!weaponRangeCurrent(r.worldView))return false;
    if(!room.roomRuntime()||room.roomRuntime()->isClosing())return false;
    // Existing strict requests remain strict. A matching poisoned certificate
    // cannot be revived by a no-op or a reused original stamp.
    const auto *s=find(room);
    if(retired(s,r))return false;
    if(s&&s->decision==r.decisionId
        &&(s->ledgerIdentity==r.conversionLedger.get()||s->request==AIRound44RangeRequestAlias::canonical(r)))
        return matches(room,r,s,p);
    return (!p||(p->getRoom()==&room&&p->objectName()==r.viewerObjectName))
        &&r.stateRevision==room.roomRuntime()->stateRevision();
}
bool aiRound44RangeDecisionActive(Room &room,const AIRequest &r,const ServerPlayer *p) {
    if(!room.roomRuntime()||room.roomRuntime()->isClosing())return false;
    const auto *s=find(room);
    return s&&s->active&&!retired(s,r)&&matches(room,r,s,p);
}
bool writeAIRound44RangeFlag(Room &room,const AIRequest &r,ServerPlayer *p,
    const QString &flag,const QString &source,bool value,QString *error) {
    auto fail=[&](const char *message){if(error)*error=QString::fromLatin1(message);return false;};
    auto *s=find(room);
    if(!aiRound44RangeSource(flag,source)||!s||!s->active||!matches(room,r,s,p))
        return fail("range flag source/request certificate is invalid");
    const bool before=p->getFlagList().contains(flag); // Never an RAII override.
    const quint64 revision=room.roomRuntime()->stateRevision();
    if(before!=value&&revision==std::numeric_limits<quint64>::max())return fail("range revision overflow");
    const auto generation=s->generation;
    s->writing=true;
    // Exact donor operation: ordinary public gameplay_property_changed signal
    // order is retained. No Room broadcast, synthetic effect or rollback.
    try { p->setFlags(value?flag:QStringLiteral("-")+flag); }
    catch(...) {if(s->generation==generation){s->writing=false;s->valid=false;}throw;}
    // A synchronous property observer may enter/finish a different decision or
    // invalidate this one. Never advance or overwrite that replacement session.
    if(s->generation!=generation)return fail("range write reentered a different native session");
    s->writing=false;++s->writes;
    QSet<QString> expected=s->expectedFlags;
    if(value&&!before)expected.insert(flag);
    else if(!value&&before)expected.remove(flag);
    const quint64 next=revision+(before!=value?1:0);
    if(room.roomRuntime()->stateRevision()!=next||flags(p)!=expected) {
        s->valid=false;return fail("range write caused an uncertified mutation");
    }
    s->expected=next;s->expectedFlags=expected;if(before!=value)++s->transitions;
    return true;
}

bool aiRound44RangeContinuation(Room &room,const AIRequest &r) {
    const auto *s=find(room);return s&&s->active&&s->writes>0&&matches(room,r,s,nullptr);
}

bool aiRound44RangeWorldCurrent(Room &room,const AIWorldView &world) {
    if(!weaponRangeCurrent(world))return false;
    if(!room.roomRuntime()||room.roomRuntime()->isClosing())return false;
    const auto *s=find(room);
    if(s&&s->world==AIRound44RangeRequestAlias::canonical(world))
        return s->valid&&!s->writing&&s->owner&&s->owner->getRoom()==&room
            &&world.self.objectName==s->viewer&&world.revision==s->original
            &&room.roomRuntime()->stateRevision()==s->expected&&flags(s->owner)==s->expectedFlags;
    if(s)for(const auto &old:s->retired)
        if(old.world==AIRound44RangeRequestAlias::canonical(world)&&!old.ledger.isNull())return false;
    return world.revision==room.roomRuntime()->stateRevision();
}

bool aiRound44RangeLuaRequestCurrent(Room &room,const AIRequest &r,const void *luaRequest) {
    const auto *s=find(room);return s&&s->active&&luaRequest&&luaRequest==s->luaRequest&&matches(room,r,s,nullptr);
}

// Exact source Room:setTag primitive; never a generic tag mutation endpoint.
bool writeAIRound45XinyangTag(Room &room,const AIRequest &r,ServerPlayer *p,int value,const AIRound45XinyangWritePermit &permit,QString *error) {
    auto fail=[&](const char *message){if(error)*error=QString::fromLatin1(message);return false;};
    auto*s=find(room);
    const bool setting=r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason=="xinyang_judge"&&value>=0;
    const bool resetting=r.kind==AIRequest::AmazingGrace&&r.choiceOptions.reason=="xinyang"&&value==-1;
    if((!setting&&!resetting)||!p||!permit.matches(room,r,p)||!s||!s->active||!matches(room,r,s,p))
        return fail("Xinyang source/request certificate invalid");
    if(setting){
        bool known=false;for(const auto&pile:r.worldView.self.piles)if(pile.name=="xinyang")for(const auto&c:pile.cards)if(c.effectiveId==value)known=true;
        if(!known||!p->getPile("xinyang").contains(value))return fail("Xinyang material is not the disclosed owner pile");
    }
    // No deferred effect: execute the genuine reached source setter now.
    // A Scenario override is a separate native callback graph; until that
    // graph is attested, deny BEFORE the write rather than drop onTagSet.
    if(room.getScenario()!=nullptr)return fail("Xinyang Scenario onTagSet continuation is not certified");
    const QString tag=QStringLiteral("xinyang_judge_card");const QVariant before=room.getTag(tag),after(value);
    const bool changed=!before.isValid()||before!=after;const quint64 revision=room.roomRuntime()->stateRevision();
    if(changed&&revision==std::numeric_limits<quint64>::max())return fail("Xinyang revision overflow");
    const auto generation=s->generation;s->writing=true;
    try {room.setTag(tag,after);}catch(...){if(s->generation==generation){s->writing=false;s->valid=false;}throw;}
    if(s->generation!=generation)return fail("Xinyang setter reentered another native session");
    s->writing=false;++s->writes;
    const auto next=revision+(changed?1:0);
    if(room.roomRuntime()->stateRevision()!=next||room.getTag(tag)!=after||flags(p)!=s->expectedFlags){s->valid=false;return fail("Xinyang setter caused an uncertified mutation");}
    s->expected=next;if(changed)++s->transitions;return true;
}

// The exact reached source Room:setTag operation shares the existing native
// request certificate. Its revision transition grants no payment authority.
bool writeAIRound53GoodatwepTag(Room&room,const AIRequest&r,ServerPlayer*p,const QString&value,
 const AIRound53GoodatwepPermit&permit,QString*error){
 auto fail=[&](const char*m){if(error)*error=QString::fromLatin1(m);return false;};auto*s=find(room);
 if(!p||!permit.matches(room,r,p)||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY
  ||!s||!s->active||!matches(room,r,s,p))return fail("goodatwep source certificate unavailable");
 if(room.getScenario())return fail("goodatwep Scenario onTagSet not certified");
 const QString key=QStringLiteral("goodatwep");const QVariant before=room.getTag(key),after(value);
 const bool changed=!before.isValid()||before!=after;const auto revision=room.roomRuntime()->stateRevision();
 if(changed&&revision==std::numeric_limits<quint64>::max())return fail("goodatwep revision exhausted");
 const auto generation=s->generation;s->writing=true;
 try{room.setTag(key,after);}catch(...){if(s->generation==generation){s->writing=false;s->valid=false;}throw;}
 if(s->generation!=generation)return fail("goodatwep setter reentered session");s->writing=false;++s->writes;
 const auto next=revision+(changed?1:0);
 if(room.roomRuntime()->stateRevision()!=next||room.getTag(key)!=after||flags(p)!=s->expectedFlags){s->valid=false;return fail("goodatwep prefix committed but current state changed");}
 s->expected=next;if(changed)++s->transitions;return true;
}

// Exact finite source setTag/removeTag. No external tag key or QVariant arrives
// from policy; the native helper constructs only own-hand Int / offered Player.
bool writeAIRound53YohaneTag(Room&room,const AIRequest&r,ServerPlayer*p,const QString&key,const QVariant&value,bool remove,
 const AIRound53YohanePermit&permit,QString*error){
 auto fail=[&](const char*m){if(error)*error=QString::fromLatin1(m);return false;};auto*s=find(room);
 if(!p||!permit.matches(room,r,p,key)||(key!="ai_yohane_card_id"&&key!="ai_yohane_player")
  ||!s||!s->active||!matches(room,r,s,p))return fail("Yohane source certificate unavailable");
 if(!remove&&room.getScenario())return fail("Yohane Scenario onTagSet not certified");
 const QVariant before=room.getTag(key);const bool changed=remove?before.isValid():(!before.isValid()||before!=value);
 const auto revision=room.roomRuntime()->stateRevision();if(changed&&revision==std::numeric_limits<quint64>::max())return fail("Yohane revision exhausted");
 const auto generation=s->generation;s->writing=true;
 try{if(remove)room.removeTag(key);else room.setTag(key,value);}catch(...){if(s->generation==generation){s->writing=false;s->valid=false;}throw;}
 if(s->generation!=generation)return fail("Yohane setter reentered session");s->writing=false;++s->writes;
 const auto next=revision+(changed?1:0);const QVariant after=room.getTag(key);
 if(room.roomRuntime()->stateRevision()!=next||(remove?after.isValid():after!=value)||flags(p)!=s->expectedFlags){s->valid=false;return fail("Yohane prefix committed but current state changed");}
 s->expected=next;if(changed)++s->transitions;return true;
}

// Finite Kaiqi reached Player:setFlags, including a selected public other player.
// No Room::setPlayerFlag broadcast and no synthetic strategy-only substitute.
bool writeAIRound46KaiqiFlag(Room &room,const AIRequest &r,ServerPlayer *owner,ServerPlayer *target,
 const QString &flag,const AIRound46KaiqiFlagPermit &permit,QString *error) {
 auto fail=[&](const char*m){if(error)*error=QString::fromLatin1(m);return false;};
 auto*s=find(room);
 const bool self=target==owner&&flag=="kaiqi_self_used";
 const bool other=target&&flag=="kaiqi_used"&&r.choiceOptions.playerNames.contains(target->objectName());
 if(r.kind!=AIRequest::PlayerChosen||r.choiceOptions.reason!="kaiqi"||(!self&&!other)
  ||!owner||!target||target->getRoom()!=&room||!permit.matches(room,r,owner,target)
  ||!s||!s->active||!matches(room,r,s,owner))return fail("Kaiqi source flag certificate unavailable");
 const auto beforeFlags=flags(target);auto expectedTarget=beforeFlags;expectedTarget.insert(flag);
 const bool changed=!beforeFlags.contains(flag);const auto revision=room.roomRuntime()->stateRevision();
 if(changed&&revision==std::numeric_limits<quint64>::max())return fail("Kaiqi revision overflow");
 const auto generation=s->generation;s->writing=true;
 try{target->setFlags(flag);}catch(...){if(s->generation==generation){s->writing=false;s->valid=false;}throw;}
 if(s->generation!=generation)return fail("Kaiqi source setter reentered another native session");
 s->writing=false;++s->writes;
 auto expectedOwner=s->expectedFlags;if(target==owner)expectedOwner.insert(flag);
 const auto next=revision+(changed?1:0);
 if(room.roomRuntime()->stateRevision()!=next||flags(target)!=expectedTarget||flags(owner)!=expectedOwner){
  s->valid=false;return fail("Kaiqi source setter caused an uncertified mutation; prefix retained");
 }
 s->expected=next;s->expectedFlags=expectedOwner;if(changed)++s->transitions;return true;
}

// R50 fixed source Player::addMark/Player::setFlags. This is not Room::setPlayerMark
// and must not manufacture broadcast or MarkChange/MarkChanged events.
bool writeAIRound50Effect(Room&room,const AIRequest&r,ServerPlayer*owner,ServerPlayer*target,
 const AIRound50EffectPermit&permit,QString*error){
 auto fail=[&](const char*m){if(error)*error=QString::fromLatin1(m);return false;};
 auto*s=find(room);const auto&key=permit.key;
 const bool markKey=key=="AI_has_clubs"||key=="AI_has_spades"||key=="AI_has_diamonds"||key=="AI_has_hearts"||key=="ai_anzhi-PlayClear";
 const bool flagKey=key=="needCrossbow"||(key=="turned_target"&&permit.value)||(key=="majiBad"&&!permit.value)||(key=="majiGood"&&!permit.value)||(key=="majiHeal"&&!permit.value);
 if(permit.room!=&room||permit.request!=&r||permit.owner!=owner||permit.target!=target||!owner||!target
  ||target->getRoom()!=&room||!s||!s->active||!matches(room,r,s,owner)
  ||(permit.mark?(!markKey||target!=owner):!flagKey))return fail("source effect permit/current certificate invalid");
 const int beforeMark=permit.mark?target->getMark(key):0;
 if(permit.mark&&beforeMark==std::numeric_limits<int>::max())return fail("source signed mark addition overflow unsupported");
 const auto beforeFlags=flags(target);auto expectedTarget=beforeFlags;
 if(!permit.mark){if(permit.value)expectedTarget.insert(key);else expectedTarget.remove(key);}
 const bool changed=permit.mark||beforeFlags.contains(key)!=permit.value;
 const auto revision=room.roomRuntime()->stateRevision();
 if(changed&&revision==std::numeric_limits<quint64>::max())return fail("source effect revision overflow unsupported");
 const auto generation=s->generation;s->writing=true;
 try{if(permit.mark)target->addMark(key);else target->setFlags(permit.value?key:QStringLiteral("-")+key);}
 catch(...){if(s->generation==generation){s->writing=false;s->valid=false;}throw;}
 if(s->generation!=generation)return fail("source effect reentered replacement session; prefix retained");
 s->writing=false;++s->writes;
 auto expectedOwner=s->expectedFlags;if(target==owner&&!permit.mark)expectedOwner=expectedTarget;
 const auto next=revision+(changed?1:0);
 if(room.roomRuntime()->stateRevision()!=next||flags(target)!=expectedTarget||flags(owner)!=expectedOwner
  ||(permit.mark&&target->getMark(key)!=beforeMark+1)){
  s->valid=false;return fail("source effect caused uncertified synchronous mutation; prefix retained");
 }
 s->expected=next;s->expectedFlags=expectedOwner;if(changed)++s->transitions;return true;
}

// Finite source targetRevises Player property, not a broadcast or shadow flag.
bool writeAIRound53AiNoToProperty(Room&room,const AIRequest&r,ServerPlayer*owner,ServerPlayer*target,
 bool value,const AIRound53AiNoToPermit&permit,QString*error){
 auto fail=[&](const char*m){if(error)*error=QString::fromLatin1(m);return false;};auto*s=find(room);
 if(!owner||!target||target->getRoom()!=&room||!permit.matches(room,r,owner,target,value)
  ||!s||!s->active||!matches(room,r,s,owner))return fail("aiNoTo source certificate invalid");
 const QVariant before=target->property("aiNoTo"),after(value);const bool changed=before!=after;
 const auto revision=room.roomRuntime()->stateRevision();if(changed&&revision==std::numeric_limits<quint64>::max())return fail("aiNoTo revision overflow");
 const auto generation=s->generation;s->writing=true;
 try{target->setProperty("aiNoTo",after);}catch(...){if(s->generation==generation){s->writing=false;s->valid=false;}throw;}
 if(s->generation!=generation)return fail("aiNoTo setter reentered a replacement decision");s->writing=false;++s->writes;
 const auto next=revision+(changed?1:0);
 if(room.roomRuntime()->stateRevision()!=next||target->property("aiNoTo")!=after||flags(owner)!=s->expectedFlags){s->valid=false;return fail("aiNoTo source prefix caused uncertified synchronous mutation");}
 s->expected=next;if(changed)++s->transitions;return true;
}
