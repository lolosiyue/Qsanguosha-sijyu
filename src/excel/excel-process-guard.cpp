#include "excel-process-guard.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <windows.h>
#include <aclapi.h>
#include <wincrypt.h>
#endif

namespace {
QString safeError(const QString &value)
{
    QString result = value;
    result.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    return result.left(96);
}
}

ExcelProcessGuard::ExcelProcessGuard(QObject *parent) : QObject(parent) { }

ExcelProcessGuard::~ExcelProcessGuard()
{
#ifdef Q_OS_WIN
    if (m_stopEvent) SetEvent(static_cast<HANDLE>(m_stopEvent));
    // The stop event wakes this wait without depending on the Qt event loop.
    // Join before destroying the handles and context used by the worker.
    if (m_watchThread) { WaitForSingleObject(static_cast<HANDLE>(m_watchThread), INFINITE); CloseHandle(static_cast<HANDLE>(m_watchThread)); }
    if (m_stopEvent) CloseHandle(static_cast<HANDLE>(m_stopEvent));
    if (m_parentHandle) CloseHandle(static_cast<HANDLE>(m_parentHandle));
#endif
}

bool ExcelProcessGuard::initialize(qint64 parentPid, const QString &bootstrapPath, const QString &nonce,
                                   const QString &session, const QString &token, const QString &runtimeTier,
                                   int maxPlayers, QString *error)
{
    m_parentPid = parentPid; m_bootstrapPath = QFileInfo(bootstrapPath).absoluteFilePath();
    m_nonce = nonce; m_session = session; m_token = token; m_runtimeTier = runtimeTier; m_maxPlayers = maxPlayers;
    if (m_parentPid <= 0 || m_parentPid > 0xffffffffLL || m_bootstrapPath.isEmpty() || m_nonce.isEmpty() || m_session.isEmpty() || m_token.isEmpty()) {
        if (error) *error = QStringLiteral("invalid_bootstrap_arguments");
        return false;
    }
    if (!validateParent(error)) return false;
#ifdef Q_OS_WIN
    HANDLE handle = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, static_cast<DWORD>(m_parentPid));
    if (!handle) { if (error) *error = QStringLiteral("parent_unavailable"); return false; }
    FILETIME created, exitTime, kernel, user;
    if (!GetProcessTimes(handle, &created, &exitTime, &kernel, &user)) {
        CloseHandle(handle); if (error) *error = QStringLiteral("parent_identity_unavailable"); return false;
    }
    m_parentCreated = (static_cast<quint64>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    m_parentHandle = handle;
    if (!isExcelParent(error)) return false;
#endif
    const QFileInfo info(m_bootstrapPath);
    const QString directory = info.absolutePath();
    const QString leaf = QFileInfo(directory).fileName();
    if (QUuid(leaf).isNull() || leaf.isEmpty()) {
        if (error) *error = QStringLiteral("bootstrap_directory_name");
        return false;
    }
    if (QFileInfo(directory).isSymLink()) { if (error) *error = QStringLiteral("bootstrap_directory_link"); return false; }
    const QString expectedRoot = QDir::fromNativeSeparators(QDir::cleanPath(QDir::tempPath() + QStringLiteral("/QSanguoshaExcel")));
    const QString expected = QDir::fromNativeSeparators(QDir::cleanPath(expectedRoot + QStringLiteral("/") + leaf));
    if (QDir::fromNativeSeparators(directory).compare(expected, Qt::CaseInsensitive) != 0) { if (error) *error = QStringLiteral("bootstrap_directory_path"); return false; }
    if (QFileInfo(expectedRoot).isSymLink()) { if (error) *error = QStringLiteral("bootstrap_root_link"); return false; }
#ifdef Q_OS_WIN
    if (GetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(expectedRoot).utf16())) & FILE_ATTRIBUTE_REPARSE_POINT) {
        if (error) *error = QStringLiteral("bootstrap_root_link"); return false;
    }
#endif
    if (QFileInfo::exists(m_bootstrapPath)) { if (error) *error = QStringLiteral("bootstrap_exists"); return false; }
    const bool existed = QFileInfo::exists(directory);
    if (!existed && !QDir().mkpath(directory)) { if (error) *error = QStringLiteral("bootstrap_directory"); return false; }
    if (!QFileInfo(directory).isDir()) { if (error) *error = QStringLiteral("bootstrap_directory"); return false; }
#ifdef Q_OS_WIN
    if (GetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(directory).utf16())) & FILE_ATTRIBUTE_REPARSE_POINT) {
        if (error) *error = QStringLiteral("bootstrap_directory_link"); return false;
    }
    QDir contents(directory);
    const QFileInfoList entries = contents.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (const QFileInfo &entry : entries) {
        if (entry.fileName() != QStringLiteral("config.ini") && entry.fileName() != QStringLiteral("data")
            && entry.fileName() != QStringLiteral("cancel.request")) {
            if (entry.fileName() != info.fileName()) { if (error) *error = QStringLiteral("bootstrap_directory_contents"); return false; }
        }
        if (entry.isSymLink() || (GetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(entry.absoluteFilePath()).utf16())) & FILE_ATTRIBUTE_REPARSE_POINT)) {
            if (error) *error = QStringLiteral("bootstrap_reparse_point"); return false;
        }
    }
    if (!applyCurrentUserAcl(directory, error)) return false;
#else
    if (!existed) QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
#endif
    return true;
}

bool ExcelProcessGuard::validateParent(QString *error) const
{
#ifdef Q_OS_WIN
    HANDLE handle = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, static_cast<DWORD>(m_parentPid));
    if (!handle) { if (error) *error = QStringLiteral("parent_unavailable"); return false; }
    FILETIME created, exitTime, kernel, user;
    const bool ok = GetProcessTimes(handle, &created, &exitTime, &kernel, &user) != 0;
    if (!ok) { CloseHandle(handle); if (error) *error = QStringLiteral("parent_identity_unavailable"); return false; }
    const quint64 stamp = (static_cast<quint64>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    if (m_parentCreated != 0 && stamp != m_parentCreated) { CloseHandle(handle); if (error) *error = QStringLiteral("parent_reused"); return false; }
    CloseHandle(handle); return true;
#else
    QFile probe(QStringLiteral("/proc/%1/stat").arg(m_parentPid));
    if (!probe.exists()) { if (error) *error = QStringLiteral("parent_unavailable"); return false; }
    return true;
#endif
}

bool ExcelProcessGuard::parentAlive() const
{
#ifdef Q_OS_WIN
    if (!m_parentHandle) return false;
    return WaitForSingleObject(static_cast<HANDLE>(m_parentHandle), 0) == WAIT_TIMEOUT && validateParent(nullptr);
#else
    return validateParent(nullptr);
#endif
}

bool ExcelProcessGuard::startMonitoring(int intervalMs)
{
    if (!m_timer) { m_timer = new QTimer(this); connect(m_timer, SIGNAL(timeout()), this, SLOT(checkParent())); }
#ifdef Q_OS_WIN
    if (!m_parentHandle) {
        HANDLE handle = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, static_cast<DWORD>(m_parentPid));
        if (!handle) return false;
        FILETIME created, exitTime, kernel, user;
        if (!GetProcessTimes(handle, &created, &exitTime, &kernel, &user)) { CloseHandle(handle); return false; }
        m_parentCreated = (static_cast<quint64>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        m_parentHandle = handle;
    }
    if (!m_stopEvent) m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_stopEvent) return false;
    if (!m_watchThread)
        m_watchThread = CreateThread(nullptr, 0, &ExcelProcessGuard::parentWaitThread, this, 0, nullptr);
    if (!m_watchThread) return false;
#endif
    m_timer->start(qMax(100, intervalMs));
    return true;
}

#ifdef Q_OS_WIN
unsigned long __stdcall ExcelProcessGuard::parentWaitThread(void *context)
{
    ExcelProcessGuard *guard = static_cast<ExcelProcessGuard *>(context);
    HANDLE handles[2] = { static_cast<HANDLE>(guard->m_parentHandle), static_cast<HANDLE>(guard->m_stopEvent) };
    if (guard->m_parentHandle && WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0)
        TerminateProcess(GetCurrentProcess(), 0xE001);
    return 0;
}

bool ExcelProcessGuard::isExcelParent(QString *error) const
{
    HANDLE process = static_cast<HANDLE>(m_parentHandle);
    HMODULE psapi = LoadLibraryW(L"psapi.dll");
    if (!psapi) { if (error) *error = QStringLiteral("parent_image_unavailable"); return false; }
    typedef DWORD (WINAPI *ImageName)(HANDLE, LPWSTR, DWORD);
    ImageName imageName = reinterpret_cast<ImageName>(GetProcAddress(psapi, "GetProcessImageFileNameW"));
    wchar_t path[32768]; DWORD length = imageName ? imageName(process, path, sizeof(path) / sizeof(path[0])) : 0;
    FreeLibrary(psapi);
    if (!length) { if (error) *error = QStringLiteral("parent_image_unavailable"); return false; }
    QString image = QString::fromWCharArray(path, static_cast<int>(length));
    if (QFileInfo(image).fileName().compare(QStringLiteral("EXCEL.EXE"), Qt::CaseInsensitive) != 0) {
        if (error) *error = QStringLiteral("parent_not_excel"); return false;
    }
    return true;
}

bool ExcelProcessGuard::applyCurrentUserAcl(const QString &directory, QString *error) const
{
    const QString native = QDir::toNativeSeparators(directory);
    PSID owner = nullptr; PSECURITY_DESCRIPTOR existing = nullptr;
    if (GetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(native.utf16())), SE_FILE_OBJECT,
                              OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &existing) != ERROR_SUCCESS) {
        if (error) *error = QStringLiteral("bootstrap_owner"); return false;
    }
    HANDLE token = nullptr; DWORD bytes = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        LocalFree(existing); if (error) *error = QStringLiteral("bootstrap_owner"); return false;
    }
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    QByteArray tokenData(static_cast<int>(bytes), 0);
    const bool ownerOk = GetTokenInformation(token, TokenUser, tokenData.data(), bytes, &bytes)
        && EqualSid(owner, reinterpret_cast<PTOKEN_USER>(tokenData.data())->User.Sid);
    CloseHandle(token);
    LocalFree(existing);
    if (!ownerOk) { if (error) *error = QStringLiteral("bootstrap_owner"); return false; }
    PSID currentSid = reinterpret_cast<PTOKEN_USER>(tokenData.data())->User.Sid;
    EXPLICIT_ACCESSW access;
    ZeroMemory(&access, sizeof(access));
    access.grfAccessPermissions = GENERIC_ALL;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(currentSid);
    PACL dacl = nullptr;
    if (SetEntriesInAclW(1, &access, nullptr, &dacl) != ERROR_SUCCESS || !dacl) {
        if (error) *error = QStringLiteral("bootstrap_acl"); return false;
    }
    const DWORD result = SetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(native.utf16())), SE_FILE_OBJECT,
                                               DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                               nullptr, nullptr, dacl, nullptr);
    LocalFree(dacl);
    if (result != ERROR_SUCCESS) { if (error) *error = QStringLiteral("bootstrap_acl"); return false; }
    return true;
}
#endif

void ExcelProcessGuard::checkParent()
{
    if (!parentAlive()) { m_timer->stop(); emit parentDied(); }
}

QString ExcelProcessGuard::generateToken()
{
#ifdef Q_OS_WIN
    HCRYPTPROV provider = 0; BYTE bytes[32];
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)
        || !CryptGenRandom(provider, sizeof(bytes), bytes)) {
        if (provider) CryptReleaseContext(provider, 0); return QString();
    }
    CryptReleaseContext(provider, 0);
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(bytes), sizeof(bytes)).toHex());
#else
    QFile random(QStringLiteral("/dev/urandom"));
    if (!random.open(QIODevice::ReadOnly)) return QString();
    const QByteArray bytes = random.read(32);
    return bytes.size() == 32 ? QString::fromLatin1(bytes.toHex()) : QString();
#endif
}
QString ExcelProcessGuard::generateSession()
{
    // Qt 5.6 only provides the canonical braced UUID string.
    return QUuid::createUuid().toString().mid(1, 36);
}

bool ExcelProcessGuard::writeReady(quint16 port, QString *error)
{
    QJsonObject object; object.insert(QStringLiteral("api_version"), 1); object.insert(QStringLiteral("nonce"), m_nonce);
    object.insert(QStringLiteral("session"), m_session); object.insert(QStringLiteral("token"), m_token);
    object.insert(QStringLiteral("port"), static_cast<int>(port)); object.insert(QStringLiteral("pid"), static_cast<double>(QCoreApplication::applicationPid()));
    object.insert(QStringLiteral("parent_pid"), static_cast<double>(m_parentPid)); object.insert(QStringLiteral("parent_created"), QString::number(m_parentCreated));
    object.insert(QStringLiteral("runtime_tier"), m_runtimeTier); object.insert(QStringLiteral("max_players"), m_maxPlayers); object.insert(QStringLiteral("status"), QStringLiteral("ready"));
    return writeBootstrap(object, error);
}

bool ExcelProcessGuard::writeError(const QString &safeCode, QString *error)
{ QJsonObject object; object.insert(QStringLiteral("api_version"), 1); object.insert(QStringLiteral("nonce"), m_nonce); object.insert(QStringLiteral("status"), QStringLiteral("error")); object.insert(QStringLiteral("error"), safeError(safeCode)); return writeBootstrap(object, error); }

bool ExcelProcessGuard::writeBootstrap(const QJsonObject &object, QString *error)
{
    QSaveFile file(m_bootstrapPath);
    if (!file.open(QIODevice::WriteOnly)) { if (error) *error = QStringLiteral("bootstrap_open"); return false; }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (file.write(payload) != payload.size()) { if (error) *error = QStringLiteral("bootstrap_write"); return false; }
    if (!file.commit()) { if (error) *error = QStringLiteral("bootstrap_commit"); return false; }
    return true;
}
