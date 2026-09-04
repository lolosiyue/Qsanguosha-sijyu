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

    TuiInteractionView(TuiRenderer *renderer, Writer writer,
                       CardTextResolver cardTextResolver = CardTextResolver(),
                       SkillCardResolver skillCardResolver = SkillCardResolver());

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

private:
    QString requestTitle(const InteractionRequest &request) const;
    QList<int> parseIndexes(const QString &text, int size, QString *error) const;
    QStringList parseNames(const QString &text, const QStringList &values,
                           QString *error) const;

    TuiRenderer *m_renderer = nullptr;
    Writer m_writer;
    CardTextResolver m_cardTextResolver;
    SkillCardResolver m_skillCardResolver;
};

#endif
