#include "notified-pile-query-scope.h"
#include "ai-strategy/owner.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include <QJsonArray>
#include <QSet>
#include <limits>
#include <cmath>

namespace {
bool exactIds(const QVariant &value, QList<int> *ids)
{
    if (value.metaType().id() != QMetaType::QVariantList) return false;
    QSet<int> seen;
    for (const QVariant &item : value.toList()) {
        // notifyMoveToPile writes ListI2V, not strings/coercible card names.
        if (item.metaType().id() != QMetaType::Int || item.toInt() < 0
            || seen.contains(item.toInt())) return false;
        seen.insert(item.toInt()); ids->append(item.toInt());
    }
    return !ids->isEmpty();
}
bool jsonId(const QJsonValue &value, int *id)
{
    if (!value.isDouble()) return false;
    const double n = value.toDouble();
    if (!std::isfinite(n) || n < 0 || n > std::numeric_limits<int>::max()
        || std::floor(n) != n) return false;
    *id = int(n); return true;
}
}
thread_local NotifiedPileQueryScope *NotifiedPileQueryScope::s_current = nullptr;

NotifiedPileQueryScope::NotifiedPileQueryScope(const ActiveSkillRequest &request,
                                             const ViewAsSkillV2 *skill)
    : m_previous(s_current)
{
    initialize(request, skill);
    // Publish only after construction succeeds; even an invalid nested request
    // masks the outer request's authority until its destructor restores it.
    s_current = this;
}

void NotifiedPileQueryScope::initialize(const ActiveSkillRequest &request, const ViewAsSkillV2 *skill)
{
    static const QSet<QString> names{ "zuguangu", "zukaiji", "zugaobian",
        "zugaoshi", "kezuliuju", "kezuhuanjia", "keolzhubi", "keolzhuri", "keolsaoguslash", "olxiaofan", "ol_xiongdiqixin" };
    if (!skill || request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        || !names.contains(skill->objectName())
        || request.pattern != (skill->objectName()=="olxiaofan" ? "@@olxiaofan2" : skill->objectName()=="ol_xiongdiqixin" ? "@@ol_xiongdiqixin!" : "@@" + skill->objectName())
        || skill->getExpandPile() != "#" + skill->objectName()
        || Sanguosha->getViewAsSkill(skill->objectName()) != skill
        || request.selectedCardIds.size() > ((skill->objectName()=="keolzhubi"||skill->objectName()=="ol_xiongdiqixin")?128:1)) return;
    auto *owner = const_cast<ServerPlayer *>(dynamic_cast<const ServerPlayer *>(request.initiator));
    if (!owner || !owner->getRoom()) return;
    Room *room = owner->getRoom();
    if (room->findPlayerByObjectName(owner->objectName(), true) != owner
        || !request.activationRef.isValid()
        || request.activationRef.ownerObjectName != owner->objectName()
        || request.activationRef.key.skillName != skill->objectName()
        || !owner->getValidSkillInstanceIds(skill->objectName()).contains(request.activationRef.key.instanceID)
        || !request.sourceRef.isValid()
        || room->resolveSkillInstanceRootRef(request.activationRef) != request.sourceRef) return;
    m_pile = "#" + skill->objectName(); m_tag = skill->objectName() + "ForAI";
    if (!exactIds(owner->getTag(m_tag), &m_ids)) return;
    for (int id : request.selectedCardIds) if (!m_ids.contains(id) && !((skill->objectName()=="keolzhubi"||skill->objectName()=="ol_xiongdiqixin") && (owner->handCards().contains(id)||owner->getEquipsId().contains(id)))) return;
    const QJsonObject row = readAIStrategyNotifiedPile(*room, owner, m_pile);
    if (!row.value("locations").isObject() || !row.value("cards").isObject()
        || !row.value("current_card_ids").isArray()) return;
    const auto locations = row.value("locations").toObject();
    const auto cards = row.value("cards").toObject();
    const auto current = row.value("current_card_ids").toArray();
    if (locations.size() != m_ids.size() || cards.size() != m_ids.size()
        || current.size() != m_ids.size()) return;
    QSet<int> currentIds;
    for (const QJsonValue &v : current) {
        int id = -1;
        if (!jsonId(v, &id) || currentIds.contains(id) || !m_ids.contains(id)) return;
        currentIds.insert(id);
    }
    for (int id : m_ids) {
        const QString key = QString::number(id);
        if (!locations.value(key).isObject() || !cards.value(key).isObject()) return;
        const auto face = cards.value(key).toObject();
        int faceId = -1, effectiveId = -1;
        if (!jsonId(face.value("id"), &faceId) || faceId != id
            || !jsonId(face.value("effective_id"), &effectiveId) || effectiveId != id
            || face.value("name").toString().isEmpty()
            || face.value("class_name").toString().isEmpty()) return;
    }
    m_room = room; m_owner = owner;
    m_activation = request.activationRef; m_source = request.sourceRef;
    m_revision = room->roomRuntime()->stateRevision(); m_active = true;
}

NotifiedPileQueryScope::~NotifiedPileQueryScope() { s_current = m_previous; }

bool NotifiedPileQueryScope::isActive() const
{
    if (!m_active || !m_room || !m_owner
        || m_room->roomRuntime()->stateRevision() != m_revision
        || !m_owner->getValidSkillInstanceIds(m_activation.key.skillName).contains(m_activation.key.instanceID)
        || m_room->resolveSkillInstanceRootRef(m_activation) != m_source) return false;
    QList<int> current;
    return exactIds(m_owner->getTag(m_tag), &current) && current == m_ids
        && !readAIStrategyNotifiedPile(*m_room, m_owner, m_pile).value("locations").toObject().isEmpty();
}

bool NotifiedPileQueryScope::pile(const Player *player, const QString &name, QList<int> *result)
{
    const auto *scope = s_current;
    if (!result || !scope || player != scope->m_owner || name != scope->m_pile || !scope->isActive()) return false;
    *result = scope->m_ids; return true;
}

bool NotifiedPileQueryScope::pileName(const Player *player, int id, QString *result)
{
    const auto *scope = s_current;
    if (!result || !scope || player != scope->m_owner || !scope->m_ids.contains(id) || !scope->isActive()) return false;
    *result = scope->m_pile; return true;
}
