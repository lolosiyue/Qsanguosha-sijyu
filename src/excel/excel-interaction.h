#ifndef QSANGUOSHA_EXCEL_INTERACTION_H
#define QSANGUOSHA_EXCEL_INTERACTION_H

#include "client/core/interaction-model.h"
#include "protocol/protocol-message.h"

#include <QJsonObject>

class ClientCore;

// QtCore-only boundary used by the Excel/VBA transport.  It owns neither the
// engine nor ClientCore; callers retain both for the lifetime of the adapter.
class ExcelInteractionAdapter final
{
public:
    explicit ExcelInteractionAdapter(ClientCore *core);

    bool beginRequest(const QSanProtocol::ProtocolMessage &message,
                      QString *error = nullptr);
    QJsonObject requestJson(QString *error = nullptr) const;
    QJsonObject evaluateDraft(const QJsonObject &draft,
                              QString *error = nullptr) const;
    bool makeResponse(const QJsonObject &draft, InteractionResponse *response,
                      QString *error = nullptr) const;

private:
    ClientCore *m_core = nullptr;
    // Keep the exact room payload alongside the canonical request.  The
    // native rules runtime needs this wire object to rebuild its prompt.
    quint64 m_wireRequestId = 0;
    QJsonObject m_wirePayload;
};

#endif
