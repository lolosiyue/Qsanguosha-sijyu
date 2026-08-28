#ifndef EFFECTS_PROFILE_H
#define EFFECTS_PROFILE_H

#include <QString>
#include <QStringList>
#include <QVariant>

// Linux GUI M2B-B 的效果 profile 契約。
//
// 呢個 header 只依賴 Qt Core：profile 名稱、feature gate、duration scale、CLI／
// settings 解析全部係純函數，可以喺 CTest 直接驗，唔使開 QApplication、唔使有
// OpenGL、亦唔使有任何美術資產。真正攞 Config 同記數的 runtime 門面喺
// VisualEffectsPolicy（effects-policy.h）。
//
// 三個 profile 只影響「睇到啲乜」，唔會影響遊戲規則同網絡回覆：
//
//   Full     完整動畫 + Spine + GIF + QML 特效 + 影片
//   Reduced  保留必要狀態提示，縮短動畫，停用 Spine／影片／QML 全屏特效
//   None     所有裝飾動畫即時完成，唔建立 Spine／QMovie／video object
enum class EffectsProfile {
    Full,
    Reduced,
    None
};

class EffectsProfileContract
{
public:
    // 使用者設定同測試 CLI 行同一條 policy，所以 key 同 flag 都喺呢度定一次。
    static const char *const SettingsKey;         // "EffectsProfile"
    static const char *const FlagEffectsProfile;  // "--effects-profile"

    static EffectsProfile defaultProfile();       // Full

    static QString profileName(EffectsProfile profile);
    static QStringList profileNames();            // {"full","reduced","none"}
    // 大細寫唔敏感，前後空白會 trim；空字串當「冇指定」，唔算錯。
    static bool parseProfileName(const QString &text, EffectsProfile *profile);

    // ── Feature gate ────────────────────────────────────────────────────
    // 全部只可以「收窄」：profile 永遠唔會開啟使用者喺設定關咗嘅嘢，
    // 呢層邏輯喺 VisualEffectsPolicy 度同 Config 夾埋。
    static bool animationsEnabled(EffectsProfile profile);
    static bool spineEnabled(EffectsProfile profile);
    static bool gifEnabled(EffectsProfile profile);
    static bool videoEnabled(EffectsProfile profile);
    static bool qmlEffectsEnabled(EffectsProfile profile);
    // 純裝飾性嘅等待（例如「等 444ms 先播拼點結果」）。Reduced 會縮短，
    // None 一律 0：遊戲狀態要即刻到達最終位置。
    static bool decorativeDelayAllowed(EffectsProfile profile);
    // 選中框、傷害、卡牌移動呢類「你而家要做緊乜」嘅視覺回饋。Reduced 一定
    // 保留，否則玩家會睇唔到 pending action。
    static bool stateFeedbackEnabled(EffectsProfile profile);

    // ── Duration ────────────────────────────────────────────────────────
    static qreal durationScale(EffectsProfile profile);   // 1.0 / 0.3 / 0.0
    // scale 之後嘅毫秒數。Reduced 唔會壓到 0（0 會令 Qt 喺 start() 入面同步
    // 派 finished()，製造重入）；None 一定係 0，但 None 嘅 call site 應該直接
    // 唔起動畫，用 EffectsCompletion::completeNow() 收工。
    static int scaledDuration(EffectsProfile profile, int durationMs);

    // ── CLI ─────────────────────────────────────────────────────────────
    // 同時接受 "--effects-profile none" 同 "--effects-profile=none"。
    struct CliOverride {
        bool present = false;
        bool valid = false;
        EffectsProfile profile = EffectsProfile::Full;
        QString value;
        QString error;
    };
    static CliOverride parseCliOverride(const QStringList &arguments);

    // ── Resolution ──────────────────────────────────────────────────────
    // 優先次序：CLI override > 使用者設定 > 預設。CLI 值唔合法會退返落設定，
    // 並且喺 error 度講明點解 —— 靜靜地當冇指定係最難查嘅一種。
    struct Resolution {
        EffectsProfile profile = EffectsProfile::Full;
        QString source;   // "cli" / "settings" / "default"
        QString error;    // 非空 = CLI 或設定值唔合法（已經退返落次一級）
    };
    static Resolution resolve(const QStringList &arguments, const QVariant &settingsValue);
};

#endif
