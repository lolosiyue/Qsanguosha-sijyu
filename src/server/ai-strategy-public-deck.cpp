#include "ai-strategy-public-deck.h"
#include "ai-strategy-room-ledger.h"
#include "ai.h"
#include "engine.h"
#include "game-rng.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QPointer>
#include <QThread>
#include <QScopeGuard>
#include "room-definition-registry.h"
#include "lua-runtime.h"
namespace {
QJsonObject denied(const QString &reason) { return {{"error",reason},{"unsupported",true},{"key","rng.public_deck_order"}}; }
// This is a reached source-effect ledger, not an action receipt. It retains
// only weak pointers to issued catalogue objects; Room owns its destruction.
struct PrintedTraversal : AIRoomLedger {
    explicit PrintedTraversal(Room &room):AIRoomLedger(room){}
    quint64 serial=0,decision=0,revision=0;
    QString viewer,sequence;
    QWeakPointer<AIConversionLedger> requestLedger;
    QList<QPointer<Card>> cards;
    QJsonArray faces;
    int next=0;
    bool awaitingAvailability=false,availabilityInFlight=false;
};
PrintedTraversal *traversal(Room &room,bool create)
{
    const char *key="_ai_round40_printed_traversal";
    auto *value=static_cast<PrintedTraversal *>(room.property(key).value<QObject *>());
    if(!value&&create){value=new PrintedTraversal(room);room.setProperty(key,QVariant::fromValue(static_cast<QObject *>(value)));}
    return value;
}
QJsonObject advancePrinted(Room &room,ServerPlayer *viewer,const AIRequest &request,const QJsonObject &query)
{
    const QString key=query.value("key").toString();
    const bool setting=key=="sfofl_zhengan_basic_set";
    if(query.size()!=4||query.value("kind")!="response_catalogue"||!query.value("sequence").isString()
       ||!query.value("index").isDouble())return denied("invalid printed source-effect shape");
    auto *state=traversal(room,false);
    const double index=query.value("index").toDouble();
    if(!state||state->decision!=request.decisionId||state->revision!=request.stateRevision
       ||state->viewer!=viewer->objectName()||state->requestLedger.toStrongRef()!=request.conversionLedger
       ||state->sequence!=query.value("sequence").toString()||index!=state->next+1
       ||state->next>=state->cards.size()||state->availabilityInFlight||setting==state->awaitingAvailability)
        return denied("printed source-effect sequence is stale or out of order");
    Card *card=state->cards.at(state->next);
    const auto face=state->faces.at(state->next).toObject();
    if(!card||card!=Sanguosha->getEngineCard(face.value("printed_id").toInt())
       ||card->objectName()!=face.value("name").toString()||int(card->getSuit())!=face.value("suit").toInt()
       ||card->getNumber()!=face.value("number").toInt())return denied("issued printed source identity changed");
    // Compiled BasicCards keep their actual Engine pointer. The common native
    // field accessor stores attribution in the current Room, so old aliases and
    // all same-Room native readers see it without writing shared QObject state.
    const bool shared = room.roomRuntime()->definitions().isEngineCard(card);
    if ((shared && card->getTypeId() != Card::TypeBasic)
        || (!shared && (room.roomRuntime()->engineCard(card->getId()) != card
                       || card->thread() != QThread::currentThread())))
        return denied("printed source object has no canonical Room attribution storage");
    // The catalogue query already charged one traversal. Each issued ordinal
    // permits exactly set then availability once; no unbounded query loop or
    // budget increase is introduced for a source list longer than 128 cards.
    EngineRuntimeContextScope engine(*Sanguosha,&room);
    LuaRuntime::Binding binding(room.roomRuntime()->lua(),false);
    if(setting){
        card->setSkillName(QStringLiteral("sfofl_zhengan_basic"));
        state->awaitingAvailability=true;
        return {{"known",true},{"key",key},{"sequence",state->sequence},{"index",state->next+1}};
    }
    // Exact actual printed object, after the preceding persistent setter. No
    // hypothetical clone, payment candidate, or generic arbitrary-ID query.
    if(card->getSkillName(false)!="sfofl_zhengan_basic")return denied("printed source setter prefix changed");
    const QString capturedSequence=state->sequence;
    const int capturedNext=state->next;
    const auto capturedLedger=state->requestLedger;
    QPointer<PrintedTraversal> liveState(state);
    state->availabilityInFlight=true;
    const auto unchanged=[&]{return liveState&&liveState->sequence==capturedSequence
        &&liveState->next==capturedNext&&liveState->requestLedger==capturedLedger;};
    const auto restore=qScopeGuard([&]{if(unchanged())liveState->availabilityInFlight=false;});
    const bool available=card->isAvailable(viewer);
    if(!unchanged()||state->decision!=request.decisionId||state->revision!=request.stateRevision
       ||room.roomRuntime()->stateRevision()!=request.stateRevision||state->cards.at(capturedNext)!=card
       ||card->getSkillName(false)!="sfofl_zhengan_basic")
        return denied("printed availability changed its issued traversal");
    state->awaitingAvailability=false;state->availabilityInFlight=false;++state->next;
    return {{"known",true},{"key",key},{"sequence",state->sequence},{"index",state->next},{"available",available}};
}
QJsonObject printed(const Card *card) {
    // Source Engine::getEngineCard: no Room::getCard, owner, place, live flags,
    // tags, skill chains or payment identities. These values are catalogue data.
    QJsonArray kinds;
    for (const char *kind : {"BasicCard","TrickCard","DelayedTrick","EquipCard","Slash"})
        if(card->isKindOf(kind)) kinds.append(kind);
    if(!kinds.contains(card->getClassName())) kinds.append(card->getClassName());
    return {{"printed_id",card->getId()}, {"id",-1}, {"effective_id",-1},
        {"name",card->objectName()}, {"class_name",card->getClassName()},
        {"suit",int(card->getSuit())}, {"number",card->getNumber()},
        {"type_id",int(card->getTypeId())}, {"kind_of",kinds},
        {"basic",card->isKindOf("BasicCard")}, {"nd_trick",card->isNDTrick()},
        {"damage_card",card->isDamageCard()}, {"red",card->isRed()}, {"black",card->isBlack()},
        {"evaluation_only",true}, {"subcards",QJsonArray()}};
}
}
QJsonObject resolveAIRound39PublicDeck(Room &room,ServerPlayer *viewer,
                                      const AIRequest &request,const QJsonObject &query) {
    if(!viewer || viewer->getRoom()!=&room || request.viewerObjectName!=viewer->objectName()
       || request.worldView.self.objectName!=viewer->objectName()
       || request.stateRevision!=room.roomRuntime()->stateRevision())
        return denied("public printed deck request is stale or belongs to another viewer");
    const QString operation=query.value("key").toString();
    if(operation=="sfofl_zhengan_basic_set"||operation=="sfofl_zhengan_basic_available") {
        if(request.kind!=AIRequest::UseCard||request.pattern!="@@sfofl_zhengan"
           ||!viewer->hasSkill("sfofl_zhengan_basic",true))return denied("printed source effect outside actual question");
        return advancePrinted(room,viewer,request,query);
    }
    if(query.size()!=2 || query.value("kind")!="response_catalogue" || !query.value("key").isString())
        return denied("public printed deck query accepts only its finite source key");
    const QString key=query.value("key").toString();
    const bool basic=key==QStringLiteral("sfofl_zhengan_basic_printed");
    const bool jvewu=key==QStringLiteral("ny_tenth_jvewu_printed");
    const bool basicScope=basic && request.kind==AIRequest::UseCard
        && request.pattern==QStringLiteral("@@sfofl_zhengan")
        && viewer->hasSkill(QStringLiteral("sfofl_zhengan_basic"),true);
    const bool jvewuScope=jvewu && request.kind==AIRequest::Activate
        && request.reason==CardUseStruct::CARD_USE_REASON_PLAY
        && viewer->hasSkill(QStringLiteral("ny_tenth_jvewu"));
    if(!basicScope && !jvewuScope)return denied("public printed deck query is outside its fixed source question");
    if(jvewuScope) {
        bool rankTwo=false;
        for(const Card *card:viewer->getCards("he"))if(card->getNumber()==2){rankTwo=true;break;}
        if(!rankTwo)return denied("Jvewu source shuffle has not reached its owner material prefix");
    }
    if(!request.conversionLedger || request.conversionLedger->queryCount>=128)
        return denied("public printed deck query budget exhausted");
    ++request.conversionLedger->queryCount;
    // Both fixed callers use derivative=false. qsanShuffle consumes bounds
    // N,N-1,...,1 including bounded(1), with native rejection sampling. Calling
    // the real helper preserves filters and RNG poststate, not merely a count.
    EngineRuntimeContextScope engine(*Sanguosha,&room);
    GameRng::Binding gameplay(room.roomRuntime()->rng());
    const QList<int> ids=Sanguosha->getRandomCards(false);
    QJsonArray cards;QList<QPointer<Card>> issued;
    for(int id:ids) {
        const Card *card=Sanguosha->getEngineCard(id);
        if(!card)return denied("configured printed deck contains an absent definition after shuffle");
        // addFunction755 PatternsCard("BasicCard",true): retain every match,
        // duplicate object name and original physical face in shuffled order.
        if(!basic || card->objectName()==QStringLiteral("BasicCard") || card->isKindOf("BasicCard"))
        {
            cards.append(printed(card));
            if(basic)issued.append(const_cast<Card *>(card));
        }
    }
    QJsonObject result{{"known",true},{"complete",true},{"key",key},{"cards",cards}};
    if(basic){
        auto *state=traversal(room,true);++state->serial;
        state->decision=request.decisionId;state->revision=request.stateRevision;
        state->viewer=viewer->objectName();state->requestLedger=request.conversionLedger;
        state->sequence=QString::number(state->serial);state->cards=issued;state->faces=cards;
        state->next=0;state->awaitingAvailability=false;state->availabilityInFlight=false;result.insert("sequence",state->sequence);
    }
    return result;
}
