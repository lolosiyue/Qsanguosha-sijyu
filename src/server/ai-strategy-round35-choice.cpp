#include "ai-strategy-round35-choice.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QDebug>
#include <QJsonArray>
#include <QSet>

bool aiRound35OriginalQuestion(const AIRequest &r)
{
    const auto h=r.worldView.strategyFacts.value("hegemony").toObject();
    return r.decisionId!=0 && !r.viewerObjectName.isEmpty()
        && r.worldView.self.objectName==r.viewerObjectName
        && h.value("enabled")==QJsonValue(true)
        && h.value("original_overrides_loaded")==QJsonValue(true);
}

bool aiRound35GuanxingConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &s)
{
    if(!aiRound35OriginalQuestion(r) || !p || !p->getRoom()
        || p->objectName()!=r.viewerObjectName
        || r.stateRevision!=p->getRoom()->roomRuntime()->stateRevision()
        || r.kind!=AIRequest::Guanxing || s.size()!=6
        || s.value("source_key")!=QJsonValue(QStringLiteral("original_guanxing"))
        || s.value("skill")!=QJsonValue(QString()) || !s.value("subcards").isArray()
        || !s.value("subcards").toArray().isEmpty())return false;
    const QString name=s.value("name").toString();
    // Source715 makes a fresh default clone even though its local is unused.
    if(name=="slash" && s.value("suit")==QJsonValue(int(Card::SuitToBeDecided))
        && s.value("number")==QJsonValue(-1))return true;
    static const QSet<QString> names{"snatch","dismantlement","indulgence","supply_shortage",
        "collateral","duel","drowning","archery_attack","savage_assault","fire_attack",
        "god_salvation","lightning"};
    if(!names.contains(name))return false;
    // A zero-material class dummy takes its printed suit/number from one
    // already-disclosed offered face. This is not ownership or payment.
    for(const auto &c:r.choiceOptions.cards)
        if(r.choiceOptions.cardIds.contains(c.effectiveId)
            && s.value("suit")==QJsonValue(c.suit)
            && s.value("number")==QJsonValue(c.number))return true;
    return false;
}

bool emitAIRound35SourceWarning(Room &room,const AIRequest &r,const ServerPlayer *p,
    const QString &source,const QString &message)
{
    if(!aiRound35OriginalQuestion(r)||!p||p->getRoom()!=&room
        ||p->objectName()!=r.viewerObjectName||r.stateRevision!=room.roomRuntime()->stateRevision()
        ||message.size()>16384)return false;
    const bool pindian=source=="Original askForPindian"&&r.kind==AIRequest::RespondCard
        &&r.choiceOptions.question=="askForPindian"
        &&message==QStringLiteral("Pindian card for %1 not found!!").arg(r.choiceOptions.reason);
    const bool guanxing=source=="Original Guanxing"&&r.kind==AIRequest::Guanxing
        &&message.contains(QStringLiteral("stack traceback:"));
    const bool round=source=="Original playerGetRound"
        &&message.contains(QStringLiteral("stack traceback:"));
    const bool dispatch=source=="Original useCardByClassName"
        &&message==QStringLiteral("stack traceback:\n\tOriginal useCardByClassName: nil card");
    if(!pindian&&!guanxing&&!round&&!dispatch)return false;
    // Exact SWIG Room.writeToConsole sink; prefix remains even if policy errors.
    room.output(message);
    qWarning("%s",qPrintable(message));
    return true;
}
