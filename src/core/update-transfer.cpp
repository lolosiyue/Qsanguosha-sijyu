#include "update-transfer.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QThread>
#include <QStorageInfo>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <sys/stat.h>
#endif

namespace {
bool realPath(const QString &path) {
    QString cursor = QFileInfo(path).absoluteFilePath();
    while (!cursor.isEmpty()) {
        QFileInfo info(cursor);
        if (info.isSymLink()) return false;
        const QString parent = info.absolutePath();
        if (parent == cursor) break;
        cursor = parent;
    }
    return true;
}
bool singleLinkFile(const QString &path) {
    const QFileInfo file(path);
    if (!file.exists()) return !file.isSymLink();
    if (!file.isFile() || file.isSymLink()) return false;
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(file.absoluteFilePath());
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    const bool ok = GetFileInformationByHandle(handle, &info) && info.nNumberOfLinks == 1
        && !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
    CloseHandle(handle);
    return ok;
#else
    struct stat info{};
    return ::lstat(QFile::encodeName(path).constData(), &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1;
#endif
}
}
UpdateTransfer::UpdateTransfer(QObject *parent, QNetworkAccessManager *network)
    : QObject(parent), m_manager(network ? network : &m_network) {
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(30000);
    connect(&m_timeout, &QTimer::timeout, this, [this] { fail(tr("The update request timed out. Try again to resume the download.")); });
}
UpdateTransfer::~UpdateTransfer() {
    m_cancel = true;
    stop();
    if (m_worker) { m_worker->wait(); delete m_worker; }
}
bool UpdateTransfer::busy() const { return m_reply || m_worker; }
void UpdateTransfer::stop() {
    m_timeout.stop();
    if (m_reply) { disconnect(m_reply, nullptr, this, nullptr); m_reply->abort(); m_reply->deleteLater(); m_reply = nullptr; }
    m_file.close();
}
void UpdateTransfer::cancel() {
    m_cancel = true;
    if (m_worker) return; // Hash verification checks cancellation between blocks.
    const bool active = busy();
    stop(); m_lock.reset();
    if (active) emit failed(tr("Cancelled. Partial downloads are kept for the next attempt."));
}
void UpdateTransfer::fail(const QString &message, bool discardPartial) {
    stop();
    if (discardPartial && !m_partial.isEmpty()) QFile::remove(m_partial);
    m_lock.reset();
    emit failed(message);
}
void UpdateTransfer::catalog(const QUrl &url) {
    if (busy()) return;
    m_download = false; m_cancel = false; m_bytes.clear(); m_redirects = 0;
    request(url);
}
void UpdateTransfer::download(const QSanUpdates::Entry &entry, const QString &userDataRoot) {
    if (busy()) return;
    if (!entry.downloadable || !QSanUpdates::safeHttps(entry.url) || entry.size <= 0
        || entry.size > (entry.material ? QSanUpdates::PackageLimit : QSanUpdates::GameLimit)
        || !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(entry.sha256).hasMatch()) {
        emit failed(tr("The download descriptor is invalid.")); return;
    }
    m_download = true; m_cancel = false; m_entry = entry; m_redirects = 0;
    const QString root = QDir(userDataRoot).filePath(QStringLiteral("update-downloads/")) + entry.sha256;
    if (userDataRoot.isEmpty() || !realPath(root) || !QDir().mkpath(root)) {
        emit failed(tr("Cannot create a safe download directory.")); return;
    }
    m_lock.reset(new QLockFile(QDir(root).filePath(QStringLiteral("download.lock"))));
    if (!m_lock->tryLock(0)) { m_lock.reset(); emit failed(tr("This download is already in use.")); return; }
    // Catalog names are presentation only for packages; never derive paths from an R2 key.
    const QString name = entry.material ? QStringLiteral("package.zip") : entry.name;
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,179}$")).match(name).hasMatch()) {
        fail(tr("The download filename is unsafe.")); return;
    }
    m_complete = QDir(root).filePath(name);
    m_partial = m_complete + QStringLiteral(".part");
    if (!realPath(m_partial) || !realPath(m_complete)
        || !singleLinkFile(m_partial) || !singleLinkFile(m_complete)) {
        fail(tr("The download path is unsafe.")); return;
    }
    if (QFileInfo::exists(m_complete)) { verify(m_complete, true); return; }
    m_file.setFileName(m_partial);
    if (!m_file.open(QIODevice::ReadWrite)) { fail(tr("Cannot open the partial download.")); return; }
    if (m_file.size() > entry.size && !m_file.resize(0)) { fail(tr("Cannot reset the partial download.")); return; }
    m_offset = m_file.size();
    if (m_offset == entry.size) { m_file.close(); verify(m_partial, false); return; }
    // Budget the full response even when resuming: an endpoint may ignore Range.
    QStorageInfo storage(root); storage.refresh();
    constexpr qint64 reserve = 512LL * 1024 * 1024;
    if (!storage.isValid() || !storage.isReady() || storage.isReadOnly()
        || storage.bytesAvailable() < entry.size + reserve) {
        fail(tr("Insufficient free space for the download and safety reserve.")); return;
    }
    if (!m_file.seek(m_offset)) { fail(tr("Cannot open the partial download.")); return; }
    request(entry.url);
}
void UpdateTransfer::request(const QUrl &url) {
    if (!QSanUpdates::safeHttps(url)) { fail(tr("Update endpoints and redirects must use HTTPS without credentials.")); return; }
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setRawHeader("User-Agent", "QSanguosha-CheckUpdates/1");
    req.setRawHeader("Accept-Encoding", "identity");
    req.setRawHeader("Accept", m_download ? "application/octet-stream" : "application/vnd.github+json");
    if (url.host() == QStringLiteral("api.github.com")) req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    if (m_download && m_offset) req.setRawHeader("Range", "bytes=" + QByteArray::number(m_offset) + '-');
    m_headers = false;
    m_reply = m_manager->get(req);
    m_reply->setReadBufferSize(256 * 1024);
    connect(m_reply, &QNetworkReply::readyRead, this, &UpdateTransfer::consume);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateTransfer::finish);
    m_timeout.start();
}
bool UpdateTransfer::headers() {
    if (!m_reply) return false;
    if (m_headers) return true;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300 && status < 400) return false;
    if (status == 403 || status == 429) {
        fail(tr("The update service refused the request or reached its rate limit. Try again later.")); return false;
    }
    if (status != 200 && !(m_download && m_offset && status == 206)) {
        fail(tr("The update service returned HTTP %1.").arg(status)); return false;
    }
    if (m_download && status == 206) {
        const auto range = QRegularExpression(QStringLiteral("^bytes ([0-9]+)-([0-9]+)/([0-9]+)$"))
            .match(QString::fromLatin1(m_reply->rawHeader("Content-Range")));
        if (!range.hasMatch() || range.captured(1).toLongLong() != m_offset
            || range.captured(2).toLongLong() != m_entry.size - 1
            || range.captured(3).toLongLong() != m_entry.size) {
            fail(tr("The server returned an invalid download range."), true); return false;
        }
    } else if (m_download && m_offset) {
        if (!m_file.resize(0) || !m_file.seek(0)) { fail(tr("Cannot reset the partial download.")); return false; }
        m_offset = 0; // A server without Range support safely starts over.
    }
    bool ok;
    const auto length = m_reply->rawHeader("Content-Length").toLongLong(&ok);
    const qint64 limit = m_download ? m_entry.size - m_offset : QSanUpdates::CatalogLimit;
    if ((ok && (length < 0 || length > limit || (m_download && length != limit)))
        || (!m_reply->rawHeader("Content-Encoding").isEmpty()
            && m_reply->rawHeader("Content-Encoding") != "identity")) {
        fail(tr("The update response has an invalid size or encoding."), m_download); return false;
    }
    m_headers = true; return true;
}
void UpdateTransfer::consume() {
    if (!headers()) return;
    m_timeout.start();
    while (m_reply && m_reply->bytesAvailable()) {
        const QByteArray block = m_reply->read(256 * 1024);
        if (m_download) {
            if (m_file.pos() > m_entry.size - block.size()) {
                fail(tr("The download exceeds its declared size."), true); return;
            }
            if (m_file.write(block) != block.size()) { fail(tr("Cannot write the download. Check available disk space.")); return; }
            emit progress(m_file.pos(), m_entry.size);
        } else {
            if (m_bytes.size() > QSanUpdates::CatalogLimit - block.size()) {
                fail(tr("The update catalog exceeds the size limit.")); return;
            }
            m_bytes += block;
        }
    }
}
void UpdateTransfer::finish() {
    if (!m_reply) return;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300 && status < 400) {
        const QUrl next = m_reply->url().resolved(m_reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
        disconnect(m_reply, nullptr, this, nullptr); m_reply->deleteLater(); m_reply = nullptr;
        if (++m_redirects > 5 || !QSanUpdates::safeHttps(next)) { fail(tr("Unsafe or excessive update redirects.")); return; }
        request(next); return;
    }
    if (m_reply->error() != QNetworkReply::NoError && status != 403 && status != 429) {
        fail(tr("The update request failed. Check your connection and try again.")); return;
    }
    consume();
    if (!m_reply) return;
    if (!m_headers && !headers()) return;
    if (m_download) {
        if (m_file.size() != m_entry.size || !m_file.flush()) { fail(tr("The download is incomplete. Try again to resume.")); return; }
        stop(); verify(m_partial, false);
    } else {
        stop(); emit catalogReady(m_bytes);
    }
}
void UpdateTransfer::verify(const QString &path, bool cached) {
    const QPointer<UpdateTransfer> guard(this);
    const QString expected = m_entry.sha256;
    const qint64 size = m_entry.size;
    m_worker = QThread::create([this, guard, path, cached, expected, size] {
        QFile file(path);
        bool ok = realPath(path) && file.open(QIODevice::ReadOnly) && file.size() == size;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (ok && !file.atEnd() && !m_cancel) {
            const QByteArray block = file.read(256 * 1024);
            if (block.isEmpty() && file.error() != QFileDevice::NoError) ok = false;
            else hash.addData(block);
        }
        ok = ok && QString::fromLatin1(hash.result().toHex()) == expected;
        QMetaObject::invokeMethod(this, [guard, path, cached, ok] {
            if (!guard) return;
            auto *self = guard.data();
            self->m_worker->wait(); delete self->m_worker; self->m_worker = nullptr;
            if (self->m_cancel) { self->m_lock.reset(); emit self->failed(tr("Cancelled. Partial downloads are kept for the next attempt.")); return; }
            if (!ok) {
                QFile::remove(path);
                self->fail(tr("The SHA-256 or size check failed. The untrusted download was discarded.")); return;
            }
            if (!cached && !QFile::rename(path, self->m_complete)) { self->fail(tr("Cannot publish the verified download.")); return; }
            self->m_lock.reset(); emit self->downloadReady(self->m_complete);
        }, Qt::QueuedConnection);
    });
    m_worker->start();
}
