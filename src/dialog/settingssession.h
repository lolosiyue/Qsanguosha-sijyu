#ifndef _SETTINGS_SESSION_H
#define _SETTINGS_SESSION_H

#include <QObject>
#include <QVariantMap>

class QFont;
class QWidget;

// 設定頁的共用後端:ConfigDialog(舊版 widget)與首頁 SettingsScene.qml 都只是它的外觀。
// 值以 Config / QSettings 的鍵名存取,分三類:
//   預覽鍵  —— 變動立即套用(主題、縮放、背景、動畫…),revert() 復原為 begin() 時的快照;
//   草稿鍵  —— 只存在 session 中(音量、遊戲選項…),commit() 才寫入;
//   即時鍵  —— 背景音樂、字型、文字顏色,選定即寫入且不參與復原(沿用舊 dialog 行為)。
class SettingsSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)

public:
    explicit SettingsSession(QObject *parent = nullptr);

    QVariantMap values() const { return m_values; }
    bool isActive() const { return m_active; }

    // 開始一次編輯:讀取目前設定並拍快照。已在編輯中(另一個外觀開著)則沿用同一份草稿。
    Q_INVOKABLE void begin();
    Q_INVOKABLE QVariant value(const QString &key) const { return m_values.value(key); }
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void commit();
    Q_INVOKABLE void revert();

    // 檔案/字型/顏色選擇器;parent 為空時以目前作用中的視窗為父。
    Q_INVOKABLE void chooseBackgroundImage(QWidget *parent = nullptr);
    Q_INVOKABLE void resetBackgroundImage();
    Q_INVOKABLE void choosePortraitBackground(QWidget *parent = nullptr);
    Q_INVOKABLE void resetPortraitBackground();
    Q_INVOKABLE void chooseBackgroundMusic(QWidget *parent = nullptr);
    Q_INVOKABLE void resetBackgroundMusic();
    Q_INVOKABLE void chooseAppFont(QWidget *parent = nullptr);
    Q_INVOKABLE void chooseTextEditFont(QWidget *parent = nullptr);
    Q_INVOKABLE void chooseTextEditColor(QWidget *parent = nullptr);

    static QString fontLabel(const QFont &font);

signals:
    void valuesChanged();
    void activeChanged();
    void backgroundChanged();
    void themeChanged();
    void visualModeChanged();
    void uiScaleChanged(qreal scale);
    void committed();

private:
    void load();
    void setActive(bool active);
    void applyPreview(const QString &key, const QVariant &value);
    void updateValue(const QString &key, const QVariant &value);

    QVariantMap m_values;
    QVariantMap m_snapshot;
    bool m_active = false;
};

#endif
