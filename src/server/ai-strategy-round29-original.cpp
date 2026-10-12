#include "ai-strategy-round56-maxcards.h"
#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round29-original.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "wrapped-card.h"
#include "structs.h"
#include <QJsonArray>
#include <QHash>
#include <QPointer>

namespace {
class ShicaiLedger final : public QObject {
public:
    struct Row {
        int id = -1;
        bool pending = true;
        quint64 serial = 0;
        QPointer<const Card> card;
        QJsonObject face;
        QMetaObject::Connection changed;
    };
    explicit ShicaiLedger(Room &room) : QObject(&room) {}
    QHash<QString, Row> rows;
    quint64 serial = 0;
    void erase(const QString &name) {
        auto it=rows.find(name);if(it==rows.end())return;
        QObject::disconnect(it->changed);rows.erase(it);
    }
};
ShicaiLedger *ledger(Room &room, bool create) {
    const QString name=QStringLiteral("_isolated_round29_shicai_preview");
    auto *object=room.findChild<QObject *>(name,Qt::FindDirectChildrenOnly);
    if(object)return dynamic_cast<ShicaiLedger *>(object);
    if(!create)return nullptr;
    auto *result=new ShicaiLedger(room);result->setObjectName(name);return result;
}
QJsonObject face(const Card *c) {
    return {{"id",c->getEffectiveId()},{"effective_id",c->getEffectiveId()},
        {"name",c->objectName()},{"class_name",c->getClassName()},
        {"type_id",int(c->getTypeId())},{"suit",int(c->getSuit())},
        {"suit_string",c->getSuitString()},{"number",c->getNumber()},
        {"kind_of",QJsonArray::fromStringList(c->getKindOfNames())},
        {"red",c->isRed()},{"black",c->isBlack()},{"name_length",c->nameLength()},
        {"flags_complete",false},{"skill_names_complete",false}};
}
QJsonObject read(Room &room, ServerPlayer *viewer, const std::function<bool()> &skillQuery = {}) {
    const QJsonObject unknown{{"known",false}};
    if(!viewer||viewer->getRoom()!=&room||room.roomRuntime()->isClosing()
        ||viewer->getPhase()!=Player::Play||!(skillQuery ? skillQuery() : viewer->hasSkill("guandu_shicai")))return unknown;
    auto *l=ledger(room,false);if(!l)return unknown;
    const auto it=l->rows.constFind(viewer->objectName());
    if(it==l->rows.cend()||it->pending||!it->card)return unknown;
    const auto row=*it;
    // HeavenMove sets the owner tag after sending both notification halves.
    // An arbitrary tag cannot mint knowledge: a matching live witness is required.
    bool ok=false;const int tagged=viewer->getTag("HeavenMove").toInt(&ok);
    if(!ok||tagged!=row.id||room.getDrawPile().isEmpty()
        ||room.getDrawPile().first()!=row.id||room.getCardPlace(row.id)!=Player::DrawPile
        ||room.getCardOwner(row.id)!=nullptr)return unknown;
    const Card *current=room.getCard(row.id);
    if(current!=row.card.data()||current->isVirtualCard()||face(current)!=row.face) {
        l->erase(viewer->objectName());return unknown;
    }
    return {{"known",true},{"serial",QString::number(row.serial)},{"card",row.face}};
}
}

int aiRound29ExtraPrototype(const AIRequest &r,const ServerPlayer *p,
                           const QString &skill,const QString &prototype) {
    if(!p||!p->getRoom()||p->getRoom()->roomRuntime()->isClosing()
        ||r.viewerObjectName!=p->objectName()||r.worldView.self.objectName!=p->objectName()
        ||r.stateRevision!=p->getRoom()->roomRuntime()->stateRevision()
        ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY
        ||!p->hasSkill(skill))return -1;
    struct Row {const char *skill;const char *prototype;int capabilities;};
    static const Row rows[]={
        {"gd_zezhu","#gd_zezhu",0},
        {"guandu_shicai","#guandu_shicai",1},
        {"guandu_yuanlue","#guandu_yuanlue",1},
        {"twyj_huzhu","#twyj_huzhu",0},
        {"twyj_qijia","#twyj_qijia",1},
        {"twyj_rangyi","#twyj_rangyi",0},
        {"twyj_zhuchen","#twyj_zhuchen",1}};
    for(const auto &row:rows)
        if(skill==QLatin1String(row.skill)&&prototype==QLatin1String(row.prototype))return row.capabilities;
    return -1;
}

void recordAIRound29ShicaiPreview(Room &room,ServerPlayer *recipient,
                                 const QList<CardsMoveStruct> &moves,bool isLostPhase) {
    if(!recipient||recipient->getRoom()!=&room||room.roomRuntime()->isClosing())return;
    for(const auto &m:moves) {
        if(m.reason.m_skillName!="guandu_shicai"
            ||m.reason.m_reason!=CardMoveReason::S_REASON_PUT
            ||m.reason.m_playerId!=recipient->objectName())continue;
        const QString name=recipient->objectName();
        if(m.from==recipient&&m.to==nullptr&&m.from_place==Player::PlaceSpecial
            &&m.to_place==Player::PlaceTable&&m.from_pile_name=="&talent") {
            if(auto *l=ledger(room,false))l->erase(name);
            continue;
        }
        if(m.from!=nullptr||m.to!=recipient||m.from_place!=Player::PlaceTable
            ||m.to_place!=Player::PlaceSpecial||m.to_pile_name!="&talent")continue;
        auto *l=ledger(room,true);
        if(!l)continue;
        if(!m.open||m.card_ids.size()!=1||m.card_ids.first()<0
            ||recipient->getPhase()!=Player::Play||!recipient->hasSkill("guandu_shicai")) {
            l->erase(name);continue;
        }
        const int id=m.card_ids.first();
        if(room.getDrawPile().isEmpty()||room.getDrawPile().first()!=id
            ||room.getCardPlace(id)!=Player::DrawPile||room.getCardOwner(id)!=nullptr) {
            l->erase(name);continue;
        }
        if(isLostPhase) {
            l->erase(name);ShicaiLedger::Row row;row.id=id;l->rows.insert(name,row);continue;
        }
        auto it=l->rows.find(name);
        if(it==l->rows.end()||!it->pending||it->id!=id) {l->erase(name);continue;}
        const Card *card=room.getCard(id);
        if(!card||card->isVirtualCard()||card->getEffectiveId()!=id) {l->erase(name);continue;}
        it->card=card;it->face=face(card);it->pending=false;it->serial=++l->serial;
        if(const auto *wrapped=qobject_cast<const WrappedCard *>(card))
            it->changed=QObject::connect(wrapped,&WrappedCard::identityChanged,l,
                [&room,id](){invalidateAIRound29ShicaiPreview(room,id);},Qt::DirectConnection);
    }
}
void invalidateAIRound29ShicaiPreview(Room &room,int id) {
    if(auto *l=ledger(room,false)) {
        const auto keys=l->rows.keys();
        for(const auto &name:keys)if(l->rows.value(name).id==id)l->erase(name);
    }
}
void clearAIRound29ShicaiPreview(Room &room) {
    if(auto *l=ledger(room,false)) {
        const auto keys=l->rows.keys();for(const auto &name:keys)l->erase(name);
    }
}
void projectAIRound29ExtraWorld(AIWorldView &world,Room &room,ServerPlayer *viewer) {
    if(!viewer||world.self.objectName!=viewer->objectName()||viewer->getRoom()!=&room)return;
    world.strategyFacts.insert("extra29",QJsonObject{{"shicai_preview",read(room,viewer,[&] {
        std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,viewer,gameLua));
        return viewer->hasSkill("guandu_shicai");
    })}});
}
const Card *aiRound29ShicaiPreview(const AIRequest &r,Room &room,ServerPlayer *viewer) {
    if(!viewer||viewer->getRoom()!=&room||r.viewerObjectName!=viewer->objectName()
        ||r.worldView.self.objectName!=viewer->objectName()
        ||r.kind!=AIRequest::Activate||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY
        ||r.stateRevision!=room.roomRuntime()->stateRevision())return nullptr;
    const auto snapshot=r.worldView.strategyFacts.value("extra29").toObject().value("shicai_preview").toObject();
    const auto current=read(room,viewer);
    if(!snapshot.value("known").toBool()||snapshot!=current)return nullptr;
    return room.getCard(current.value("card").toObject().value("effective_id").toInt(-1));
}
