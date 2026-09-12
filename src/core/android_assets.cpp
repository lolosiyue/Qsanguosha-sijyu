#include "android_assets.h"

#include <QDir>
#include <QDirIterator>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace {

bool setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
    qWarning("Android asset deployment failed: %s", qPrintable(message));
    return false;
}

bool isSafeRelativePath(const QString &path, QString *error)
{
    QString portablePath = path;
    portablePath.replace('\\', '/');
    const QStringList parts = portablePath.split('/', Qt::KeepEmptyParts);
    if (portablePath.isEmpty() || portablePath.startsWith('/') || portablePath.contains(':'))
        return setError(error, QStringLiteral("asset path is not relative: %1").arg(path));

    for (const QString &part : parts) {
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral(".."))
            return setError(error, QStringLiteral("asset path contains an invalid component: %1").arg(path));
    }
    return true;
}

QString resourcePathFor(const QString &assetPath)
{
    return QStringLiteral(":/assets/") + assetPath;
}

} // namespace

QString AndroidAssets::getWritableDataPath()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty())
        return {};

    // Android's application data path can have a legitimate system symlink in
    // its ancestry; canonicalize that trusted root before creating runtime/.
    const QFileInfo appDataInfo(appDataPath);
    const QString canonicalAppDataPath = appDataInfo.canonicalFilePath();
    const QString stableAppDataPath = canonicalAppDataPath.isEmpty()
        ? appDataInfo.absoluteFilePath()
        : canonicalAppDataPath;
    return QDir(stableAppDataPath).filePath(QStringLiteral("runtime"));
}

bool AndroidAssets::ensureDirectoryExists(const QString &path, QString *error)
{
    if (path.isEmpty())
        return setError(error, QStringLiteral("target directory is empty"));

    const QFileInfo pathInfo(path);
    const QString absolutePath = pathInfo.absoluteFilePath();
    const QFileInfo existingInfo(absolutePath);

    // Never follow a child symlink while creating the private runtime tree.
    if (existingInfo.isSymLink())
        return setError(error, QStringLiteral("target directory is a symbolic link: %1").arg(absolutePath));
    if (existingInfo.exists()) {
        if (!existingInfo.isDir())
            return setError(error, QStringLiteral("target path is not a directory: %1").arg(absolutePath));
        const QString parentPath = QFileInfo(absolutePath).dir().absolutePath();
        if (parentPath != absolutePath && !ensureDirectoryExists(parentPath, error))
            return false;
        return true;
    }

    const QString parentPath = QFileInfo(absolutePath).dir().absolutePath();
    if (parentPath != absolutePath && !ensureDirectoryExists(parentPath, error))
        return false;

    QDir parentDir(parentPath);
    const QString directoryName = QFileInfo(absolutePath).fileName();
    if (!parentDir.mkdir(directoryName)) {
        const QFileInfo createdInfo(absolutePath);
        if (!createdInfo.exists() || createdInfo.isSymLink() || !createdInfo.isDir())
            return setError(error, QStringLiteral("cannot create target directory: %1").arg(absolutePath));
    }
    return true;
}

bool AndroidAssets::copyAssetFile(const QString &assetPath,
                                  const QString &targetPath,
                                  QString *error)
{
    QString portableAssetPath = assetPath;
    portableAssetPath.replace('\\', '/');
    if (!isSafeRelativePath(portableAssetPath, error))
        return false;

    const QFileInfo targetInfo(targetPath);
    if (targetInfo.isSymLink())
        return setError(error, QStringLiteral("target file is a symbolic link: %1").arg(targetPath));
    if (!ensureDirectoryExists(targetInfo.absolutePath(), error))
        return false;
    if (targetInfo.exists()) {
        if (!targetInfo.isFile())
            return setError(error, QStringLiteral("target path is not a regular file: %1").arg(targetPath));
        // TODO/human preserves the user's writable copy on every startup.
        return true;
    }

    QFile sourceFile(resourcePathFor(portableAssetPath));
    if (!sourceFile.open(QIODevice::ReadOnly))
        return setError(error, QStringLiteral("asset file is missing or unreadable: %1").arg(portableAssetPath));

    // Publish only after the complete qrc file has been written and flushed.
    const QString temporaryTemplate = QDir(targetInfo.absolutePath())
        .filePath(QStringLiteral(".qsanguosha-asset-XXXXXX"));
    QTemporaryFile temporaryFile(temporaryTemplate);
    if (!temporaryFile.open())
        return setError(error, QStringLiteral("cannot create temporary asset file beside: %1").arg(targetPath));

    auto cleanup = [&temporaryFile]() {
        temporaryFile.close();
    };

    QByteArray buffer(64 * 1024, Qt::Uninitialized);
    while (!sourceFile.atEnd()) {
        const qint64 bytesRead = sourceFile.read(buffer.data(), buffer.size());
        if (bytesRead <= 0 || temporaryFile.write(buffer.constData(), bytesRead) != bytesRead) {
            cleanup();
            return setError(error, QStringLiteral("cannot write temporary asset file: %1").arg(targetPath));
        }
    }
    if (sourceFile.error() != QFile::NoError) {
        cleanup();
        return setError(error, QStringLiteral("cannot read asset file: %1").arg(portableAssetPath));
    }
    if (!temporaryFile.flush()) {
        cleanup();
        return setError(error, QStringLiteral("cannot flush temporary asset file: %1").arg(targetPath));
    }
    temporaryFile.close();

    // A concurrent importer may have created the file; never replace it.
    const QFileInfo publishedInfo(targetPath);
    if (publishedInfo.isSymLink() || publishedInfo.exists()) {
        if (publishedInfo.isSymLink() || !publishedInfo.isFile())
            return setError(error, QStringLiteral("target appeared as a non-file: %1").arg(targetPath));
        return true;
    }

    if (!temporaryFile.rename(targetPath)) {
        return setError(error, QStringLiteral("cannot publish asset file: %1").arg(targetPath));
    }
    temporaryFile.setAutoRemove(false);
    return true;
}

bool AndroidAssets::copyAssetDir(const QString &assetDir,
                                 const QString &targetDir,
                                 QString *error)
{
    QString portableAssetDir = assetDir;
    portableAssetDir.replace('\\', '/');
    if (!portableAssetDir.isEmpty() && !isSafeRelativePath(portableAssetDir, error))
        return false;
    if (!ensureDirectoryExists(targetDir, error))
        return false;

    const QString resourceDir = portableAssetDir.isEmpty()
        ? QStringLiteral(":/assets")
        : resourcePathFor(portableAssetDir);
    QDir resourceDirectory(resourceDir);
    if (!resourceDirectory.exists())
        return setError(error, QStringLiteral("asset directory is missing: %1").arg(resourceDir));

    QDirIterator iterator(resourceDir, QDir::Files, QDirIterator::Subdirectories);
    int copiedFiles = 0;
    while (iterator.hasNext()) {
        const QString sourcePath = iterator.next();
        const QString relativePath = resourceDirectory.relativeFilePath(sourcePath);
        if (!isSafeRelativePath(relativePath, error))
            return false;

        const QString assetPath = portableAssetDir.isEmpty()
            ? relativePath
            : portableAssetDir + '/' + relativePath;
        const QString targetPath = QDir(targetDir).filePath(relativePath);
        if (!copyAssetFile(assetPath, targetPath, error))
            return false;
        ++copiedFiles;
    }

    if (copiedFiles == 0)
        return setError(error, QStringLiteral("asset directory is empty: %1").arg(resourceDir));
    return true;
}

bool AndroidAssets::copyAssetsToWritableLocation(QString *error)
{
    if (error)
        error->clear();

    const QString dataPath = getWritableDataPath();
    if (dataPath.isEmpty())
        return setError(error, QStringLiteral("app data location is unavailable"));
    return copyAssetDir(QString(), dataPath, error);
}
