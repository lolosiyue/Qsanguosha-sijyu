#include "controller-custom-dialog.h"

#include "controller-interaction-contract.h"
#include "roomscene.h"
#include "client.h"
#include "client-core.h"
#include "client-live-session.h"
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QMainWindow>
#include <QTimer>
#include <QPointer>
#include <functional>

namespace {
class ContractDialog final : public QDialog {
public:
    ContractDialog(QWidget *parent, bool mayCancel, QVariant cancelValue)
        : QDialog(parent), m_mayCancel(mayCancel), m_cancelValue(cancelValue) {}
    std::function<void(const QVariant &)> send;
    void retire() { m_retired = true; QDialog::reject(); }
    void reject() override {
        if (m_retired) { QDialog::reject(); return; }
        if (!m_mayCancel) return;
        if (send) send(m_cancelValue);
    }
private:
    bool m_mayCancel;
    bool m_retired = false;
    QVariant m_cancelValue;
};

struct FieldControl {
    QString key;
    QString type;
    QWidget *control = nullptr;
    QListWidget *ordered = nullptr;
};
}

bool presentControllerCustomInteraction(RoomScene *scene, const QVariantMap &parameters, QString *error)
{
    const QJsonObject descriptor = QJsonObject::fromVariantMap(parameters.value("controller_ui").toMap());
    if (descriptor.isEmpty()) return false;
    if (!ControllerInteractionContract::validateDescriptor(descriptor, error)) return false;
    if (!ClientInstance || !ClientInstance->interactionCore()->hasActiveRequest(InteractionType::QmlInteract)) {
        if (error) *error = QStringLiteral("Custom interaction is no longer active.");
        return false;
    }
    const auto *core = ClientInstance->interactionCore();
    const quint64 requestId = core->activeRequestId();
    const quint64 generation = ClientInstance->liveSession() ? ClientInstance->liveSession()->generation() : 0;
    const bool canCancel = descriptor.value("can_cancel").toBool() && core->activeRequest().cancelable;
    auto *dialog = new ContractDialog(scene->mainWindow(), canCancel, descriptor.value("cancel_value").toVariant());
    dialog->setObjectName("controllerCustomInteraction");
    dialog->setWindowTitle(descriptor.value("title").toString(QObject::tr("Custom interaction")));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(dialog);
    auto *scroll = new QScrollArea(dialog);
    scroll->setWidgetResizable(true);
    auto *content = new QWidget(scroll);
    auto *form = new QFormLayout(content);
    scroll->setWidget(content); layout->addWidget(scroll);
    QList<FieldControl> controls;
    for (const auto &value : descriptor.value("fields").toArray()) {
        const auto field = value.toObject();
        FieldControl entry{field.value("key").toString(), field.value("type").toString()};
        const QVariant initial = field.value("default").toVariant();
        const QString label = field.value("label").toString(entry.key);
        if (entry.type == QLatin1String("choice")) {
            auto *combo = new QComboBox(content);
            combo->addItem(QObject::tr("Choose..."), QVariant());
            for (const auto &optionValue : field.value("options").toArray()) {
                const auto option = optionValue.toObject();
                combo->addItem(option.value("label").toString(option.value("value").toString()), option.value("value").toString());
            }
            if (initial.isValid()) combo->setCurrentIndex(combo->findData(initial));
            entry.control = combo;
        } else if (entry.type == QLatin1String("boolean")) {
            auto *check = new QCheckBox(QObject::tr("Yes"), content);
            check->setChecked(initial.toBool()); entry.control = check;
        } else if (entry.type == QLatin1String("integer")) {
            auto *spin = new QSpinBox(content);
            spin->setRange(field.value("minimum").toInt(0), field.value("maximum").toInt(100));
            spin->setValue(initial.isValid() ? initial.toInt() : spin->minimum()); entry.control = spin;
        } else if (entry.type == QLatin1String("text")) {
            auto *text = new QLineEdit(initial.toString(), content);
            text->setMaxLength(field.value("max_length").toInt(512)); entry.control = text;
        } else {
            auto *group = new QWidget(content);
            auto *groupLayout = new QVBoxLayout(group);
            auto *choices = new QListWidget(group);
            auto *order = new QListWidget(group);
            choices->setObjectName(entry.key + "Options");
            order->setObjectName(entry.key + "SelectionOrder");
            choices->setAccessibleName(label);
            order->setAccessibleName(QObject::tr("Selection order"));
            for (const auto &optionValue : field.value("options").toArray()) {
                const auto option = optionValue.toObject();
                auto *item = new QListWidgetItem(option.value("label").toString(option.value("value").toString()), choices);
                item->setData(Qt::UserRole, option.value("value").toString());
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Unchecked);
            }
            QObject::connect(choices, &QListWidget::itemChanged, dialog, [order](QListWidgetItem *item) {
                const QString id = item->data(Qt::UserRole).toString();
                int index = -1;
                for (int i = 0; i < order->count(); ++i) if (order->item(i)->data(Qt::UserRole).toString() == id) index = i;
                if (item->checkState() == Qt::Checked && index < 0) {
                    auto *selected = new QListWidgetItem(item->text(), order);
                    selected->setData(Qt::UserRole, id);
                } else if (item->checkState() != Qt::Checked && index >= 0) delete order->takeItem(index);
            });
            groupLayout->addWidget(choices); groupLayout->addWidget(order);
            auto *commands = new QHBoxLayout;
            for (bool earlier : {true, false}) {
                auto *move = new QPushButton(earlier ? QObject::tr("Earlier") : QObject::tr("Later"), group);
                move->setObjectName(entry.key + (earlier ? "Earlier" : "Later"));
                move->setAutoDefault(false); commands->addWidget(move);
                QObject::connect(move, &QPushButton::clicked, dialog, [order, earlier]() {
                    const int from = order->currentRow(), to = from + (earlier ? -1 : 1);
                    if (from >= 0 && to >= 0 && to < order->count()) {
                        order->insertItem(to, order->takeItem(from)); order->setCurrentRow(to);
                    }
                });
            }
            groupLayout->addLayout(commands);
            for (const auto &selected : field.value("default").toArray())
                for (int i = 0; i < choices->count(); ++i)
                    if (choices->item(i)->data(Qt::UserRole) == selected.toVariant()) choices->item(i)->setCheckState(Qt::Checked);
            entry.control = choices; entry.ordered = order;
            form->addRow(label, group); controls << entry;
            continue;
        }
        entry.control->setObjectName(entry.key);
        entry.control->setAccessibleName(label);
        form->addRow(label, entry.control); controls << entry;
    }
    auto *reason = new QLabel(dialog);
    reason->setTextFormat(Qt::PlainText); reason->setWordWrap(true); layout->addWidget(reason);
    auto *buttons = new QHBoxLayout;
    auto *confirm = new QPushButton(QObject::tr("Confirm"), dialog);
    confirm->setObjectName("controllerCustomConfirm"); confirm->setAutoDefault(false);
    buttons->addWidget(confirm);
    if (canCancel) {
        auto *cancel = new QPushButton(QObject::tr("Cancel"), dialog);
        cancel->setObjectName("controllerCustomCancel"); cancel->setAutoDefault(false);
        QObject::connect(cancel, &QPushButton::clicked, dialog, &ContractDialog::reject); buttons->addWidget(cancel);
    }
    layout->addLayout(buttons);
    const QPointer<Client> client = ClientInstance;
    const QPointer<ContractDialog> guarded = dialog;
    dialog->send = [client, guarded, requestId, generation, descriptor, reason](const QVariant &answer) {
        if (!client || !guarded || !client->interactionCore()->hasActiveRequest(InteractionType::QmlInteract)
            || client->interactionCore()->activeRequestId() != requestId
            || !client->liveSession() || client->liveSession()->generation() != generation) return;
        QString error;
        if (!ControllerInteractionContract::validateResponse(descriptor, answer, &error)) { reason->setText(error); return; }
        client->replyQml(answer); // Native Core/session/encoder path; no direct socket reply.
        if (guarded && (!client || !client->interactionCore()->hasActiveRequest()
                        || client->interactionCore()->activeRequestId() != requestId)) guarded->retire();
    };
    QObject::connect(confirm, &QPushButton::clicked, dialog, [dialog, controls]() {
        QVariantMap answer;
        for (const auto &entry : controls) {
            QVariant value;
            if (auto *combo = qobject_cast<QComboBox *>(entry.control)) value = combo->currentData();
            else if (auto *check = qobject_cast<QCheckBox *>(entry.control)) value = check->isChecked();
            else if (auto *spin = qobject_cast<QSpinBox *>(entry.control)) value = spin->value();
            else if (auto *text = qobject_cast<QLineEdit *>(entry.control)) value = text->text();
            else if (entry.ordered) {
                QStringList selected;
                for (int i = 0; i < entry.ordered->count(); ++i) selected << entry.ordered->item(i)->data(Qt::UserRole).toString();
                value = selected;
            }
            answer.insert(entry.key, value);
        }
        if (dialog->send) dialog->send(answer);
    });
    auto *watch = new QTimer(dialog);
    watch->setInterval(100);
    QObject::connect(watch, &QTimer::timeout, dialog, [client, guarded, requestId, generation]() {
        if (!guarded) return;
        if (!client || !client->interactionCore()->hasActiveRequest(InteractionType::QmlInteract)
            || client->interactionCore()->activeRequestId() != requestId || !client->liveSession()
            || client->liveSession()->generation() != generation) guarded->retire();
    });
    watch->start(); dialog->resize(560, 640); dialog->open();
    return true;
}

void showControllerCoverageFailure(RoomScene *scene, const QString &reason)
{
    scene->mainWindow()->setProperty("controllerUnsupported", reason);
    auto *dialog = new QDialog(scene->mainWindow());
    dialog->setObjectName("controllerUnsupportedInteraction");
    dialog->setProperty("controllerLocalDialog", true);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(dialog);
    auto *text = new QLabel(QObject::tr("This custom interaction has no validated controller contract.\n%1\nThe game request is still pending; closing this notice does not answer it.").arg(reason), dialog);
    text->setTextFormat(Qt::PlainText); text->setWordWrap(true); layout->addWidget(text);
    auto *close = new QPushButton(QObject::tr("Close notice"), dialog);
    QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::close); layout->addWidget(close); dialog->open();
}
