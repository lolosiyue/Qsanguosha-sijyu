#ifndef ANDROID_ASSETS_H
#define ANDROID_ASSETS_H

#include <QString>

class AndroidAssets
{
public:
    static bool copyAssetsToWritableLocation(QString *error = nullptr);
    static QString getWritableDataPath();
    static bool copyAssetFile(const QString &assetPath,
                              const QString &targetPath,
                              QString *error = nullptr);
    static bool copyAssetDir(const QString &assetDir,
                             const QString &targetDir,
                             QString *error = nullptr);

private:
    static bool ensureDirectoryExists(const QString &path, QString *error);
};

#endif // ANDROID_ASSETS_H
