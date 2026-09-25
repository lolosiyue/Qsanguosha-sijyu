#ifndef _SKILL_DECLARATION_H
#define _SKILL_DECLARATION_H

#include "skill-dialog-info.h"
#include "structs.h"

#include <QHash>
#include <QList>
#include <QPointer>
#include <QStringList>

class Card;
class Player;
class Skill;

enum class SkillDeclarationReason
{
    None,
    EngineUnavailable,
    MissingSkill,
    UnsupportedDialog,
    DialogInactive,
    NoEnabledCandidate,
    CandidateUnknown,
    CandidateUnavailable,
    CandidateRemoved,
    CardLocked,
    CardUnavailable,
    InvalidSelection
};

QString skillDeclarationReasonName(SkillDeclarationReason reason);

struct SkillDeclarationCandidate
{
    QString value;
    QString label;
    QString kind;
    QString group;
    bool enabled = false;
    SkillDeclarationReason reason = SkillDeclarationReason::None;
};

struct SkillDeclarationValidation
{
    bool accepted = false;
    QString canonicalValue;
    SkillDeclarationReason reason = SkillDeclarationReason::None;
};

// A read-only declaration snapshot plus an explicit, short-lived commit
// boundary. Native card clones stay owned by this session.
class SkillDeclarationSession final
{
public:
    SkillDeclarationSession(const QString &skillName, Player *self,
                             CardUseStruct::CardUseReason reason,
                             const QString &pattern = QString(),
                             const QStringList &bannedPackages = QStringList(),
                             quint64 requestId = 0);
    SkillDeclarationSession(const SkillDialogInfo &info, Player *self,
                             CardUseStruct::CardUseReason reason,
                             const QString &pattern = QString(),
                             const QStringList &bannedPackages = QStringList(),
                             quint64 requestId = 0,
                             const QString &skillName = QString());
    ~SkillDeclarationSession();

    SkillDeclarationSession(const SkillDeclarationSession &) = delete;
    SkillDeclarationSession &operator=(const SkillDeclarationSession &) = delete;

    // Whether a guhuo/juguan/tiansuan dialog declares anything for this reason,
    // without building candidates.
    static bool activeFor(const SkillDialogInfo &info, CardUseStruct::CardUseReason reason);
    // The Self tag holding the committed choice: a Card clone, or a plain string.
    static QString tagKeyFor(const SkillDialogInfo &info, const QString &skillName);

    SkillDialogInfo info() const { return m_info; }
    QString skillName() const { return m_skillName; }
    bool supported() const { return m_supported; }
    bool active() const { return m_active; }
    bool needsDeclaration() const;
    SkillDeclarationReason reasonCode() const { return m_reasonCode; }
    QList<SkillDeclarationCandidate> candidates() const { return m_candidates; }

    const Card *cloneCard(const QString &value) const;
    SkillDeclarationValidation validate(const QString &value) const;
    // Clears the current declaration, including a previously selected string.
    void clearChoice();
    bool apply(const QString &value);
    void refresh();

private:
    void clearOwnedTag() const;
    void releaseCards();
    void build();
    SkillDeclarationCandidate cardCandidate(const QString &value, Card *card) const;
    bool cardCommonlyEnabled(const Card *card, const QString &value,
                             SkillDeclarationReason *reason) const;
    QString tagKey() const;

    QString m_skillName;
    QPointer<Player> m_self;
    CardUseStruct::CardUseReason m_reason;
    QString m_pattern;
    QStringList m_bannedPackages;
    quint64 m_requestId = 0;
    const Skill *m_skill = nullptr;
    SkillDialogInfo m_info;
    QList<SkillDeclarationCandidate> m_candidates;
    mutable QHash<QString, QPointer<Card>> m_cards;
    bool m_supported = false;
    bool m_active = false;
    SkillDeclarationReason m_reasonCode = SkillDeclarationReason::None;
    bool m_infoProvided = false;
};

#endif
