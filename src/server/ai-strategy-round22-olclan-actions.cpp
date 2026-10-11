#include "ai-strategy-round22-olclan-actions.h"
#include "ai.h"
#include "card.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>

bool aiRound22OlClanActionConstruction(const AIRequest &request, const ServerPlayer *viewer,
                                const QJsonObject &spec, QString *error)
{
    const auto reject = [&](const char *why) { if (error) *error=QString::fromLatin1(why); return false; };
    if (!viewer || !viewer->getRoom() || request.viewerObjectName != viewer->objectName()
        || request.stateRevision != viewer->getRoom()->roomRuntime()->stateRevision()
        || request.kind != AIRequest::Activate || request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
        return reject("OlClan construction requires the current owner play request");
    static const QSet<QString> fields{"source_key","name","skill","suit","number","subcards"};
    if (spec.size()!=fields.size()) return reject("OlClan constructor field set differs from source");
    for (auto it=spec.begin();it!=spec.end();++it)
        if (!fields.contains(it.key())) return reject("OlClan constructor contains an unaudited field");
    if (!spec.value("source_key").isString() || !spec.value("name").isString()
        || !spec.value("skill").isString() || !spec.value("suit").isDouble()
        || !spec.value("number").isDouble() || !spec.value("subcards").isArray()
        || spec.value("suit").toDouble()!=int(Card::NoSuit) || spec.value("number").toDouble()!=0)
        return reject("OlClan source constructor is not exact NoSuit zero");
    const QString key=spec.value("source_key").toString();
    QString name,skill;int count=-1;
    // b6025 ai/olClan-ai.lua:186,607,879,1818. Source cloneCard/dummyCard
    // defaults are NoSuit/0, followed by the listed raw skill and materials.
    if (key=="zuzhanding") { name="slash";skill=key;count=1; }
    else if (key=="kezuxumin") { name="amazing_grace";skill=key;count=1; }
    else if (key=="kezubolong") { name="thunder_slash";skill=key;count=0; }
    else if (key=="zutanfeng") { name="slash";count=0; }
    else return reject("OlClan constructor source is not audited");
    const QJsonArray ids=spec.value("subcards").toArray();
    if (spec.value("name").toString()!=name || spec.value("skill").toString()!=skill || ids.size()!=count
        || !viewer->hasSkill(key)) return reject("OlClan constructor product or owner source differs");
    for (const auto &value:ids) {
        const double n=value.toDouble(-1);
        if (!value.isDouble() || !std::isfinite(n) || n<0 || n>std::numeric_limits<int>::max() || n!=std::floor(n))
            return reject("OlClan source material ID is invalid");
        const int id=int(n);bool disclosed=false;
        for (const auto &card:request.worldView.handCards) if (card.effectiveId==id) disclosed=true;
        for (const auto &card:request.worldView.self.equips) if (card.effectiveId==id) disclosed=true;
        const auto place=viewer->getRoom()->getCardPlace(id);
        if (!disclosed || viewer->getRoom()->getCardOwner(id)!=viewer
            || (place!=Player::PlaceHand && place!=Player::PlaceEquip))
            return reject("OlClan source material is outside disclosed owner he");
    }
    if (error) error->clear();
    return true;
}

bool applyAIRound22OlClanActionConstruction(const AIRequest &request, const ServerPlayer *viewer,
                                     const QJsonObject &spec, Card &card)
{
    if (!aiRound22OlClanActionConstruction(request,viewer,spec) || card.getId()>=0
        || card.objectName()!=spec.value("name").toString()
        || card.getSkillName(false)!=spec.value("skill").toString()) return false;
    QList<int> ids;for (const auto &id:spec.value("subcards").toArray()) ids << id.toInt();
    if (card.getSubcards()!=ids) return false;
    // The source flag belongs to this original detached clone. It is retained
    // by the existing readonly recipe, not reconstructed from a Lua flag table.
    if (spec.value("source_key").toString()=="zutanfeng") card.setFlags("Qinggang");
    return true;
}

int aiRound22OlClanActionPrototype(const QString &skill,const QString &prototype)
{
    // Skill::Skill strips the trailing '&' (skill.cpp:46-53); native objectName
    // and issued activation are zulianzhuvs, not the registration spelling.
    // Parent holder/attached activation: olClan.lua1003-1133. No forged parent
    // skill claim: caller's already-issued native source scope is required.
    if (skill=="zulianzhuvs" && prototype=="#zulianzhuCard") return 1;
    if ((skill=="kezubolong" && prototype=="#kezubolongCard")
        || (skill=="kezuxumin" && prototype=="#kezuxuminCard")
        || (skill=="zutanfeng" && prototype=="#zutanfengCard")) return 0;
    return -1;
}
