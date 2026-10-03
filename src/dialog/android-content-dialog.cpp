#include "android-content-dialog.h"
#include "android-content-store.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScroller>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <memory>

namespace {
AndroidContentStore *contentStore = nullptr;

class StartupWaitDialog final : public QDialog
{
public:
    void reject() override {} // Back/Escape cannot destroy an active store worker.
protected:
    void closeEvent(QCloseEvent *event) override { event->ignore(); }
};

// Image assets may not exist yet, so both pages are styled from code only.
// Palette follows the desktop local-room loading page.
const QString pageStyleSheet = QStringLiteral(
    "QDialog#androidStartupPage, QDialog#androidContentDialog {"
    " background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #13253A, stop:1 #060D17);"
    "}"
    "QFrame#androidContentPanel {"
    " background-color: rgba(8, 18, 31, 218);"
    " border: 1px solid rgba(226, 190, 119, 108);"
    " border-radius: 18px;"
    "}"
    "QLabel#androidContentEyebrow, QLabel#androidContentSection {"
    " color: #E6C179; font-size: 13px; font-weight: 700;"
    "}"
    "QLabel#androidContentTitle { color: #FFF8E9; font-size: 26px; font-weight: 700; }"
    "QLabel#androidContentStatus { color: #F7FAFE; font-size: 15px; font-weight: 600; }"
    "QDialog#androidContentDialog QLabel#androidContentStatus {"
    " padding: 10px 14px; border-left: 3px solid #D9AD5C; border-radius: 6px;"
    " background: rgba(217, 173, 92, 32);"
    "}"
    "QLabel#androidContentHint { color: #B9C9DA; font-size: 13px; }"
    "QFrame#androidContentDivider { background: rgba(226, 190, 119, 72); border: none; }"
    "QProgressBar#androidStartupProgress, QProgressBar#androidContentProgress {"
    " min-height: 8px; max-height: 8px; border: none; border-radius: 4px;"
    " background: rgba(226, 236, 247, 35);"
    "}"
    "QProgressBar#androidContentProgress::chunk { border-radius: 4px; background: #D9AD5C; }"
    // A busy bar repeats the chunk; spaced segments make the motion visible.
    "QProgressBar#androidStartupProgress::chunk {"
    " width: 14px; margin-right: 6px; border-radius: 4px; background: #D9AD5C;"
    "}"
    "QListWidget#androidContentPackages {"
    " color: #F7FAFE; font-size: 14px; outline: none;"
    " background: rgba(8, 18, 31, 190);"
    " border: 1px solid rgba(226, 190, 119, 70); border-radius: 10px; padding: 4px;"
    "}"
    "QListWidget#androidContentPackages::item { padding: 8px 10px; border-radius: 6px; }"
    "QListWidget#androidContentPackages::item:selected {"
    " color: #FFF8E9; background: rgba(217, 173, 92, 90);"
    "}"
    "QPushButton[androidRole] {"
    " min-height: 46px; padding: 0 16px; font-size: 14px; border-radius: 8px;"
    " color: #FFF8E9; border: 1px solid rgba(226, 190, 119, 110);"
    " background: rgba(16, 30, 48, 230);"
    "}"
    "QPushButton[androidRole]:pressed { background: rgba(217, 173, 92, 95); }"
    "QPushButton[androidRole=\"primary\"] {"
    " color: #1B1206; font-weight: 700; border: none;"
    " background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #EBC77F, stop:1 #C9964A);"
    "}"
    "QPushButton[androidRole=\"primary\"]:pressed { background: #B9853C; }"
    "QPushButton[androidRole=\"danger\"] { color: #FFD9D2; border-color: rgba(222, 110, 92, 150); }"
    "QPushButton[androidRole=\"danger\"]:pressed { background: rgba(222, 110, 92, 90); }"
    "QPushButton[androidRole]:disabled {"
    " color: rgba(247, 250, 254, 90); border-color: rgba(226, 190, 119, 40);"
    " background: rgba(16, 30, 48, 140);"
    "}");

QLabel *styledLabel(const QString &text, const char *name, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QLatin1String(name));
    label->setWordWrap(true);
    return label;
}

QString devicePath(const QUrl &url)
{
    // Qt's Android file engine opens SAF content URIs without broad storage access.
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}
}

void AndroidContentDialog::configure(AndroidContentStore *store) { contentStore = store; }

bool AndroidContentDialog::prepareStartup(QString *error)
{
    if (!contentStore) {
        if (error) *error = tr("Content store is not configured.");
        return false;
    }
    StartupWaitDialog dialog;
    dialog.setWindowTitle(tr("QSanguosha · Preparing to start"));
    dialog.setAttribute(Qt::WA_QuitOnClose, false);
    dialog.setWindowFlag(Qt::WindowCloseButtonHint, false);
    dialog.setObjectName(QStringLiteral("androidStartupPage"));
    // Fill the screen like the content dialog instead of a small centered box.
    dialog.setProperty("androidContentDialogOwnScroll", true);
    dialog.setStyleSheet(pageStyleSheet);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->addStretch(1);
    auto *panel = new QFrame(&dialog);
    panel->setObjectName(QStringLiteral("androidContentPanel"));
    panel->setMinimumWidth(500);
    panel->setMaximumWidth(560);
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(36, 26, 36, 28);
    panelLayout->setSpacing(10);
    auto *eyebrow = styledLabel(tr("Preparing to start"), "androidContentEyebrow", panel);
    eyebrow->setAlignment(Qt::AlignCenter);
    panelLayout->addWidget(eyebrow);
    auto *title = styledLabel(tr("QSanguosha"), "androidContentTitle", panel);
    title->setAlignment(Qt::AlignCenter);
    panelLayout->addWidget(title);
    auto *divider = new QFrame(panel);
    divider->setObjectName(QStringLiteral("androidContentDivider"));
    divider->setFixedHeight(1);
    panelLayout->addWidget(divider);
    panelLayout->addSpacing(4);
    auto *label = styledLabel(tr("Checking resources and applying pending content…"),
        "androidContentStatus", panel);
    label->setAlignment(Qt::AlignCenter);
    panelLayout->addWidget(label);
    auto *progress = new QProgressBar(panel);
    progress->setObjectName(QStringLiteral("androidStartupProgress"));
    progress->setTextVisible(false);
    progress->setRange(0, 0);
    panelLayout->addWidget(progress);
    auto *hint = styledLabel(tr("The full media resources contain many files. The first launch may take several minutes; please wait."), "androidContentHint", panel);
    hint->setAlignment(Qt::AlignCenter);
    panelLayout->addWidget(hint);
    layout->addWidget(panel);
    layout->setAlignment(panel, Qt::AlignHCenter);
    layout->addStretch(1);

    bool ok = false;
    bool finished = false;
    QString workerError;
    AndroidContentStore *store = contentStore;
    std::unique_ptr<QThread> worker(QThread::create([store, &ok, &workerError] {
        ok = store->prepareStartup(&workerError);
    }));
    // The nested GUI loop keeps painting and handles activity changes while
    // the worker exclusively owns the store. Backgrounding never terminates it.
    const auto continueWhenReady = [&] {
        if (finished && QGuiApplication::applicationState() == Qt::ApplicationActive)
            dialog.accept();
    };
    QObject::connect(worker.get(), &QThread::finished, &dialog, [&] {
        worker->wait();
        finished = true;
        label->setText(tr("Resource processing has finished. The result appears once the app returns to the foreground."));
        progress->setRange(0, 1);
        progress->setValue(1);
        continueWhenReady();
    });
    QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged, &dialog,
        [&](Qt::ApplicationState) { continueWhenReady(); });
    worker->start();
    const int result = dialog.exec();
    // Also join if application shutdown exits the nested loop unexpectedly;
    // neither the stack result nor the main-owned store may outlive this call.
    worker->wait();
    if (error) *error = workerError;
    if (result != QDialog::Accepted) {
        if (error && error->isEmpty()) *error = tr("Startup was aborted.");
        return false;
    }
    return ok;
}

bool AndroidContentDialog::prepareForGame()
{
    if (!contentStore) return false;
    if (contentStore->mediaReady() && !contentStore->needsRecovery()) return true;
    AndroidContentDialog dialog(true);
    return dialog.exec() == QDialog::Accepted
        && contentStore->mediaReady() && !contentStore->needsRecovery();
}

void AndroidContentDialog::openManager(QWidget *parent)
{
    if (!contentStore) return;
    AndroidContentDialog dialog(false, parent);
    dialog.exec();
}

AndroidContentDialog::AndroidContentDialog(bool startup, QWidget *parent)
    : QDialog(parent), m_startup(startup)
{
    setWindowTitle(tr("QSanguosha · Resources and extensions"));
    setObjectName(QStringLiteral("androidContentDialog"));
    setProperty("androidContentDialogOwnScroll", true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setStyleSheet(pageStyleSheet);
    auto *outer = new QVBoxLayout(this);
    outer->setSizeConstraint(QLayout::SetNoConstraint);
    outer->setContentsMargins(20, 12, 20, 12);
    outer->setSpacing(10);
    // A landscape phone can be shorter than the combined labels and controls.
    // Scroll the whole body while keeping cancel/close visible during imports.
    auto *bodyScroll = m_bodyScroll = new QScrollArea(this);
    bodyScroll->setWidgetResizable(true);
    bodyScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bodyScroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    bodyScroll->setFrameShape(QFrame::NoFrame);
    bodyScroll->viewport()->setAutoFillBackground(false);
    // Widgets receive synthesized mouse events for unhandled touchscreen input.
    // The mouse recognizer consumes a drag and releases the pressed child outside
    // its bounds, preventing a swipe over an import button from clicking it.
    QScroller::grabGesture(bodyScroll->viewport(), QScroller::LeftMouseButtonGesture);
    bodyScroll->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, false);
    auto *body = new QWidget(bodyScroll);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    bodyScroll->setWidget(body);
    body->setAutoFillBackground(false); // setWidget() turns it on; keep the page gradient.
    outer->addWidget(bodyScroll, 1);
    layout->addWidget(styledLabel(tr("Resources and extensions"), "androidContentTitle", body));
    m_status = styledLabel(QString(), "androidContentStatus", this);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_status);
    auto *notice = styledLabel(tr(
        "Import the full media resources before the first game. Imports and management changes take effect after a restart.\n"
        "Only import Lua you trust: scripts have full Lua access and can read and write any file this app can access. "
        "Keep the app in the foreground while importing; switching away or locking the screen cancels the import."), "androidContentHint", this);
    notice->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(notice);
    layout->addWidget(styledLabel(tr("Installed packages"), "androidContentSection", body));
    m_packages = new QListWidget(this);
    m_packages->setObjectName(QStringLiteral("androidContentPackages"));
    m_packages->setFixedHeight(120);
    QScroller::grabGesture(m_packages->viewport(), QScroller::LeftMouseButtonGesture);
    m_packages->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, false);
    layout->addWidget(m_packages);
    layout->addWidget(styledLabel(tr("Import and manage"), "androidContentSection", body));
    auto *panel = new QWidget(body);
    auto *buttons = new QGridLayout(panel);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    // The activity is landscape-only, so two columns halve the scroll height.
    auto add = [&](const QString &label, const std::function<void()> &fn,
                   const char *role = "secondary") {
        auto *button = new QPushButton(label, panel);
        button->setMinimumHeight(48);
        button->setProperty("androidRole", QLatin1String(role));
        buttons->addWidget(button, int(m_actions.size()) / 2, int(m_actions.size()) % 2);
        m_actions.append(button);
        connect(button, &QPushButton::clicked, this, fn);
        return button;
    };
    add(tr("Import full media ZIP"), [this] { importFile(true, false); }, "primary");
    add(tr("Import extension ZIP"), [this] { importFile(false, false); });
    add(tr("Import modular package ZIP"), [this] { importFile(false, false, true); });
    add(tr("Import single Lua"), [this] { importFile(false, true); });
    add(tr("Enable/disable package"), [this] {
        const QString id = selectedPackage();
        if (id.isEmpty()) return;
        for (const auto &p : contentStore->packages()) if (p.id == id) {
            run([id, enabled = !p.enabled](QString *e) { return contentStore->setPackageEnabled(id, enabled, e); });
            break;
        }
    });
    add(tr("Remove selected package"), [this] {
        const QString id = selectedPackage();
        if (id.isEmpty()) return;
        if (QMessageBox::question(this, tr("Remove extension"),
            tr("Remove \"%1\"? This takes effect after a restart.").arg(id)) == QMessageBox::Yes)
            run([id](QString *e) { return contentStore->removePackage(id, e); });
    }, "danger");
    add(tr("Move selected package up"), [this] { movePackage(-1); });
    add(tr("Move selected package down"), [this] { movePackage(1); });
    add(tr("Export load order"), [this] { exportOrder(); });
    add(tr("Switch back to the previous version"), [this] {
        const bool recovery = m_startup && contentStore->needsRecovery();
        run([recovery](QString *e) { return recovery ? contentStore->recoverPrevious(e) : contentStore->rollback(e); });
    });
    m_recoverButton = add(tr("Disable last import / discard pending changes"), [this] {
        run([](QString *e) { return contentStore->discardPendingAndDisableLastImport(e); });
    });
    layout->addWidget(panel);
    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("androidContentProgress"));
    m_progress->setTextVisible(false);
    m_progress->setRange(0, 100);
    outer->addWidget(m_progress);
    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    m_cancelButton = new QPushButton(tr("Cancel import"), this);
    m_cancelButton->setMinimumHeight(48);
    m_cancelButton->setProperty("androidRole", QStringLiteral("secondary"));
    connect(m_cancelButton, &QPushButton::clicked, this, [this] { m_cancel.store(true); });
    footer->addWidget(m_cancelButton);
    m_continue = new QPushButton(startup ? tr("Enter home") : tr("Done"), this);
    m_continue->setMinimumHeight(48);
    m_continue->setProperty("androidRole", QStringLiteral("primary"));
    connect(m_continue, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(m_continue);
    auto *close = new QPushButton(startup ? tr("Close app") : tr("Back"), this);
    close->setMinimumHeight(48);
    close->setProperty("androidRole", QStringLiteral("secondary"));
    connect(close, &QPushButton::clicked, this, &AndroidContentDialog::reject);
    footer->addWidget(close);
    outer->addLayout(footer);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (m_worker && state != Qt::ApplicationActive) m_cancel.store(true);
    });
    refresh();
}

AndroidContentDialog::~AndroidContentDialog()
{
    // Parent destruction can bypass reject(). Cancel and join before members
    // captured by the worker disappear; queued progress calls are then dropped
    // by QObject destruction. The worker never waits for a GUI callback.
    if (m_worker) {
        m_cancel.store(true);
        disconnect(m_worker, nullptr, this, nullptr);
        m_worker->wait();
        delete m_worker;
        m_worker = nullptr;
    }
}

void AndroidContentDialog::reject()
{
    if (m_worker) {
        m_cancel.store(true);
        m_closeWhenFinished = true;
        return;
    }
    QDialog::reject();
}

QString AndroidContentDialog::selectedPackage() const
{
    auto *item = m_packages->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void AndroidContentDialog::refresh()
{
    const QString selected = selectedPackage();
    m_packages->clear();
    for (const auto &p : contentStore->packages()) {
        const QString version = p.packageVersion.isEmpty() ? QString() : QStringLiteral(" · v%1").arg(p.packageVersion);
        auto *item = new QListWidgetItem(QStringLiteral("%1%2  · %3%4")
            .arg(p.id, version, p.enabled ? tr("Enabled") : tr("Disabled"),
                 p.bundled ? tr(" · Bundled original") : QString()), m_packages);
        item->setData(Qt::UserRole, p.id);
        if (p.id == selected) m_packages->setCurrentItem(item);
    }
    const bool upgradeConflict = contentStore->status().value("upgrade_conflict").toBool();
    m_recoverButton->setText(upgradeConflict
        ? tr("Restore bundled extensions, keep media")
        : tr("Disable last import / discard pending changes"));
    m_status->setText(upgradeConflict
        ? tr("New APK content conflicts with existing extensions. Adjust the packages or restore the bundled extensions; the old content stays in the previous version.\n%1")
            .arg(contentStore->status().value("recovery_error").toString())
        : contentStore->needsRecovery()
        ? tr("The last content startup did not finish. Disable the last import or switch back to the previous version.")
        : contentStore->hasPending() ? tr("Changes are fully staged. Close and reopen the app to apply them.")
        : contentStore->mediaReady() ? tr("Full resources are ready.")
        : tr("The full media resources have not been imported; games cannot start yet."));
    for (auto *button : m_actions) button->setEnabled(true);
    m_packages->setEnabled(true);
    m_cancelButton->setEnabled(false);
    m_continue->setEnabled(!m_startup || (contentStore->mediaReady() && !contentStore->needsRecovery()));
}

void AndroidContentDialog::revealStatus()
{
    // Wait for word-wrapped text to update its layout before positioning it.
    // Stop a previous flick so it cannot scroll the result out of view again.
    QScroller::scroller(m_bodyScroll->viewport())->stop();
    QTimer::singleShot(0, this, [this] {
        m_bodyScroll->ensureWidgetVisible(m_status, 0, 8);
    });
}

void AndroidContentDialog::run(const std::function<bool(QString *)> &operation)
{
    if (m_worker) return;
    m_cancel.store(false);
    m_progress->setValue(0);
    for (auto *button : m_actions) button->setEnabled(false);
    m_packages->setEnabled(false);
    m_continue->setEnabled(false);
    m_cancelButton->setEnabled(true);
    m_status->setText(tr("Processing; keep the app in the foreground…"));
    revealStatus();
    struct Result { bool ok = false; QString error; };
    auto result = std::make_shared<Result>();
    m_worker = QThread::create([operation, result] { result->ok = operation(&result->error); });
    connect(m_worker, &QThread::finished, this, [this, result] {
        m_worker->wait();
        m_worker->deleteLater();
        m_worker = nullptr;
        refresh();
        if (!result->ok) {
            qWarning() << "Android content import:" << result->error;
            m_status->setText(result->error.isEmpty()
                ? tr("Operation cancelled; the current version is kept.") : result->error);
        }
        else m_progress->setValue(100);
        revealStatus();
        if (m_closeWhenFinished) QDialog::reject();
    });
    m_worker->start();
}

void AndroidContentDialog::importFile(bool media, bool singleLua, bool modularPackage)
{
    const QUrl url = QFileDialog::getOpenFileUrl(this, tr("Select a file to import"), QUrl(),
        singleLua ? tr("Lua (*.lua);;All files (*)")
                  : tr("ZIP (*.zip);;All files (*)"));
    if (url.isEmpty()) return;
    QString filename = QFileInfo(url.path()).fileName();
    QString bundle;
    if (!media && !modularPackage) {
        bool ok = false;
        const QString proposal = QFileInfo(filename).completeBaseName()
            .replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
        bundle = QInputDialog::getText(this, tr("Package name"),
            tr("A package with the same name is replaced and keeps its load position; file conflicts with other packages reject the import."),
            QLineEdit::Normal, proposal, &ok).trimmed();
        if (!ok || bundle.isEmpty()) return;
        if (singleLua) filename = bundle + QStringLiteral(".lua");
        else filename = bundle + QStringLiteral(".zip");
    }
    // File selection briefly backgrounds the activity; start only after it returns.
    if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
        m_status->setText(tr("Return to the foreground and select the file again."));
        revealStatus();
        return;
    }
    run([this, url, media, filename, bundle, modularPackage](QString *error) {
        QFile file(devicePath(url));
        if (!file.open(QIODevice::ReadOnly)) { *error = file.errorString(); return false; }
        const auto progress = [this](quint64 done, quint64 total) {
            const int value = total ? int(qMin(100.0, double(done) * 100.0 / double(total))) : 0;
            QMetaObject::invokeMethod(this, [this, value] { m_progress->setValue(value); }, Qt::QueuedConnection);
        };
        if (media) return contentStore->stageMedia(file, &m_cancel, progress, error);
        if (modularPackage) return contentStore->stageModularPackage(file, filename, &m_cancel, progress, error);
        return contentStore->stageExtension(file, filename, bundle, &m_cancel, progress, error);
    });
}

void AndroidContentDialog::movePackage(int offset)
{
    const int row = m_packages->currentRow();
    if (row < 0 || row + offset < 0 || row + offset >= m_packages->count()) return;
    QStringList ids;
    for (int i = 0; i < m_packages->count(); ++i) ids << m_packages->item(i)->data(Qt::UserRole).toString();
    ids.swapItemsAt(row, row + offset);
    run([ids](QString *error) { return contentStore->reorderPackages(ids, error); });
}

void AndroidContentDialog::exportOrder()
{
    const QUrl url = QFileDialog::getSaveFileUrl(this, tr("Export load order"),
        QUrl(QStringLiteral("runtime-content.json")), QStringLiteral("JSON (*.json)"));
    if (url.isEmpty()) return;
    QFile file(devicePath(url));
    QString error;
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || !contentStore->exportDescriptor(file, &error) || !file.flush())
        QMessageBox::warning(this, tr("Export failed"), error.isEmpty() ? file.errorString() : error);
}
