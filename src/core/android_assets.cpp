#ifdef ANDROID

#include "android_assets.h"
//#include <QStandardPaths>
//#include <QFile>
//#include <QDebug>
//#include <QDirIterator>

QString AndroidAssets::getWritableDataPath()
{
    // Use external storage so users can access and modify files
    QString externalPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QString gamePath = externalPath + "/QSanguosha";

    // Fallback to app-specific external storage if generic fails
	//if(externalPath.isEmpty()){
        gamePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	//}
    return gamePath;
}

bool AndroidAssets::ensureDirectoryExists(const QString &path)
{
    static QDir dir;
    if (!dir.exists(path)) {
        return dir.mkpath(path);
    }
    return true;
}

bool AndroidAssets::copyAssetFile(const QString &assetPath, const QString &targetPath)
{
    // Check if target file already exists and is newer
    if (QFile::exists(targetPath)) {
        // For now, skip existing files to avoid overwriting user data
        return true;
    }
    // Ensure target directory exists
    QFileInfo targetInfo(targetPath);
    if (!ensureDirectoryExists(targetInfo.absolutePath())) {
        qWarning() << "Failed to create directory:" << targetInfo.absolutePath();
        return false;
    }
    // Copy from assets (Qt resource system)
    QString resourcePath = "assets:/" + assetPath;
    QFile sourceFile(resourcePath);
    
    if (!sourceFile.exists()) {
        qWarning() << "Asset file not found:" << resourcePath;
        return false;
    }
    
    if (!sourceFile.copy(targetPath)) {
        qWarning() << "Failed to copy asset:" << assetPath << "to" << targetPath;
        return false;
    }
    
    // Make the copied file writable
    QFile::setPermissions(targetPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    
    return true;
}

bool AndroidAssets::copyAssetDir(const QString &assetDir, const QString &targetDir)
{
    if (!ensureDirectoryExists(targetDir)) {
        qWarning() << "Failed to create target directory:" << targetDir;
        return false;
    }
    
    QString resourceDir = "assets:/" + assetDir;
    QDirIterator it(resourceDir, QDirIterator::Subdirectories);
    
    bool success = true;
    while (it.hasNext()) {
        QString assetFile = it.next();
        QFileInfo info(assetFile);
        
        if (info.isFile()) {
            QString relativePath = assetFile.mid(resourceDir.length() + 1);
            QString targetFile = targetDir + "/" + relativePath;
            
            if (!copyAssetFile(assetDir + "/" + relativePath, targetFile)) {
                success = false;
            }
        }
    }
    
    return success;
}

bool AndroidAssets::copyAssetsToWritableLocation()
{
    QString dataPath = getWritableDataPath();
	if (QFile::exists(dataPath + "/config.ini"))
		return true;
    
    bool success = true;
	
    // Copy essential directories
    QStringList assetDirs = {
        "audio",
        "diy",
        "dmp",
        "doc",
        "etc",
        "extensions",
        "font",
        "iconengines",
        "image",
        "imageformats",
        "lang",
        "listserver",
        "lua",
        "platforminputcontexts",
        "platforms",
        "qmltooling",
        "QtQuick.2",
        "scenarios",
        "skins",
        //"sqldrivers",
        "translations",
        "virtualkeyboard",
        "ui-script"
    };
    for (const QString &dir : assetDirs) {
        QString targetDir = dataPath + "/" + dir;
        if (!copyAssetDir(dir, targetDir)) {
            success = false;
        }
    }
    
    // Copy essential files
    QStringList assetFiles = {
        "config.ini",
        "sanguosha.qm",
        "qt_zh_CN.qm"
    };
    
    for (const QString &file : assetFiles) {
        QString targetFile = dataPath + "/" + file;
        if (!copyAssetFile(file, targetFile)) {
            success = false;
        }
    }
    
    return success;
}

bool AndroidAssets::createDefaultConfig(const QString &configPath)
{
    QFile configFile(configPath);
    if (!configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to create config file:" << configPath;
        return false;
    }

    QTextStream out(&configFile);

    // Write default Android configuration
    out << "[General]\n";
    out << "GameMode=02p\n";
    out << "UserName=Player\n";
    out << "ServerName=Player's server\n";
    out << "Address=\n";
    out << "HostAddress=127.0.0.1\n";
    out << "ServerPort=9527\n";
    out << "EnableAI=true\n";
    out << "AIDelay=1000\n";
    out << "EnableBgMusic=false\n";  // Disabled for Android
    out << "EnableEffects=true\n";
    out << "BGMVolume=1.0\n";
    out << "EffectVolume=1.0\n";
    out << "BackgroundImage=image/system/backdrop/new-version.jpg\n";
    out << "UseFullSkin=true\n";
    out << "EnableHotKey=false\n";  // Disabled for Android
    out << "NeverNullifyMyTrick=false\n";
    out << "EnableLastWord=true\n";
    out << "BubbleChatboxKeepTime=2000\n";
    out << "CountDownSeconds=3\n";
    out << "OperationTimeout=15\n";
    out << "OperationNoLimit=false\n";
    out << "EnableAutoTarget=true\n";
    out << "EnableIntellectualSelection=true\n";
    out << "EnableDoubleClick=true\n";
    out << "EnableAutoPreshow=false\n";
    out << "EnableAutoSaveRecord=false\n";
    out << "RecordSavePath=records\n";
    out << "NetworkInterface=\n";
    out << "ChatFont=@Arial,9,-1,5,50,0,0,0,0,0\n";
    out << "UIFont=@Arial,9,-1,5,50,0,0,0,0,0\n";
    out << "TextEditFont=@Arial,9,-1,5,50,0,0,0,0,0\n";

    configFile.close();
    return true;
}

bool AndroidAssets::checkQmlFilesExist()
{
    QString dataPath = getWritableDataPath();
    QString uiScriptDir = dataPath + "/ui-script";

    // 确保ui-script目录存在
    if (!ensureDirectoryExists(uiScriptDir)) {
        qWarning() << "Failed to create ui-script directory:" << uiScriptDir;
        return false;
    }

    // 检查关键QML文件是否存在
    QStringList essentialQmlFiles = {
        "animation.qml",
        "Default.qml"
    };

    bool allExist = true;
    for (const QString &qmlFile : essentialQmlFiles) {
        QString targetPath = uiScriptDir + "/" + qmlFile;
        if (QFile::exists(targetPath)) {
            qDebug() << "QML file exists:" << qmlFile;
        } else {
            qWarning() << "QML file missing:" << qmlFile << "at" << targetPath;
            allExist = false;
        }
    }

    if (!allExist) {
        qWarning() << "Some essential QML files are missing.";
        qWarning() << "Please manually copy QML files to:" << uiScriptDir;
    }

    return allExist;
}

#endif // ANDROID