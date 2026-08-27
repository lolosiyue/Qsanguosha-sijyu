#include "local-response-ui-inspector.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QLabel *makeValueLabel(QWidget *parent)
{
    QLabel *label = new QLabel(parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

}

LocalResponseUiInspector::LocalResponseUiInspector(QWidget *parent)
    : QWidget(parent, Qt::Tool),
      m_caseValue(makeValueLabel(this)), m_modeValue(makeValueLabel(this)),
      m_requestValue(makeValueLabel(this)), m_clientValue(makeValueLabel(this)),
      m_presentationValue(makeValueLabel(this)),
      m_replyReceivedValue(makeValueLabel(this)),
      m_replyCommandValue(makeValueLabel(this)), m_replyBodyValue(makeValueLabel(this)),
      m_finalValue(makeValueLabel(this)),
      m_nextActionButton(new QPushButton(tr("Run Next Case Action"), this)),
      m_remainingActionsButton(new QPushButton(tr("Run Remaining Case Actions"), this)),
      m_snapshotButton(new QPushButton(tr("Save Snapshot"), this)),
      m_screenshotButton(new QPushButton(tr("Save Screenshot"), this)),
      m_closeButton(new QPushButton(tr("Close"), this))
{
    setWindowTitle(tr("Local askFor UI Inspector"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(620, 420);

    QFormLayout *fields = new QFormLayout;
    fields->addRow(tr("Case"), m_caseValue);
    fields->addRow(tr("Mode"), m_modeValue);
    fields->addRow(tr("Request command / serial"), m_requestValue);
    fields->addRow(tr("Client status / pattern"), m_clientValue);
    fields->addRow(tr("Presentation assertion"), m_presentationValue);
    fields->addRow(tr("Reply received"), m_replyReceivedValue);
    fields->addRow(tr("Reply command"), m_replyCommandValue);
    fields->addRow(tr("Reply body"), m_replyBodyValue);
    fields->addRow(tr("Final result"), m_finalValue);

    QHBoxLayout *actions = new QHBoxLayout;
    actions->addWidget(m_nextActionButton);
    actions->addWidget(m_remainingActionsButton);
    actions->addWidget(m_snapshotButton);
    actions->addWidget(m_screenshotButton);
    actions->addStretch();
    actions->addWidget(m_closeButton);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addLayout(fields);
    layout->addStretch();
    layout->addLayout(actions);

    setPresentationResult(tr("Pending"));
    setReply(false);
    setFinalResult(tr("Awaiting presentation"));
}

void LocalResponseUiInspector::setCaseName(const QString &value) { m_caseValue->setText(value); }
void LocalResponseUiInspector::setMode(const QString &value) { m_modeValue->setText(value); }

void LocalResponseUiInspector::setRequest(const QString &command, int serial)
{
    m_requestValue->setText(QStringLiteral("%1 / %2").arg(command).arg(serial));
}

void LocalResponseUiInspector::setClientState(const QString &status, const QString &pattern)
{
    m_clientValue->setText(QStringLiteral("%1 / %2").arg(status, pattern));
}

void LocalResponseUiInspector::setPresentationResult(const QString &value)
{
    m_presentationValue->setText(value);
}

void LocalResponseUiInspector::setReply(bool received, const QString &command,
    const QString &body)
{
    m_replyReceivedValue->setText(received ? tr("Yes") : tr("No"));
    m_replyCommandValue->setText(command);
    m_replyBodyValue->setText(body);
}

void LocalResponseUiInspector::setFinalResult(const QString &value)
{
    m_finalValue->setText(value);
}

QPushButton *LocalResponseUiInspector::nextActionButton() const { return m_nextActionButton; }
QPushButton *LocalResponseUiInspector::remainingActionsButton() const { return m_remainingActionsButton; }
QPushButton *LocalResponseUiInspector::snapshotButton() const { return m_snapshotButton; }
QPushButton *LocalResponseUiInspector::screenshotButton() const { return m_screenshotButton; }
QPushButton *LocalResponseUiInspector::closeButton() const { return m_closeButton; }
