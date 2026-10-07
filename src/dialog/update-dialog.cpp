#include "update-dialog.h"
#include "update-transfer.h"
#include "package-store.h"
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QUrl releasesUrl() {
    return QUrl(QStringLiteral("https://api.github.com/repos/lolosiyue/Qsanguosha-sijyu/releases?per_page=100"));
}
}
UpdateDialog::UpdateDialog(const QString &runtimeRoot, const QString &userDataRoot,
                         const QString &version, const QString &commit, bool cleanBuild,
                         QSettings *settings, QWidget *parent, QNetworkAccessManager *network)
    : QDialog(parent), m_runtimeRoot(runtimeRoot), m_userDataRoot(userDataRoot), m_version(version),
      m_commit(commit), m_cleanBuild(cleanBuild), m_settings(settings), m_transfer(new UpdateTransfer(this, network)) {
    setWindowTitle(tr("Check for updates"));
    setObjectName(QStringLiteral("checkUpdatesDialog"));
    setProperty("controllerLocalDialog", true);
    resize(760, 560);
    auto *layout = new QVBoxLayout(this);
    auto *build = new QLabel(tr("Current game: %1 · Commit: %2 · Platform: %3")
        .arg(version, commit.isEmpty() ? tr("Unknown") : commit.left(12), QSanUpdates::platform()), this);
    build->setTextFormat(Qt::PlainText); build->setWordWrap(true); layout->addWidget(build);
    auto *hint = new QLabel(tr("Game downloads require manual installation after exiting. Material packages are applied on the next launch; active files remain in use until then."), this);
    hint->setWordWrap(true); layout->addWidget(hint);
    layout->addWidget(new QLabel(tr("Public material manifest URL (HTTPS):"), this));
    m_url = new QLineEdit(this); m_url->setObjectName(QStringLiteral("materialManifestUrl"));
    m_url->setAccessibleName(tr("Public material manifest URL (HTTPS):"));
    if (settings) m_url->setText(settings->value(QStringLiteral("Updates/MaterialManifestUrl")).toString());
    layout->addWidget(m_url);
    m_prerelease = new QCheckBox(tr("Include prereleases"), this);
    if (settings) m_prerelease->setChecked(settings->value(QStringLiteral("Updates/IncludePrerelease"), false).toBool());
    layout->addWidget(m_prerelease);
    m_status = new QLabel(this); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText);
    m_status->setObjectName(QStringLiteral("updateStatus"));
    layout->addWidget(m_status);
    auto *contents = new QHBoxLayout;
    m_list = new QListWidget(this); m_list->setAccessibleName(tr("Available game and material versions"));
    m_details = new QTextEdit(this); m_details->setReadOnly(true); m_details->setAccessibleName(tr("Update details"));
    m_details->setObjectName(QStringLiteral("updateDetails"));
    contents->addWidget(m_list, 1); contents->addWidget(m_details, 1); layout->addLayout(contents, 1);
    m_progress = new QProgressBar(this); m_progress->setRange(0, 1000); m_progress->hide(); layout->addWidget(m_progress);
    auto *buttons = new QHBoxLayout;
    m_check = new QPushButton(tr("Check again"), this);
    m_download = new QPushButton(tr("Download selected"), this);
    m_page = new QPushButton(tr("Open release page"), this);
    m_cancelButton = new QPushButton(tr("Cancel operation"), this);
    m_close = new QPushButton(tr("Close"), this);
    m_check->setObjectName(QStringLiteral("checkUpdatesAgain"));
    m_download->setObjectName(QStringLiteral("downloadSelectedUpdate"));
    m_cancelButton->setObjectName(QStringLiteral("cancelUpdateOperation"));
    for (auto *button : {m_check, m_download, m_page, m_cancelButton, m_close}) {
        button->setAutoDefault(false); buttons->addWidget(button);
    }
    layout->addLayout(buttons);
    connect(m_check, &QPushButton::clicked, this, &UpdateDialog::check);
    connect(m_download, &QPushButton::clicked, this, &UpdateDialog::download);
    connect(m_page, &QPushButton::clicked, this, [this] {
        const auto *e = entry();
        QDesktopServices::openUrl(e && QSanUpdates::safeHttps(e->releasePage) ? e->releasePage
            : QUrl(QStringLiteral("https://github.com/lolosiyue/Qsanguosha-sijyu/releases")));
    });
    connect(m_cancelButton, &QPushButton::clicked, this, &UpdateDialog::cancel);
    connect(m_close, &QPushButton::clicked, this, &UpdateDialog::reject);
    connect(m_list, &QListWidget::currentRowChanged, this, &UpdateDialog::selected);
    connect(m_transfer, &UpdateTransfer::catalogReady, this, &UpdateDialog::received);
    connect(m_transfer, &UpdateTransfer::downloadReady, this, &UpdateDialog::downloaded);
    connect(m_transfer, &UpdateTransfer::failed, this, &UpdateDialog::failed);
    connect(m_transfer, &UpdateTransfer::progress, this, [this](qint64 bytes, qint64 total) {
        m_progress->setValue(total ? int(bytes * 1000 / total) : 0);
        m_status->setText(tr("Downloading: %1 / %2 MiB. You can cancel and resume later.")
            .arg(bytes / (1024 * 1024)).arg(total / (1024 * 1024)));
    });
    setOperation(Idle);
    // This constructor is only called by the existing homepage button.
    QTimer::singleShot(0, this, &UpdateDialog::check);
}
UpdateDialog::~UpdateDialog() {
    m_cancel = true;
    if (m_worker) { m_worker->wait(); delete m_worker; }
}
void UpdateDialog::reject() {
    if (m_operation != Idle) { m_closeAfterCancel = true; cancel(); return; }
    QDialog::reject();
}
void UpdateDialog::cancel() {
    m_cancel = true;
    if (m_operation == Stage) {
        m_cancel = true;
        m_status->setText(tr("Cancelling extraction. If staging has already begun, its atomic operation will finish safely."));
        return;
    }
    m_transfer->cancel();
}
void UpdateDialog::setOperation(Operation operation) {
    m_operation = operation;
    const bool idle = operation == Idle;
    m_check->setEnabled(idle); m_url->setEnabled(idle); m_prerelease->setEnabled(idle);
    m_list->setEnabled(idle); m_cancelButton->setEnabled(!idle);
    m_progress->setVisible(operation == Download || operation == Stage);
    m_page->setEnabled(idle);
    m_download->setEnabled(idle && entry() && entry()->downloadable
        && (entry()->material || !m_newerCommits.contains(entry()->version) || m_newerCommits.value(entry()->version)));
    if (!idle) m_cancelButton->setFocus(Qt::OtherFocusReason);
}
const QSanUpdates::Entry *UpdateDialog::entry() const {
    const int row = m_list->currentRow();
    return row >= 0 && row < m_entries.size() ? &m_entries[row] : nullptr;
}
void UpdateDialog::check() {
    if (m_operation != Idle) return;
    const QString raw = m_url->text().trimmed();
    if (!raw.isEmpty() && !QSanUpdates::safeHttps(QUrl(raw, QUrl::StrictMode))) {
        m_status->setText(tr("Enter a public HTTPS manifest URL without credentials or a fragment.")); return;
    }
    if (m_settings) {
        m_settings->setValue(QStringLiteral("Updates/MaterialManifestUrl"), raw);
        m_settings->setValue(QStringLiteral("Updates/IncludePrerelease"), m_prerelease->isChecked());
    }
    m_cancel = false;
    m_entries.clear(); m_newerCommits.clear(); m_messages.clear(); m_list->clear(); m_details->clear();
    m_status->setText(tr("Checking GitHub Releases…"));
    setOperation(Releases); m_transfer->catalog(releasesUrl());
}
void UpdateDialog::checkMaterials() {
    if (m_url->text().trimmed().isEmpty()) {
        m_messages += tr("Material updates are not configured. Supply the public R2 manifest URL.") + '\n';
        refresh(); return;
    }
    m_status->setText(tr("Checking material catalog…"));
    setOperation(Materials); m_transfer->catalog(QUrl(m_url->text().trimmed(), QUrl::StrictMode));
}
void UpdateDialog::received(const QByteArray &bytes) {
    if (m_operation == Compare) {
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(bytes, &parse);
        // Ahead proves the release tag is a descendant of this exact build.
        // Diverged/behind/identical/unknown builds never become downloads.
        const bool ahead = parse.error == QJsonParseError::NoError && doc.isObject()
            && doc.object().value(QStringLiteral("status")).toString() == QStringLiteral("ahead")
            && doc.object().value(QStringLiteral("ahead_by")).toDouble() > 0;
        m_newerCommits.insert(m_chosen.version, ahead);
        setOperation(Idle);
        if (ahead) {
            m_status->setText(tr("Release ancestry verified. Downloading the selected build…"));
            setOperation(Download); m_transfer->download(m_chosen, m_userDataRoot);
        } else {
            m_status->setText(tr("The release is not newer than this build, or its ancestry could not be verified. Use the release page to review it manually."));
            selected();
        }
        return;
    }
    if (m_operation == Releases) {
        const auto catalog = QSanUpdates::releases(bytes, m_version, QSanUpdates::platform(), m_prerelease->isChecked());
        if (!catalog.valid()) m_messages += catalog.error + '\n';
        else {
            m_entries.append(catalog.entries);
            m_messages += catalog.entries.isEmpty() ? tr("No comparable game release was found in the latest 100 releases.") + '\n'
                : tr("Latest comparable game release: %1").arg(catalog.entries.first().version) + '\n';
        }
        checkMaterials(); return;
    }
    if (m_operation == Materials) {
        QMap<QString, QString> installed, pending;
        PackageStore store(m_runtimeRoot, m_userDataRoot);
        // Startup already verified the active catalog; don't hash 3–4 GB for a button click.
        for (const auto &p : QSanPackages::activeCatalog().packages) installed.insert(p.id, p.version);
        for (const QString &id : store.pendingIds()) pending.insert(id, QString());
        const auto catalog = QSanUpdates::materials(bytes, m_version, installed, pending);
        if (!catalog.valid()) m_messages += catalog.error + '\n';
        else {
            m_entries.append(catalog.entries);
            m_messages += tr("Material packages checked: %1").arg(catalog.entries.size()) + '\n';
        }
        refresh();
    }
}
void UpdateDialog::failed(const QString &error) {
    const Operation previous = m_operation;
    m_messages += error + '\n';
    setOperation(Idle);
    m_status->setText(m_messages.trimmed());
    if (m_closeAfterCancel) { QDialog::reject(); return; }
    // An offline game catalog must not hide an independently available R2 catalog.
    if (previous == Releases && !m_cancel) checkMaterials();
    else if (previous == Materials) refresh();
}
void UpdateDialog::refresh() {
    m_list->clear();
    for (auto &e : m_entries) {
        if (!e.material && (!m_cleanBuild || !QRegularExpression(QStringLiteral("^[0-9a-f]{40}$")).match(m_commit).hasMatch())) {
            e.downloadable = false;
            e.reason = tr("This build has local changes or unknown commit identity. Review releases manually to avoid replacing a newer development build.");
        }
        m_list->addItem(tr("%1 · Current: %2 · Latest: %3")
            .arg(e.name, e.currentVersion.isEmpty() ? tr("Not installed") : e.currentVersion, e.version));
    }
    setOperation(Idle); m_status->setText(m_messages.trimmed());
    if (!m_entries.isEmpty()) m_list->setCurrentRow(0);
    selected();
}
void UpdateDialog::selected() {
    const auto *e = entry();
    const bool newer = !e || e->material || !m_newerCommits.contains(e->version) || m_newerCommits.value(e->version);
    m_download->setEnabled(m_operation == Idle && e && e->downloadable && newer);
    m_page->setEnabled(m_operation == Idle);
    if (!e) { m_details->clear(); return; }
    QString text = tr("%1\nCurrent: %2\nLatest: %3\nDownload size: %4 MiB\n\nChanges:\n%5")
        .arg(e->name, e->currentVersion.isEmpty() ? tr("Not installed") : e->currentVersion, e->version)
        .arg(e->size / (1024 * 1024)).arg(e->notes.isEmpty() ? tr("No release notes supplied.") : e->notes);
    if (!e->reason.isEmpty()) text += QStringLiteral("\n\n") + e->reason;
    if (!e->material && e->downloadable) text += QStringLiteral("\n\n") + tr("Before downloading, GitHub must confirm that this release is newer than your build. Install it manually after exiting; platform labels do not guarantee OS or driver compatibility.");
    m_details->setPlainText(text);
}
void UpdateDialog::download() {
    const auto *e = entry();
    if (m_operation != Idle || !e || !e->downloadable) return;
    m_chosen = *e;
    m_cancel = false;
    if (!e->material) {
        if (m_newerCommits.contains(e->version) && !m_newerCommits.value(e->version)) return;
        if (!m_newerCommits.contains(e->version)) {
            QUrl compare(QStringLiteral("https://api.github.com"));
            compare.setPath(QStringLiteral("/repos/lolosiyue/Qsanguosha-sijyu/compare/") + m_commit + QStringLiteral("...") + e->version);
            setOperation(Compare); m_status->setText(tr("Verifying that the selected release is newer than this build…"));
            m_transfer->catalog(compare); return;
        }
    }
    setOperation(Download); m_progress->setValue(0);
    m_status->setText(tr("Downloading the selected update…"));
    m_transfer->download(m_chosen, m_userDataRoot);
}
void UpdateDialog::downloaded(const QString &path) {
    if (!m_chosen.material) {
        setOperation(Idle);
        m_status->setText(tr("Verified download saved to %1. Exit the game, then install or extract it into a separate location and restart. The updater will not run an installer or replace the running executable.").arg(path));
        m_details->setPlainText(m_status->text());
        auto *box = new QMessageBox(QMessageBox::Information, tr("Game download ready"), m_status->text(), QMessageBox::Close, this);
        box->setProperty("controllerLocalDialog", true);
        auto *folder = box->addButton(tr("Open download folder"), QMessageBox::ActionRole);
        connect(folder, &QPushButton::clicked, box, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())); });
        box->exec(); delete box; return;
    }
    setOperation(Stage); m_progress->setRange(0, 0); m_cancel = false;
    m_status->setText(tr("Validating and staging the material package for the next launch…"));
    const QPointer<UpdateDialog> guard(this);
    const QString runtime = m_runtimeRoot, user = m_userDataRoot, id = m_chosen.id, version = m_chosen.version;
    m_worker = QThread::create([this, guard, path, runtime, user, id, version] {
        QString error;
        QFile file(path);
        PackageStore store(runtime, user);
        const bool ok = file.open(QIODevice::ReadOnly) && store.stageArchive(file, &error, &m_cancel, id, version);
        QMetaObject::invokeMethod(this, [guard, ok, error] {
            if (!guard) return;
            auto *self = guard.data();
            self->m_worker->wait(); delete self->m_worker; self->m_worker = nullptr;
            self->m_progress->setRange(0, 1000);
            self->setOperation(Idle);
            if (ok) {
                for (auto &e : self->m_entries) if (e.material && e.id == self->m_chosen.id) {
                    e.downloadable = false; e.reason = tr("A package change is already pending. Restart or manage packages first.");
                }
                self->selected();
                self->m_status->setText(tr("The material update is staged. Restart the game to apply it. Use Package manager to restore the previous version if needed."));
            } else {
                self->m_status->setText(tr("Material staging failed; active packages were preserved.\n%1").arg(error.isEmpty() ? tr("Cannot read the downloaded package.") : error));
            }
            if (self->m_closeAfterCancel) self->QDialog::reject();
        }, Qt::QueuedConnection);
    });
    m_worker->start();
}
