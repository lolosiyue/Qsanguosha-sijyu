#include "skill-declaration.h"

#include "card.h"
#include "engine.h"
#include "standard.h"
#include "skill.h"

#include <QVariant>

namespace {

QString declarationObjectName(const SkillDialogInfo &info, const QString &skillName)
{
    return info.objectName.isEmpty() ? skillName : info.objectName;
}

bool isPlay(CardUseStruct::CardUseReason reason)
{
    return reason == CardUseStruct::CARD_USE_REASON_PLAY;
}

}

QString skillDeclarationReasonName(SkillDeclarationReason reason)
{
    switch (reason) {
    case SkillDeclarationReason::None: return QStringLiteral("none");
    case SkillDeclarationReason::EngineUnavailable: return QStringLiteral("engine_unavailable");
    case SkillDeclarationReason::MissingSkill: return QStringLiteral("missing_skill");
    case SkillDeclarationReason::UnsupportedDialog: return QStringLiteral("unsupported_dialog");
    case SkillDeclarationReason::DialogInactive: return QStringLiteral("dialog_inactive");
    case SkillDeclarationReason::NoEnabledCandidate: return QStringLiteral("no_enabled_candidate");
    case SkillDeclarationReason::CandidateUnknown: return QStringLiteral("candidate_unknown");
    case SkillDeclarationReason::CandidateUnavailable: return QStringLiteral("candidate_unavailable");
    case SkillDeclarationReason::CandidateRemoved: return QStringLiteral("candidate_removed");
    case SkillDeclarationReason::CardLocked: return QStringLiteral("card_locked");
    case SkillDeclarationReason::CardUnavailable: return QStringLiteral("card_unavailable");
    case SkillDeclarationReason::InvalidSelection: return QStringLiteral("invalid_selection");
    }
    return QStringLiteral("invalid_selection");
}

SkillDeclarationSession::SkillDeclarationSession(
    const QString &skillName, Player *self, CardUseStruct::CardUseReason reason,
    const QString &pattern, const QStringList &bannedPackages, quint64 requestId)
    : m_skillName(skillName), m_self(self), m_reason(reason), m_pattern(pattern),
      m_bannedPackages(bannedPackages), m_requestId(requestId)
{
    m_bannedPackages.removeDuplicates();
    build();
}

SkillDeclarationSession::SkillDeclarationSession(
    const SkillDialogInfo &info, Player *self, CardUseStruct::CardUseReason reason,
    const QString &pattern, const QStringList &bannedPackages,
    quint64 requestId, const QString &skillName)
    : m_skillName(skillName), m_self(self), m_reason(reason), m_pattern(pattern),
      m_bannedPackages(bannedPackages), m_requestId(requestId), m_info(info),
      m_infoProvided(true)
{
    m_bannedPackages.removeDuplicates();
    build();
}

SkillDeclarationSession::~SkillDeclarationSession()
{
    clearOwnedTag();
    releaseCards();
}

void SkillDeclarationSession::releaseCards()
{
    const QList<QPointer<Card>> cards = m_cards.values();
    m_cards.clear();
    for (const QPointer<Card> &card : cards) {
        if (card != nullptr)
            delete card;
    }
}

void SkillDeclarationSession::clearOwnedTag() const
{
    Player *self = m_self.data();
    if (self == nullptr)
        return;
    const QString key = tagKey();
    const QVariant current = self->getTag(key);
    const Card *tagCard = current.value<const Card *>();
    if (tagCard == nullptr)
        return;
    for (const QPointer<Card> &card : m_cards) {
        if (card.data() == tagCard) {
            self->removeTag(key);
            return;
        }
    }
}

QString SkillDeclarationSession::tagKey() const
{
    return declarationObjectName(m_info, m_skillName);
}

bool SkillDeclarationSession::needsDeclaration() const
{
    if (!m_supported || !m_active)
        return false;
    for (const SkillDeclarationCandidate &candidate : m_candidates) {
        if (candidate.enabled)
            return true;
    }
    return false;
}

void SkillDeclarationSession::clearChoice()
{
    if (m_self != nullptr)
        m_self->removeTag(tagKey());
}

const Card *SkillDeclarationSession::cloneCard(const QString &value) const
{
    const QString declarationType = m_info.parameters.value(QStringLiteral("declarationType")).toString();
    const QString ruleType = declarationType.isEmpty() ? m_info.type : declarationType;
    if (!m_supported || ruleType == QLatin1String("tiansuan"))
        return nullptr;
    bool knownCandidate = false;
    for (const SkillDeclarationCandidate &candidate : m_candidates) {
        if (candidate.value == value) {
            knownCandidate = true;
            break;
        }
    }
    if (!knownCandidate)
        return nullptr;
    const QPointer<Card> cached = m_cards.value(value);
    if (cached != nullptr)
        return cached.data();
    if (Sanguosha == nullptr)
        return nullptr;
    Card *card = Sanguosha->cloneCard(value);
    if (card == nullptr)
        return nullptr;
    card->setSkillName(tagKey());
    card->setCanRecast(false);
    m_cards.insert(value, QPointer<Card>(card));
    return card;
}

bool SkillDeclarationSession::cardCommonlyEnabled(
    const Card *card, const QString &value, SkillDeclarationReason *reason) const
{
    if (card == nullptr || m_self == nullptr) {
        if (reason != nullptr) *reason = SkillDeclarationReason::CandidateUnknown;
        return false;
    }
    const QVariantMap params = m_info.parameters;
    if (params.value(QStringLiteral("checkLocked"), true).toBool()
        && m_self->isLocked(card)) {
        if (reason != nullptr) *reason = SkillDeclarationReason::CardLocked;
        return false;
    }
    const QString declarationType = params.value(QStringLiteral("declarationType")).toString();
    const bool checkAvailability = params.value(QStringLiteral("checkAvailability"), true).toBool();
    const bool playOnly = params.value(QStringLiteral("playOnly"), true).toBool();
    const bool forcedResponse = params.value(QStringLiteral("forceResponse"), false).toBool();
    const bool juguanAnyCard = (m_info.type == QLatin1String("juguan")
        || declarationType == QLatin1String("juguan"))
        && m_info.parameters.value(QStringLiteral("cardNames")).toString().startsWith('$');
    const bool juguan = m_info.type == QLatin1String("juguan")
        || declarationType == QLatin1String("juguan");
    const bool availabilityContext = juguan ? isPlay(m_reason) : (playOnly || isPlay(m_reason));
    if (checkAvailability && !forcedResponse
        && !juguanAnyCard && availabilityContext && !card->isAvailable(m_self)) {
        if (reason != nullptr) *reason = SkillDeclarationReason::CardUnavailable;
        return false;
    }
    if (m_skill != nullptr) {
        const SkillDeclarationReason specialized = m_skill->declarationReason(m_self, value, card);
        if (specialized != SkillDeclarationReason::None) {
            if (reason != nullptr) *reason = specialized;
            return false;
        }
    }
    if (reason != nullptr) *reason = SkillDeclarationReason::None;
    return true;
}

SkillDeclarationCandidate SkillDeclarationSession::cardCandidate(
    const QString &value, Card *card) const
{
    SkillDeclarationCandidate candidate;
    candidate.value = value;
    candidate.label = value;
    candidate.kind = QStringLiteral("card");
    SkillDeclarationReason reason = SkillDeclarationReason::None;
    candidate.enabled = cardCommonlyEnabled(card, value, &reason);
    candidate.reason = reason;
    return candidate;
}

void SkillDeclarationSession::build()
{
    m_candidates.clear();
    releaseCards();
    if (!m_infoProvided)
        m_info = SkillDialogInfo();
    m_supported = false;
    m_active = false;
    m_reasonCode = SkillDeclarationReason::None;
    if (Sanguosha == nullptr) {
        m_reasonCode = SkillDeclarationReason::EngineUnavailable;
        return;
    }
    const QString lookupSkillName = m_skillName.isEmpty() ? m_info.objectName : m_skillName;
    m_skill = Sanguosha->getSkill(lookupSkillName);
    if (m_skill == nullptr) {
        const ViewAsSkill *viewAs = Sanguosha->getViewAsSkill(lookupSkillName);
        m_skill = viewAs;
    }
    if (m_skill == nullptr && !m_infoProvided) {
        m_reasonCode = SkillDeclarationReason::MissingSkill;
        return;
    }
    if (!m_infoProvided) {
        m_info = m_skill->getDialogInfo();
        if (!m_info.isValid()) {
            if (const ViewAsSkill *viewAs = Sanguosha->getViewAsSkill(lookupSkillName)) {
                m_skill = viewAs;
                m_info = viewAs->getDialogInfo();
            }
        }
    }
    if (!m_info.isValid()) {
        m_reasonCode = SkillDeclarationReason::UnsupportedDialog;
        return;
    }

    const QString declarationType = m_info.parameters.value(QStringLiteral("declarationType")).toString();
    const QString ruleType = declarationType.isEmpty() ? m_info.type : declarationType;
    if (ruleType == QLatin1String("guhuo")) {
        m_supported = true;
        m_active = !m_info.parameters.value(QStringLiteral("playOnly"), true).toBool()
            || isPlay(m_reason);
        if (!m_active) {
            m_reasonCode = SkillDeclarationReason::DialogInactive;
            return;
        }
        const bool left = m_info.parameters.value(QStringLiteral("left"), true).toBool();
        const bool right = m_info.parameters.value(QStringLiteral("right"), true).toBool();
        const bool slashCombined = m_info.parameters.value(QStringLiteral("slashCombined"), false).toBool();
        const bool delayed = m_info.parameters.value(QStringLiteral("delayedTricks"), false).toBool();
        QList<const Card *> prototypes;
        for (const BasicCard *card : Sanguosha->findChildren<const BasicCard *>())
            prototypes.append(card);
        for (const TrickCard *card : Sanguosha->findChildren<const TrickCard *>())
            prototypes.append(card);
        for (const Card *engineCard : prototypes) {
            if (engineCard == nullptr || engineCard->objectName().startsWith('_')
                || m_bannedPackages.contains(engineCard->getPackage()))
                continue;
            const bool basic = engineCard->isKindOf("BasicCard");
            const bool trick = engineCard->isKindOf("TrickCard");
            if ((!left && basic) || (!right && trick) || (!basic && !trick)
                || (slashCombined && engineCard->isKindOf("Slash")
                    && engineCard->objectName() != QLatin1String("slash"))
                || (trick && !delayed && !engineCard->isNDTrick()))
                continue;
            if (m_cards.contains(engineCard->objectName()))
                continue;
            Card *card = Sanguosha->cloneCard(engineCard->objectName());
            if (card == nullptr)
                continue;
            card->setSkillName(tagKey());
            card->setCanRecast(false);
            m_cards.insert(engineCard->objectName(), QPointer<Card>(card));
            m_candidates.append(cardCandidate(engineCard->objectName(), card));
        }
    } else if (ruleType == QLatin1String("juguan")) {
        m_supported = true;
        const QString raw = m_info.parameters.value(QStringLiteral("cardNames")).toString();
        m_active = !raw.isEmpty() && (raw.endsWith('!') || isPlay(m_reason));
        if (!m_active) {
            m_reasonCode = SkillDeclarationReason::DialogInactive;
            return;
        }
        QString names = raw;
        names.remove('!');
        names.remove('$');
        for (const QString &name : names.split(',', Qt::SkipEmptyParts)) {
            const QString value = name.trimmed();
            Card *card = Sanguosha->cloneCard(value);
            if (card != nullptr) {
                card->setSkillName(tagKey());
                card->setCanRecast(false);
                m_cards.insert(value, QPointer<Card>(card));
                m_candidates.append(cardCandidate(value, card));
            }
        }
    } else if (ruleType == QLatin1String("tiansuan")) {
        m_supported = true;
        m_active = true;
        const QString key = tagKey() + QStringLiteral("_tiansuan_remove_");
        for (const QString &raw : m_info.parameters.value(QStringLiteral("choices")).toString()
                 .split(',', Qt::SkipEmptyParts)) {
            const QString value = raw.trimmed();
            SkillDeclarationCandidate candidate;
            candidate.value = value;
            candidate.label = value;
            candidate.kind = QStringLiteral("choice");
            candidate.enabled = true;
            for (const QString &mark : m_self != nullptr ? m_self->getMarkNames() : QStringList()) {
                if (mark.startsWith(key + value) && m_self->getMark(mark) > 0) {
                    candidate.enabled = false;
                    candidate.reason = SkillDeclarationReason::CandidateRemoved;
                    break;
                }
            }
            m_candidates.append(candidate);
        }
    } else {
        const QList<SkillDeclarationCandidate> provided =
            m_skill != nullptr
                ? m_skill->declarationCandidates(m_self, m_reason, m_pattern,
                                                 m_bannedPackages, m_requestId)
                : QList<SkillDeclarationCandidate>();
        // Dynamic declarations opt in explicitly so an empty result can mean
        // "supported, but no eligible candidate". Legacy named dialogs such
        // as mobilejianying remain unsupported until they publish metadata.
        const bool customDeclaration
            = m_info.parameters.value(QStringLiteral("customDeclaration"), false).toBool();
        if (customDeclaration || !provided.isEmpty()) {
            m_supported = true;
            m_active = true;
            m_candidates = provided;
        } else {
            m_reasonCode = SkillDeclarationReason::UnsupportedDialog;
        }
    }
    if (m_supported && m_candidates.isEmpty() && m_reasonCode == SkillDeclarationReason::None)
        m_reasonCode = SkillDeclarationReason::NoEnabledCandidate;
}

SkillDeclarationValidation SkillDeclarationSession::validate(const QString &value) const
{
    SkillDeclarationValidation result;
    result.canonicalValue = value;
    if (m_self == nullptr) {
        result.reason = SkillDeclarationReason::EngineUnavailable;
        return result;
    }
    if (!m_supported) {
        result.reason = m_reasonCode == SkillDeclarationReason::None
            ? SkillDeclarationReason::UnsupportedDialog : m_reasonCode;
        return result;
    }
    for (const SkillDeclarationCandidate &candidate : m_candidates) {
        if (candidate.value.compare(value, Qt::CaseInsensitive) != 0
            && candidate.label != value)
            continue;
        result.canonicalValue = candidate.value;
        SkillDeclarationReason liveReason = candidate.reason;
        bool liveEnabled = candidate.enabled;
        const Card *card = candidate.kind == QLatin1String("card")
            ? cloneCard(candidate.value) : nullptr;
        const QString declarationType = m_info.parameters.value(QStringLiteral("declarationType")).toString();
        const QString ruleType = declarationType.isEmpty() ? m_info.type : declarationType;
        if (ruleType == QLatin1String("tiansuan")) {
            liveEnabled = true;
            const QString prefix = tagKey() + QStringLiteral("_tiansuan_remove_") + candidate.value;
            for (const QString &mark : m_self != nullptr ? m_self->getMarkNames() : QStringList()) {
                if (mark.startsWith(prefix) && m_self->getMark(mark) > 0) {
                    liveEnabled = false;
                    liveReason = SkillDeclarationReason::CandidateRemoved;
                    break;
                }
            }
        } else if (card != nullptr) {
            liveEnabled = cardCommonlyEnabled(card, candidate.value, &liveReason);
        } else if (m_skill != nullptr) {
            const SkillDeclarationReason specialized =
                m_skill->declarationReason(m_self, candidate.value, nullptr);
            if (specialized != SkillDeclarationReason::None) {
                liveEnabled = false;
                liveReason = specialized;
            }
        }
        if (!liveEnabled) {
            result.reason = liveReason == SkillDeclarationReason::None
                ? SkillDeclarationReason::CandidateUnavailable : liveReason;
            return result;
        }
        result.accepted = true;
        result.reason = SkillDeclarationReason::None;
        return result;
    }
    result.reason = SkillDeclarationReason::CandidateUnknown;
    return result;
}

bool SkillDeclarationSession::apply(const QString &value)
{
    clearChoice();
    if (value.isEmpty())
        return !needsDeclaration();
    const SkillDeclarationValidation validation = validate(value);
    if (!validation.accepted || m_self == nullptr)
        return false;
    const SkillDeclarationCandidate *selected = nullptr;
    for (const SkillDeclarationCandidate &candidate : m_candidates) {
        if (candidate.value == validation.canonicalValue) {
            selected = &candidate;
            break;
        }
    }
    if (selected == nullptr)
        return false;
    if (selected->kind == QLatin1String("card")) {
        const Card *card = cloneCard(selected->value);
        if (card == nullptr)
            return false;
        m_self->setTag(tagKey(), QVariant::fromValue(card));
    } else {
        m_self->setTag(tagKey(), selected->value);
    }
    return true;
}

void SkillDeclarationSession::refresh()
{
    clearOwnedTag();
    build();
}
