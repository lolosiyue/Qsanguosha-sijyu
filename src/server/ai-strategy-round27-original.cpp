#include "ai-strategy-round27-original.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "general.h"
#include "engine.h"
#include <memory>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QSet>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>

namespace {
class OriginalViews final : public QObject {
public:
    explicit OriginalViews(Room &room) : QObject(&room) { setObjectName("isolated_original27_views"); }
    QMutex mutex;
    QHash<QString,QHash<QString,QSet<int>>> observed;
};
OriginalViews &views(Room &room)
{
    static QMutex creation;
    QMutexLocker lock(&creation);
    auto *child=room.findChild<QObject *>("isolated_original27_views",Qt::FindDirectChildrenOnly);
    if(auto *state=dynamic_cast<OriginalViews *>(child))return *state;
    return *new OriginalViews(room);
}
void projectObserved(AIWorldView &world,Room &room,ServerPlayer *viewer)
{
    auto &state=views(room);QMutexLocker lock(&state.mutex);
    auto visible=world.strategyFacts.value("source_visible_hand_ids").toObject();
    auto counts=world.strategyFacts.value("original_known_both_visible_counts").toObject();
    const auto rows=state.observed.value(viewer->objectName());
    for(const auto &p:world.players){
        const auto *live=room.findPlayerByObjectName(p.objectName,true);if(!live)continue;
        const auto ids=rows.value(p.objectName);if(ids.isEmpty())continue;
        QSet<int> lawful;for(const auto &c:p.knownCards)lawful.insert(c.effectiveId);
        if(p.objectName==viewer->objectName())for(const auto &c:world.handCards)lawful.insert(c.effectiveId);
        QSet<int> prior;for(const auto &v:visible.value(p.objectName).toArray())prior.insert(v.toInt(-1));
        QJsonArray ordered;int added=0;
        for(int id:live->handCards()){
            const bool observed=ids.contains(id)&&lawful.contains(id);
            if(prior.contains(id)||observed)ordered.append(id);
            if(observed&&!prior.contains(id))++added;
        }
        visible.insert(p.objectName,ordered);
        if(counts.contains(p.objectName))counts.insert(p.objectName,counts.value(p.objectName).toInt()+added);
    }
    world.strategyFacts.insert("source_visible_hand_ids",visible);
    world.strategyFacts.insert("original_known_both_visible_counts",counts);
}
}


int aiRound27OriginalPrototype(const AIRequest &r, const ServerPlayer *p,
                              const QString &skill, const QString &prototype)
{
    if (!p || !p->getRoom() || !r.worldView.enableHegemony
        || r.viewerObjectName != p->objectName()
        || r.stateRevision != p->getRoom()->roomRuntime()->stateRevision()
        || r.kind != AIRequest::Activate || r.reason != CardUseStruct::CARD_USE_REASON_PLAY
        || prototype != QStringLiteral("ActiveSkillCard") || !p->hasSkill(skill)) return -1;
    // heg-basara-ai.lua:24 creates the actual generic V2 card. HFirstShowCard
    // and HCareermanCard are policy/history keys, never invented prototypes.
    return skill == QStringLiteral("heg_firstshow") || skill == QStringLiteral("heg_careerman") ? 0 : -1;
}

void projectAIRound27OriginalWorld(AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom()!=&room || room.roomRuntime()->isClosing() || world.self.objectName != viewer->objectName() || !world.enableHegemony) return;
    projectObserved(world,room,viewer);
    // TurnStart reads the owner's current raw general objects (not another
    // observer's actual hidden general). No foreign-general definitions leak.
    const auto gender = [](const General *g) -> QJsonValue {
        if (!g) return false;
        return QJsonObject{{"name", g->objectName()}, {"male", g->isMale()}, {"female", g->isFemale()}};
    };
    QJsonObject prototypes;
    for (const QString &name : {QStringLiteral("HFanjianCard"), QStringLiteral("HFangquanCard"),
                               QStringLiteral("HTianxiangCard"), QStringLiteral("HWoodenOxCard")}) {
        std::unique_ptr<SkillCard> prototype(Sanguosha->cloneSkillCard(name));
        prototypes.insert(name, bool(prototype));
    }
    world.strategyFacts.insert(QStringLiteral("original27"), QJsonObject{
        {"legacy_prototypes", prototypes},
        {"owner_raw_head", gender(viewer->getGeneral())},
        {"owner_raw_deputy", gender(viewer->getGeneral2())},
        {"owner_actual_head", gender(viewer->getActualGeneral1()?viewer->getActualGeneral1():Sanguosha->getGeneral("anjiang"))},
        {"owner_actual_deputy", gender(viewer->getActualGeneral2()?viewer->getActualGeneral2():Sanguosha->getGeneral("anjiang"))}
    });
}

void recordAIRound27OriginalEvent(const AIWorldView &world, Room &room, ServerPlayer *viewer,
    ServerPlayer *eventPlayer, const AIEventView &event, const QVariant &data)
{
    if(!viewer||viewer->getRoom()!=&room||room.roomRuntime()->isClosing()
        ||!world.enableHegemony||world.self.objectName!=viewer->objectName())return;
    auto &state=views(room);QMutexLocker lock(&state.mutex);
    auto &owned=state.observed[viewer->objectName()];
    if(event.triggerEvent==GameStart){owned.clear();return;}
    if(event.triggerEvent==CardsMoveOneTime&&data.canConvert<CardsMoveOneTimeStruct>()){
        const auto move=data.value<CardsMoveOneTimeStruct>();
        // Same source clear point as filterEvent: hand cards leaving while
        // the event player is current clear every viewer/target visibility key.
        if(eventPlayer==room.getCurrent())for(int i=0;i<move.card_ids.size();++i)
            if(move.from_places.value(i,Player::PlaceUnknown)==Player::PlaceHand)
                for(auto it=owned.begin();it!=owned.end();++it)it->remove(move.card_ids.at(i));
        return;
    }
    if(event.triggerEvent!=ChoiceMade||eventPlayer!=viewer||event.from!=viewer->objectName()
        ||!event.privateEvent||event.privateViewer!=viewer->objectName()
        ||event.details.value("choice_kind")!="viewCards"||data.userType()!=QMetaType::QString)return;
    const auto prompt=data.toString().split(':');
    if(prompt.size()!=3||prompt.first()!="viewCards")return;
    const auto *target=room.findPlayerByObjectName(prompt.at(1),true);if(!target)return;
    // showCard also emits viewCards for a subset! Only the actual recipient
    // payload is evidence; donor's whole-hand scan is not a disclosure grant.
    for(const auto &text:prompt.at(2).split('+',Qt::SkipEmptyParts)){
        bool ok=false;const int id=text.toInt(&ok);
        if(ok&&id>=0&&target->handCards().contains(id))owned[target->objectName()].insert(id);
    }
}

void clearAIRound27OriginalViews(Room &room)
{
    auto *child=room.findChild<QObject *>("isolated_original27_views",Qt::FindDirectChildrenOnly);
    if(auto *state=dynamic_cast<OriginalViews *>(child)){QMutexLocker lock(&state->mutex);state->observed.clear();}
}
