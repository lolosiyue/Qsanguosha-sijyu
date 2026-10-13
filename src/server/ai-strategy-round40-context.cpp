#include "ai-strategy-round40-context.h"
#include "ai-strategy-room-ledger.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QPointer>

namespace {
class History final : public AIRoomLedger {
public:
    explicit History(Room &r) : AIRoomLedger(r) {}
    bool initialized=false, present=false;
    quint64 serial=0;
    QPointer<ServerPlayer> actor;
};
History *history(Room &room, bool create=true)
{
    for (QObject *child:room.children())
        if (auto *row=dynamic_cast<History *>(child)) return row;
    return create ? new History(room) : nullptr;
}
bool role(const QString &s)
{ return s=="lord" || s=="loyalist" || s=="rebel" || s=="renegade"; }
}
void clearAIRound40TargetHistory(Room &room)
{
    auto *h=history(room);
    h->initialized=true; h->present=false; h->serial=0; h->actor.clear();
}
void observeAIRound40LegacyFilterEvent(Room &room, int event,
    ServerPlayer *player, const QVariant &data)
{
    if (event!=TargetSpecified || !player || player->getRoom()!=&room) return;
    auto *h=history(room);
    // Every source write replaces the old record, including malformed or foreign
    // newest values. Never resurrect a previous own record after nested use.
    h->present=true; ++h->serial; h->actor.clear();
    if (!data.canConvert<CardUseStruct>()) return;
    const auto use=data.value<CardUseStruct>();
    if (!use.from || use.from->getRoom()!=&room) return;
    h->actor=use.from;
    // The source stores a full QVariant, but no nullification list is copied to
    // this observer ledger. Its writers are not all public (e.g. goddess1311),
    // and actor ownership alone does not authorize the mutable native member.

}
void projectAIRound40TargetHistory(AIRequest &r, Room &room, ServerPlayer *viewer)
{
    if (r.kind!=AIRequest::SkillInvoke || r.choiceOptions.reason!="zd_zhungangshuo") return;
    QJsonObject row{{"known",false}};
    auto publish=[&](){auto y=r.choiceOptions.context.value("yongjian").toObject();
        y.insert("target_specified_use",row);r.choiceOptions.context.insert("yongjian",y);};
    publish();
    if (!viewer || viewer->getRoom()!=&room || room.roomRuntime()->isClosing()
        || r.viewerObjectName!=viewer->objectName()
        || r.worldView.self.objectName!=viewer->objectName()
        || r.stateRevision!=room.roomRuntime()->stateRevision()) return;
    const auto *h=history(room,false);
    if (!h || !h->initialized) return;
    row=QJsonObject{{"known",true},{"present",h->present},{"viewer",viewer->objectName()},
        {"decision_id",r.getDecisionId()},{"state_revision",r.getStateRevision()},
        {"serial",QString::number(h->serial)}};
    if (!h->present) {publish();return;}
    // Full source history exists. Card::onUse public logs carry card/from/to,
    // not this list. No source-safe complete membership witness exists yet.
    row=QJsonObject{{"known",false}}; publish();
}
void projectAIRound40AzirEvent(AIWorldView &world, Room &room, ServerPlayer *viewer,
    ServerPlayer *eventPlayer, const AIEventView &event, const QVariant &data)
{
    world.strategyFacts.remove("azirshenji_role_witness");
    if (!viewer || viewer->getRoom()!=&room || !eventPlayer || eventPlayer->getRoom()!=&room
        || world.self.objectName!=viewer->objectName() || event.triggerEvent!=CardFinished
        || !data.canConvert<CardUseStruct>()) return;
    const auto use=data.value<CardUseStruct>();
    if (!use.card || use.card->objectName()!="azirshenjiCard") return;
    QJsonArray rows;
    for (ServerPlayer *p:room.getOtherPlayers(eventPlayer)) {
        const AIPlayerView *view=nullptr;
        if (p==viewer) view=&world.self;
        else for (const auto &v:world.players) if (v.objectName==p->objectName()) {view=&v;break;}
        QJsonObject row{{"player",p->objectName()},{"head_known",false}};
        // Raw head name, not actualGeneral replacement. Foreign raw face is
        // lawful only when the ordinary/public snapshot disclosed that head.
        if (view && (p==viewer || (!view->generalName.isEmpty()
            && view->generalName!="anjiang" && view->generalName==p->getGeneralName()))) {
            row.insert("head_known",true);
            const bool solar=p->getGeneralName()=="kesolardisk";
            row.insert("solar",solar);
            if (solar) {
                const bool known=view->roleRevealed && role(view->role);
                row.insert("role_known",known);
                if (known) row.insert("role",view->role);
            }
        }
        rows.append(row);
    }
    world.strategyFacts.insert("azirshenji_role_witness",
        QJsonObject{{"actor",eventPlayer->objectName()},{"rows",rows}});
}
