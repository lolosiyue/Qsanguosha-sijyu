#include "android-content-dialog.h"
#include "android-content-store.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
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
        if (error) *error = QStringLiteral("內容儲存區尚未設定。");
        return false;
    }
    StartupWaitDialog dialog;
    dialog.setWindowTitle(QStringLiteral("三國殺 · 準備啟動"));
    dialog.setAttribute(Qt::WA_QuitOnClose, false);
    dialog.setWindowFlag(Qt::WindowCloseButtonHint, false);
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(QStringLiteral(
        "正在檢查資源與套用待生效內容……\n"
        "完整聲畫資源包含大量檔案，首次啟動可能需要數分鐘，請稍候。"), &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);
    auto *progress = new QProgressBar(&dialog);
    progress->setRange(0, 0);
    layout->addWidget(progress);

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
        label->setText(QStringLiteral("資源處理已結束，返回前景後顯示結果。"));
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
        if (error && error->isEmpty()) *error = QStringLiteral("啟動已中止。");
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
    setWindowTitle(QStringLiteral("三國殺 · 資源與擴展"));
    setProperty("androidContentDialogOwnScroll", true);
    setAttribute(Qt::WA_QuitOnClose, false);
    auto *outer = new QVBoxLayout(this);
    outer->setSizeConstraint(QLayout::SetNoConstraint);
    // A landscape phone can be shorter than the combined labels and controls.
    // Scroll the whole body while keeping cancel/close visible during imports.
    auto *bodyScroll = m_bodyScroll = new QScrollArea(this);
    bodyScroll->setWidgetResizable(true);
    bodyScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bodyScroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    // Widgets receive synthesized mouse events for unhandled touchscreen input.
    // The mouse recognizer consumes a drag and releases the pressed child outside
    // its bounds, preventing a swipe over an import button from clicking it.
    QScroller::grabGesture(bodyScroll->viewport(), QScroller::LeftMouseButtonGesture);
    bodyScroll->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, false);
    auto *body = new QWidget(bodyScroll);
    auto *layout = new QVBoxLayout(body);
    bodyScroll->setWidget(body);
    outer->addWidget(bodyScroll, 1);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_status);
    auto *notice = new QLabel(QStringLiteral(
        "首次開局須完整匯入聲畫資源。匯入與管理變更在重新啟動後生效。\n"
        "只匯入信任的 Lua：腳本可使用完整 Lua 能力，讀寫本程式可存取的檔案。"
        "匯入期間請保持前景；切背景或鎖屏會取消本次匯入。"), this);
    notice->setWordWrap(true);
    notice->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(notice);
    m_packages = new QListWidget(this);
    m_packages->setFixedHeight(100);
    QScroller::grabGesture(m_packages->viewport(), QScroller::LeftMouseButtonGesture);
    m_packages->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, false);
    layout->addWidget(m_packages);
    auto *panel = new QWidget(body);
    auto *buttons = new QVBoxLayout(panel);
    auto add = [&](const QString &label, const std::function<void()> &fn) {
        auto *button = new QPushButton(label, panel);
        button->setMinimumHeight(48);
        buttons->addWidget(button);
        m_actions.append(button);
        connect(button, &QPushButton::clicked, this, fn);
        return button;
    };
    add(QStringLiteral("匯入完整聲畫 ZIP"), [this] { importFile(true, false); });
    add(QStringLiteral("匯入擴展 ZIP"), [this] { importFile(false, false); });
    add(QStringLiteral("匯入單一 Lua"), [this] { importFile(false, true); });
    add(QStringLiteral("啟用／停用整包"), [this] {
        const QString id = selectedPackage();
        if (id.isEmpty()) return;
        for (const auto &p : contentStore->packages()) if (p.id == id) {
            run([id, enabled = !p.enabled](QString *e) { return contentStore->setPackageEnabled(id, enabled, e); });
            break;
        }
    });
    add(QStringLiteral("移除所選整包"), [this] {
        const QString id = selectedPackage();
        if (id.isEmpty()) return;
        if (QMessageBox::question(this, QStringLiteral("移除擴展"),
            QStringLiteral("移除「%1」？重新啟動後生效。").arg(id)) == QMessageBox::Yes)
            run([id](QString *e) { return contentStore->removePackage(id, e); });
    });
    add(QStringLiteral("所選整包上移"), [this] { movePackage(-1); });
    add(QStringLiteral("所選整包下移"), [this] { movePackage(1); });
    add(QStringLiteral("匯出載入順序"), [this] { exportOrder(); });
    add(QStringLiteral("切回上一版本"), [this] {
        const bool recovery = m_startup && contentStore->needsRecovery();
        run([recovery](QString *e) { return recovery ? contentStore->recoverPrevious(e) : contentStore->rollback(e); });
    });
    m_recoverButton = add(QStringLiteral("停用上次匯入／取消待套用變更"), [this] {
        run([](QString *e) { return contentStore->discardPendingAndDisableLastImport(e); });
    });
    layout->addWidget(panel);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    outer->addWidget(m_progress);
    auto *footer = new QHBoxLayout;
    m_cancelButton = new QPushButton(QStringLiteral("取消匯入"), this);
    m_cancelButton->setMinimumHeight(48);
    connect(m_cancelButton, &QPushButton::clicked, this, [this] { m_cancel.store(true); });
    footer->addWidget(m_cancelButton);
    m_continue = new QPushButton(startup ? QStringLiteral("進入首頁") : QStringLiteral("完成"), this);
    m_continue->setMinimumHeight(48);
    connect(m_continue, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(m_continue);
    auto *close = new QPushButton(startup ? QStringLiteral("關閉程式") : QStringLiteral("返回"), this);
    close->setMinimumHeight(48);
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
        auto *item = new QListWidgetItem(QStringLiteral("%1  · %2%3")
            .arg(p.id, p.enabled ? QStringLiteral("已啟用") : QStringLiteral("已停用"),
                 p.bundled ? QStringLiteral(" · 隨包原版") : QString()), m_packages);
        item->setData(Qt::UserRole, p.id);
        if (p.id == selected) m_packages->setCurrentItem(item);
    }
    const bool upgradeConflict = contentStore->status().value("upgrade_conflict").toBool();
    m_recoverButton->setText(upgradeConflict
        ? QStringLiteral("還原隨包擴展，保留媒體")
        : QStringLiteral("停用上次匯入／取消待套用變更"));
    m_status->setText(upgradeConflict
        ? QStringLiteral("APK 新增內容與現有擴展衝突。請調整整包，或還原隨包擴展；舊內容仍保留於上一版本。\n%1")
            .arg(contentStore->status().value("recovery_error").toString())
        : contentStore->needsRecovery()
        ? QStringLiteral("上次內容啟動未完成。請停用上次匯入或切回上一版本。")
        : contentStore->hasPending() ? QStringLiteral("變更已完整暫存。請關閉並重新開啟程式以套用。")
        : contentStore->mediaReady() ? QStringLiteral("完整資源已就緒。")
        : QStringLiteral("尚未匯入完整聲畫資源，暫不能開局。"));
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
    m_status->setText(QStringLiteral("正在處理，請保持前景……"));
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
                ? QStringLiteral("操作已取消，現用版本保留。") : result->error);
        }
        else m_progress->setValue(100);
        revealStatus();
        if (m_closeWhenFinished) QDialog::reject();
    });
    m_worker->start();
}

void AndroidContentDialog::importFile(bool media, bool singleLua)
{
    const QUrl url = QFileDialog::getOpenFileUrl(this, QStringLiteral("選擇匯入檔案"), QUrl(),
        singleLua ? QStringLiteral("Lua (*.lua);;所有檔案 (*)")
                  : QStringLiteral("ZIP (*.zip);;所有檔案 (*)"));
    if (url.isEmpty()) return;
    QString filename = QFileInfo(url.path()).fileName();
    QString bundle;
    if (!media) {
        bool ok = false;
        const QString proposal = QFileInfo(filename).completeBaseName()
            .replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
        bundle = QInputDialog::getText(this, QStringLiteral("整包名稱"),
            QStringLiteral("相同名稱會替換原包並保留載入位置；其他包的檔案衝突會拒絕匯入。"),
            QLineEdit::Normal, proposal, &ok).trimmed();
        if (!ok || bundle.isEmpty()) return;
        if (singleLua) filename = bundle + QStringLiteral(".lua");
        else filename = bundle + QStringLiteral(".zip");
    }
    // File selection briefly backgrounds the activity; start only after it returns.
    if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
        m_status->setText(QStringLiteral("請回到前景後重新選取檔案。"));
        revealStatus();
        return;
    }
    run([this, url, media, filename, bundle](QString *error) {
        QFile file(devicePath(url));
        if (!file.open(QIODevice::ReadOnly)) { *error = file.errorString(); return false; }
        const auto progress = [this](quint64 done, quint64 total) {
            const int value = total ? int(qMin(100.0, double(done) * 100.0 / double(total))) : 0;
            QMetaObject::invokeMethod(this, [this, value] { m_progress->setValue(value); }, Qt::QueuedConnection);
        };
        return media ? contentStore->stageMedia(file, &m_cancel, progress, error)
            : contentStore->stageExtension(file, filename, bundle, &m_cancel, progress, error);
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
    const QUrl url = QFileDialog::getSaveFileUrl(this, QStringLiteral("匯出載入順序"),
        QUrl(QStringLiteral("runtime-content.json")), QStringLiteral("JSON (*.json)"));
    if (url.isEmpty()) return;
    QFile file(devicePath(url));
    QString error;
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || !contentStore->exportDescriptor(file, &error) || !file.flush())
        QMessageBox::warning(this, QStringLiteral("匯出失敗"), error.isEmpty() ? file.errorString() : error);
}
