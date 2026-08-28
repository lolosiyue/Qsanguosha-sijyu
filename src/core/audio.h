#ifndef _AUDIO_H
#define _AUDIO_H

#include <QJsonObject>
#include <QString>

// 全個 client 唯一的 audio facade。M2B-A 冇新增第二套 facade：呢個 class 保持
// 原有的 static API（call site 一個都唔使改），只係將實作轉交畀 IAudioBackend
// （src/ui/audio/audio-backend.h）。
//
// 實作只會編入 GUI target（QSanguosha）。qsanguosha_engine／qsanguosha_server
// 唔會定義 AUDIO_SUPPORT，所以 dedicated server 由頭到尾唔會拉到 Qt Multimedia。
class Audio
{
public:
    static void init();
    static void quit();

    // filename 係短 UI 音效定係武將語音由 classifyAudioFile() 判斷，call site
    // 唔使自己知。superpose=false 保持舊語義：同一個檔案響緊就唔重疊播。
    static void play(const QString &filename, bool superpose = true);
    static void stop();

    static void playBGM(const QString &filename);
    static void setBGMVolume(float volume);
    static void stopBGM();

    static QString getVersion();

    // ── M2B-A 新增的觀測／設定接口 ──────────────────────────────────────
    // 目前生效的 backend 名（"fmod" / "qt" / "null"）。
    static QString backendName();
    static bool isInitialized();
    // 有冇真正可用的輸出裝置。冇裝置唔係錯誤，只係聽唔到聲。
    static bool hasOutputDevice();
    // 由 Config 讀 master／effect／voice／mute 並推落 backend。設定畫面按確定
    // 之後呼叫一次即可。
    static void applyConfigVolumes();
    // --multimedia-smoke 同 about dialog 用的結構化狀態。
    static QJsonObject diagnostics();
};

#endif
