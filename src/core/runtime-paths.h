#ifndef QSAN_RUNTIME_PATHS_H
#define QSAN_RUNTIME_PATHS_H

#include <QString>
#include <QStringList>
#include <QVariantMap>

// 執行期版面解析（Linux GUI M3）。
//
// 遊戲歷史上假設 `CWD == repository root`：engine 直接 load "lua/config.lua"，
// skin bank 直接開 "image/..."。安裝／打包之後呢個假設唔再成立，所以所有
// 「資產喺邊」同「可寫資料喺邊」嘅判斷收攏喺呢一個 module，唔再散落喺
// call site 度做 QDir::currentPath() 拼路徑。
//
// 解析出嚟嘅 asset root 會喺 startup 一次過 setCurrent()（見 resolve() 的註釋），
// 舊有嘅相對路徑因此繼續有效，但係目標由「使用者碰巧喺邊度開遊戲」變成
// 「一個真正存在、被驗證過嘅資產目錄」。
namespace QSanRuntimePaths
{
// asset root 由邊個來源決定。順序即係優先級。
enum class AssetRootSource {
    None,
    CommandLine,        // --asset-root <path>
    Environment,        // QSAN_ASSET_ROOT
    InstalledPrefix,    // <appDir>/../share/qsanguosha（GNUInstallDirs 安裝樹）
    PortableBundle,     // <appDir>/share/qsanguosha（可攜／AppImage 版面）
    WorkingDirectory,   // 目前工作目錄（開發樹的既有行為）
    ApplicationDir,     // binary 隔籬（Windows deploy 版面）
    ApplicationParent,  // <appDir>/..（source tree 出面嘅 build 輸出目錄）
};

struct Resolution
{
    bool resolved = false;
    QString assetRoot;
    AssetRootSource assetRootSource = AssetRootSource::None;
    QString userDataRoot;
    QString applicationDir;
    // 每個試過嘅候選：「來源=路徑=verdict」，缺資產診斷同 smoke report 都要用。
    QStringList candidates;
    // 明確指定（CLI／env）但唔合格嘅 root：呢個要當錯誤，唔可以靜靜落 fallback。
    QString error;
};

// 解析一次並記住結果。arguments 係完整 argv（含 argv[0]）或者
// QCoreApplication::arguments()；重複呼叫會沿用第一次嘅結果。
//
// 成功之後會 QDir::setCurrent(assetRoot())：呢個係 M3 對舊有相對路徑
// call site 的過渡橋樑，令「由任意 CWD 啟動」即刻成立。新 code 應該用
// assetPath()／userDataPath()，唔好再靠 CWD。
bool resolve(const QStringList &arguments, QString *error = nullptr);

bool isResolved();
const Resolution &resolution();

QString applicationDir();
QString assetRoot();
QString userDataRoot();

// assetRoot 下面嘅唯讀資產。relative 為空時等於 assetRoot()。
QString assetPath(const QString &relative);
// userDataRoot 下面嘅可寫檔案；會順手建立 parent directory。
QString userDataPath(const QString &relative);
// 對局記錄／replay 的目錄（會建立）。
QString recordDir();
// 使用者可以自訂、但亦有隨包附帶版本的內容（自訂劇本之類）：user data 行先，
// 搵唔到先返資產樹。回傳嘅路徑唔保證存在。
QString readablePath(const QString &relative);
// 自訂劇本目錄（可寫，會建立）。
QString customSceneDir();

QString sourceName(AssetRootSource source);
// smoke report／診斷用的 JSON-able 描述，唔會帶出家目錄以外嘅敏感資料。
QVariantMap describe();

// 測試用：清空已記住嘅結果。
void resetForTesting();
}

#endif
