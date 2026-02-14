#ifndef ANDROID_ASSETS_H
#define ANDROID_ASSETS_H

#ifdef ANDROID

//#include <QString>
//#include <QDir>

class AndroidAssets
{
public:
    static bool copyAssetsToWritableLocation();
    static QString getWritableDataPath();
    static bool copyAssetFile(const QString &assetPath, const QString &targetPath);
    static bool copyAssetDir(const QString &assetDir, const QString &targetDir);
    static bool createDefaultConfig(const QString &configPath);
    static bool checkQmlFilesExist();

private:
    static bool ensureDirectoryExists(const QString &path);
};

#endif // ANDROID
#endif // ANDROID_ASSETS_H