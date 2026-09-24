#ifndef TUI_INTERACTION_VIEW_H
#define TUI_INTERACTION_VIEW_H

#include "core/client-interaction-view.h"
#include "tui-renderer.h"

#include <QList>
#include <functional>

class TuiInteractionView final : public IClientInteractionView
{
public:
    using Writer = std::function<void(const QString &)>;
    using CardTextResolver = std::function<QString(int)>;
    using SkillCardResolver = std::function<QString(const QString &, int,
                                                    const QList<int> &, QString *)>;
    // Applies the declaration a dialog skill asks for -- the card Guhuo names,
    // the choice Tiansuan makes -- before its card is built. An empty option
    // only clears the last one; false means the answer cannot go through, and
    // the error already lists what the skill would accept.
    using SkillDeclarationResolver = std::function<bool(const QString &, const QString &,
                                                        QString *)>;

    TuiInteractionView(TuiRenderer *renderer, Writer writer,
                       CardTextResolver cardTextResolver = CardTextResolver(),
                       SkillCardResolver skillCardResolver = SkillCardResolver(),
                       SkillDeclarationResolver skillDeclarationResolver
                           = SkillDeclarationResolver());

    void presentRequest(const InteractionRequest &request) override;
    static QString rejectionText(const InteractionValidation &validation);
    static QString cancelReasonText(InteractionCancelReason reason);
    void finishRequest(const InteractionRequest &request,
                       const InteractionResponse &response) override;
    void cancelRequest(const InteractionRequest &request,
                       InteractionCancelReason reason) override;
    void rejectResponse(const InteractionRequest &request,
                        const InteractionResponse &response,
                        const InteractionValidation &validation) override;

    bool parseAnswer(const InteractionRequest &request, const QString &line,
                     InteractionResponse *response, QString *error) const;
    // True means a local draft command was handled without a wire response.
    bool stageGeneralAnswer(const InteractionRequest &request, QString *line);
    QStringList selectedGenerals(quint64 requestId) const
    { return requestId == m_generalRequestId ? m_generalDraft : QStringList(); }

private:
    quint64 m_generalRequestId = 0;
    QStringList m_generalDraft;
    QString requestTitle(const InteractionRequest &request) const;
    QList<int> parseIndexes(const QString &text, int size, QString *error) const;
    // allowRepeats is for card targets: a card whose targetFilter() hands back
    // more than one vote for a player -- Collateral, GreatYeyanCard -- is meant
    // to be aimed at that player again, and only the engine knows how often.
    // The name list stays a syntax check; the count is the controller's call.
    QStringList parseNames(const QString &text, const QStringList &values,
                           QString *error, bool allowRepeats = false) const;

    TuiRenderer *m_renderer = nullptr;
    Writer m_writer;
    CardTextResolver m_cardTextResolver;
    SkillCardResolver m_skillCardResolver;
    SkillDeclarationResolver m_skillDeclarationResolver;
};

#endif
