#ifndef QSAN_ASSET_MANIFEST_H
#define QSAN_ASSET_MANIFEST_H

#include <QString>
#include <QStringList>
#include <QVariantMap>

// 資產清單（Linux GUI M3）。
//
// repository 同 clean CI 都冇完整美術／音訊資產（見 AGENTS.md）。發佈出去嘅
// core runtime package 亦一樣：佢帶齊規則、Lua、介面腳本，但唔帶幾 GB 立繪
// 同語音。所以「乜嘢一定要有」同「乜嘢冇咗只係少啲聲畫」必須係一份可以讀、
// 可以驗、可以喺缺嘢嗰陣印出嚟嘅資料，而唔係散落喺 code 入面嘅假設。
//
// manifest 住喺 asset root：<assetRoot>/assets-manifest.json。
namespace QSanAssetManifest
{
struct Entry
{
    QString path;
    bool required = false;
    bool present = false;
};

struct Report
{
    bool manifestPresent = false;
    QString manifestPath;
    QString error;            // manifest 讀唔到／格式錯（缺 manifest 唔算錯）
    int schemaVersion = 0;
    QString gameVersion;
    QString assetPackVersion;
    QString assetRoot;
    QList<Entry> entries;

    QStringList missingRequired() const;
    QStringList missingOptional() const;
    // 缺 required 先至係「呢個包壞咗」；缺 optional 只係內容少啲。
    bool complete() const { return error.isEmpty() && missingRequired().isEmpty(); }
};

// 讀 manifest 並逐條檢查存在與否。assetRoot 留空即用
// QSanRuntimePaths::assetRoot()。
//
// manifestPath 留空即用 <assetRoot>/assets-manifest.json（安裝／打包出嚟嘅
// 位置）。開發樹同 CI 冇裝過嘢，manifest 只喺 build directory 入面，所以要
// 可以明確指一份 —— 否則「邊啲資產係預期缺失」呢個問題喺最需要答嘅場合
// （clean checkout 跑 GUI）反而答唔到。
Report inspect(const QString &assetRoot = QString(), const QString &manifestPath = QString());

// 給人睇嘅缺資產診斷（每行一句，唔會 crash，亦唔會扮成錯誤）。
QStringList diagnostics(const Report &report);

// JSON-able，畀 --asset-report 同 package smoke 用。
QVariantMap describe(const Report &report);
}

#endif
