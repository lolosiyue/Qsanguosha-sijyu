#include "package-manager-dialog.h"
#include "runtime-paths.h"

#include "../core/package-store.h"

#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

PackageManagerDialog::PackageManagerDialog(const QString &runtimeRoot, const QString &userDataRoot, QWidget *parent)
    : QDialog(parent), m_store(new PackageStore(runtimeRoot, userDataRoot))
{
    setWindowTitle(tr("Package manager"));
    resize(640, 420);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Package changes take effect after restarting the game."), this));
    m_packages = new QListWidget(this);
    layout->addWidget(m_packages, 1);
    auto *buttons = new QHBoxLayout;
    m_importDirectory = new QPushButton(tr("Install folder…"), this);
    m_importArchive = new QPushButton(tr("Install ZIP…"), this);
    m_remove = new QPushButton(tr("Remove"), this);
    m_rollback = new QPushButton(tr("Restore previous"), this);
    auto *close = new QPushButton(tr("Close"), this);
    buttons->addWidget(m_importDirectory);
    buttons->addWidget(m_importArchive);
    buttons->addWidget(m_remove);
    buttons->addWidget(m_rollback);
    buttons->addStretch();
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_packages, &QListWidget::currentRowChanged, this, [this] {
        const QString id = selectedId();
        m_remove->setEnabled(!id.isEmpty() && id != QStringLiteral("core"));
        m_rollback->setEnabled(!id.isEmpty() && id != QStringLiteral("core"));
    });
    connect(m_importDirectory, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, tr("Select package folder"));
        if (path.isEmpty()) return;
        runOperation(tr("Installing package…"), [store = m_store, path](QString *error) {
            return store->stageDirectory(path, error);
        });
    });
    connect(m_importArchive, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Select package ZIP"), QString(), tr("ZIP archives (*.zip)"));
        if (path.isEmpty()) return;
        runOperation(tr("Installing package…"), [store = m_store, path](QString *error) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                if (error) *error = QObject::tr("Cannot open the selected archive.");
                return false;
            }
            return store->stageArchive(file, error);
        });
    });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const QString id = selectedId();
        if (id.isEmpty()) return;
        runOperation(tr("Scheduling package removal…"), [store = m_store, id](QString *error) {
            return store->remove(id, error);
        });
    });
    connect(m_rollback, &QPushButton::clicked, this, [this] {
        const QString id = selectedId();
        if (id.isEmpty()) return;
        runOperation(tr("Scheduling restore…"), [store = m_store, id](QString *error) {
            return store->rollback(id, error);
        });
    });
    refresh();
}

void PackageManagerDialog::openManager(const QString &runtimeRoot, const QString &userDataRoot, QWidget *parent)
{
    PackageManagerDialog dialog(runtimeRoot, userDataRoot, parent);
    dialog.exec();
}

PackageManagerDialog::~PackageManagerDialog()
{
    if (m_worker) {
        m_worker->wait();
        delete m_worker;
        m_worker = nullptr;
    }
}

QString PackageManagerDialog::selectedId() const
{
    const QListWidgetItem *item = m_packages->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void PackageManagerDialog::refresh()
{
    m_packages->clear();
    const QVariantList rows = m_store->overview();
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        const QString id = row.value(QStringLiteral("id")).toString();
        QString label;
        if (row.value(QStringLiteral("storeError")).toBool())
            label = tr("Pending package change discarded: %1").arg(row.value(QStringLiteral("version")).toString());
        else {
            label = QStringLiteral("%1 — %2").arg(id, row.value(QStringLiteral("version")).toString());
            if (row.value(QStringLiteral("pendingRemoval")).toBool()) label += tr(" (removal pending)");
            else if (row.value(QStringLiteral("pending")).toBool())
                label += tr(" (update pending: %1)").arg(row.value(QStringLiteral("pendingVersion")).toString());
            if (row.value(QStringLiteral("previousAvailable")).toBool()) label += tr(" · previous available");
        }
        auto *item = new QListWidgetItem(label, m_packages);
        item->setData(Qt::UserRole, id);
    }
    if (!m_packages->count()) m_packages->addItem(tr("No installed packages"));
    for (const QString &error : QSanRuntimePaths::describe().value(QStringLiteral("package_asset_errors")).toStringList())
        m_packages->addItem(tr("Missing package asset: %1").arg(error));
    m_remove->setEnabled(false);
    m_rollback->setEnabled(false);
}

void PackageManagerDialog::runOperation(const QString &workingText, const std::function<bool(QString *)> &operation)
{
    setEnabled(false);
    setWindowTitle(workingText);
    const QPointer<PackageManagerDialog> dialog(this);
    QCoreApplication *application = QCoreApplication::instance();
    QThread *thread = QThread::create([dialog, application, workingText, operation] {
        QString error;
        const bool succeeded = operation(&error);
        QMetaObject::invokeMethod(application, [dialog, succeeded, error, workingText] {
            if (!dialog) return;
            QThread *worker = dialog->m_worker;
            dialog->m_worker = nullptr;
            if (worker) {
                worker->wait();
                delete worker;
            }
            dialog->setEnabled(true);
            dialog->setWindowTitle(QObject::tr("Package manager"));
            if (!succeeded)
                QMessageBox::warning(dialog, QObject::tr("Package operation failed"), error.isEmpty()
                                     ? QObject::tr("The package operation failed.") : error);
            else {
                dialog->refresh();
                QMessageBox::information(dialog, QObject::tr("Restart required"),
                                         QObject::tr("The change is staged and will take effect after restarting the game."));
            }
            Q_UNUSED(workingText);
        }, Qt::QueuedConnection);
    });
    m_worker = thread;
    thread->start();
}
