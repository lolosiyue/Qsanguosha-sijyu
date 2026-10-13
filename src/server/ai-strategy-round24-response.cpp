#include "ai-strategy-round24-response.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-projection.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include <QHash>
#include <QObject>
#include <QUuid>
#include <memory>
namespace {
struct SavedJudge { QString token, who, pattern; bool good=false; };
class SavedJudges final : public AIRoomLedger {
public:
    explicit SavedJudges(Room &room) : AIRoomLedger(room) { setObjectName("isolated-round24-saved-judges"); }
    QHash<QString,SavedJudge> rows;
};
SavedJudges *store(Room &room) {
    for (QObject *child:room.children())
        if (child->objectName()=="isolated-round24-saved-judges")
            if (auto *ledger=dynamic_cast<SavedJudges *>(child)) return ledger;
    return new SavedJudges(room);
}
std::unique_ptr<Card> disclosedClone(const AICardView &face) {
    if (face.effectiveId<0) return {};
    std::unique_ptr<Card> card(Sanguosha->cloneCard(face.objectName,Card::Suit(face.suit),face.number));
    if (!card || card->getClassName()!=face.className) return {};
    card->setId(face.effectiveId); card->setSkillName(face.skillName);
    return card;
}
bool publicPredicate(const QString &pattern) {
    if (pattern.contains('$')) return false;
    for (const QString &part:pattern.split('#')) {
        const QStringList factors=part.split('|');
        if (factors.size()>3 && factors[3]!=".") return false;
    }
    return true;
}
bool knownNoFilters(const AIRequest &request,const ServerPlayer *who) {
    const AIPlayerView *view=nullptr;
    if (who->objectName()==request.worldView.self.objectName) view=&request.worldView.self;
    else for (const auto &p:request.worldView.players) if (p.objectName==who->objectName()) {view=&p;break;}
    if (!view || !view->skillsComplete) return false;
    for (const auto &s:view->skills) if (!s.invalid && dynamic_cast<const FilterSkill *>(Sanguosha->getViewAsSkill(s.skillName))) return false;
    // Weidi's appended lord filters must not be inferred absent from another roster.
    for (const auto &s:view->skills) if (!s.invalid && s.skillName=="weidi") return false;
    return true;
}
}
void projectAIStrategyRound24Response(AIRequest &request,Room &room,ServerPlayer *viewer,
                                     const QVariant &data,QJsonObject &context) {
    if (!viewer || viewer->getRoom()!=&room || request.viewerObjectName!=viewer->objectName()
        || request.worldView.self.objectName!=viewer->objectName()
        || request.stateRevision!=room.roomRuntime()->stateRevision()) return;
    const QString reason=request.choiceOptions.reason;
    if (reason=="nyarz_huanshi" && (request.kind==AIRequest::Discard || request.kind==AIRequest::AmazingGrace)) {
        const QVariant raw=room.getTag("nyarz_huanshi");
        const JudgeStruct *pointer=raw.value<JudgeStruct *>();
        const JudgeStruct judge=pointer ? *pointer : raw.value<JudgeStruct>();
        if (!raw.isValid() || !judge.who) return;
        // The fixed writer asks the skill owner to exchange HE, then the judge
        // recipient to select from the faces it just publicly showed.
        if (request.kind==AIRequest::Discard && !viewer->hasSkill("nyarz_huanshi")) return;
        if (request.kind==AIRequest::AmazingGrace && judge.who!=viewer) return;
        QList<AICardView> faces=request.kind==AIRequest::AmazingGrace ? request.choiceOptions.cards : request.worldView.handCards;
        if (request.kind==AIRequest::Discard) faces.append(request.worldView.self.equips);
        QJsonObject matches;
        for (const auto &face:faces) {
            if (request.kind==AIRequest::AmazingGrace && !request.choiceOptions.cardIds.contains(face.effectiveId)) continue;
            auto card=disclosedClone(face);if (!card) continue;
            // Exactly the source's nil receiver. In particular place and '$'
            // availability clauses are skipped by ExpPattern with nullptr.
            matches.insert(QString::number(face.effectiveId),Sanguosha->matchExpPattern(judge.pattern,nullptr,card.get()));
        }
        auto second=context.value("NyarzSecond").toObject();
        second.insert("judge_nil_matches",QJsonObject{{judge.pattern,matches}});
        context.insert("NyarzSecond",second);
    }
    if (reason!="kanyu") return;
    if (request.kind==AIRequest::SkillInvoke) {
        auto *ledger=store(room);
        const JudgeStruct *judge=data.value<JudgeStruct *>();
        if (!judge || !judge->who) {ledger->rows.remove(viewer->objectName());context.insert("round24_kanyu",QJsonObject{{"present",false}});return;}
        SavedJudge row{QUuid::createUuid().toString(QUuid::WithoutBraces),judge->who->objectName(),judge->pattern,judge->good};
        ledger->rows.insert(viewer->objectName(),row);
        context.insert("round24_kanyu",QJsonObject{{"present",true},{"token",row.token},{"who",row.who},{"pattern",row.pattern},{"predicate_good",row.good}});
    } else if (request.kind==AIRequest::AmazingGrace) {
        auto *ledger=store(room);
        if (!ledger->rows.contains(viewer->objectName())) return;
        const SavedJudge row=ledger->rows.value(viewer->objectName());
        ServerPlayer *who=nullptr;for (auto *p:room.getAllPlayers(true)) if (p->objectName()==row.who) {who=p;break;}
        if (!who) return;
        JudgeStruct judge;judge.who=who;judge.pattern=row.pattern;judge.good=row.good;
        QJsonObject outcomes;
        for (const auto &face:request.choiceOptions.cards) {
            if (!request.choiceOptions.cardIds.contains(face.effectiveId)) continue;
            if (who==viewer) {
                const auto filtered=projectAIOwnerFilteredCard(room,viewer,face.effectiveId,&judge,int(Player::PlaceJudge),&face);
                if (filtered.value("complete").toBool() && filtered.value("judge_outcome").isBool()) outcomes.insert(QString::number(face.effectiveId),filtered.value("judge_outcome"));
            } else if (knownNoFilters(request,who) && publicPredicate(row.pattern)) {
                auto card=disclosedClone(face);if (card) outcomes.insert(QString::number(face.effectiveId),judge.isGood(card.get()));
            }
        }
        context.insert("round24_kanyu",QJsonObject{{"present",true},{"token",row.token},{"who",row.who},{"pattern",row.pattern},{"predicate_good",row.good},{"outcomes",outcomes}});
    }
}

void clearAIStrategyRound24Response(Room &room) {
    for (QObject *child:room.children())
        if (child->objectName()=="isolated-round24-saved-judges")
            if (auto *ledger=dynamic_cast<SavedJudges *>(child)) ledger->rows.clear();
}
