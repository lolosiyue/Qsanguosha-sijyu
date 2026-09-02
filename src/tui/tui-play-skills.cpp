#include "tui-play-skills.h"

#include "card.h"
#include "client-game-state.h"
#include "engine.h"
#include "skill.h"

#include <QCoreApplication>
#include <QObject>
#include <QSet>

namespace {

QString tr(const char *source)
{
    return QCoreApplication::translate("QSanguoshaTui", source);
}

} // namespace

void tuiFillPlaySkillCandidates(const ClientGameState &state, CardInteractionPayload *payload)
{
    if (payload == nullptr || Sanguosha == nullptr)
        return;
    const QString self = state.selfName();
    if (self.isEmpty())
        return;

    QSet<QString> seenKeys;
    QSet<QString> seenNames;
    auto addSkill = [&](const QString &name, int instanceId) {
        const Skill *skill = Sanguosha->getSkill(name);
        if (skill == nullptr || skill->isHideSkill() || !skill->isVisible())
            return;
        if (skill->inherits("FilterSkill"))
            return;
        if (ViewAsSkill::parseViewAsSkill(skill) == nullptr)
            return;
        const QString key = QStringLiteral("%1#%2").arg(name).arg(instanceId);
        if (seenKeys.contains(key))
            return;
        if (instanceId <= 0 && seenNames.contains(name))
            return;
        seenKeys.insert(key);
        if (instanceId > 0)
            seenNames.insert(name);
        SkillActivationCandidate candidate;
        candidate.skillName = name;
        candidate.instanceId = instanceId;
        payload->skillCandidates.append(candidate);
    };

    const QVariantMap instances = state.playerValue(self, QStringLiteral("skill_instances")).toMap();
    for (auto it = instances.constBegin(); it != instances.constEnd(); ++it) {
        const QVariantMap entry = it.value().toMap();
        if (!entry.value(QStringLiteral("visible"), true).toBool())
            continue;
        addSkill(entry.value(QStringLiteral("skill_name")).toString(),
                 entry.value(QStringLiteral("instance_id")).toInt());
    }
    for (const QString &name : state.playerValue(self, QStringLiteral("skills")).toStringList())
        addSkill(name, 0);
    for (int cardId : state.cardsForPlayer(self, 1)) {
        const Card *equip = Sanguosha->getEngineCard(cardId);
        if (equip != nullptr)
            addSkill(equip->objectName(), 0);
    }
}

QString tuiResolveSkillCardWireText(const QString &selfName, const QString &skillName,
                                    int instanceId, const QList<int> &subcardIds, QString *error)
{
    if (Sanguosha == nullptr) {
        if (error != nullptr)
            *error = tr("引擎尚未載入");
        return QString();
    }
    const ViewAsSkill *viewAs = Sanguosha->getViewAsSkill(skillName);
    if (viewAs == nullptr) {
        if (error != nullptr)
            *error = tr("沒有這個轉換技");
        return QString();
    }

    const Card *card = nullptr;
    if (const auto *v2 = dynamic_cast<const ViewAsSkillV2 *>(viewAs)) {
        ActiveSkillRequest request;
        request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
        request.selectedCardIds = subcardIds;
        request.activationRef = SkillInstanceRef(selfName,
            SkillInstanceKey(skillName, instanceId));
        card = v2->createCard(request);
    } else if (const auto *zero = qobject_cast<const ZeroCardViewAsSkill *>(viewAs)) {
        card = zero->viewAs();
    } else {
        QList<const Card *> selected;
        for (int cardId : subcardIds) {
            const Card *subcard = Sanguosha->getEngineCard(cardId);
            if (subcard == nullptr) {
                if (error != nullptr)
                    *error = tr("沒有這張牌");
                return QString();
            }
            selected.append(subcard);
        }
        card = viewAs->viewAs(selected);
    }
    if (card == nullptr) {
        if (error != nullptr) {
            *error = subcardIds.isEmpty()
                ? tr("此技能需要選手牌")
                : tr("這些牌不能發動該技能");
        }
        return QString();
    }

    Card *mutableCard = const_cast<Card *>(card);
    mutableCard->setActivationSkill(skillName, instanceId);
    const QString text = card->toString();
    // Card::deleteLater() also drain()s the global lifetime manager and can
    // reap unrelated pending engine cards. Queue only this temporary virtual.
    if (card->isVirtualCard() && card->parent() == nullptr)
        mutableCard->QObject::deleteLater();
    return text;
}
