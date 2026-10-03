#ifndef _SETTINGS_SESSION_H
#define _SETTINGS_SESSION_H

#include <QObject>
#include <QVariantMap>

class QFont;
class QWidget;

// Shared settings backend for ConfigDialog and the home SettingsScene.qml.
// Values use Config/QSettings keys in three groups:
// Preview keys apply immediately and revert() restores the begin() snapshot.
// Draft keys remain in the session until commit().
// Immediate keys persist on selection and are not reverted, matching the legacy dialog.
class SettingsSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)

public:
    explicit SettingsSession(QObject *parent = nullptr);

    QVariantMap values() const { return m_values; }
    bool isActive() const { return m_active; }

    // Start editing from current settings, or reuse the draft while another settings view is open.
    Q_INVOKABLE void begin();
    Q_INVOKABLE QVariant value(const QString &key) const { return m_values.value(key); }
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void commit();
    Q_INVOKABLE void revert();

    // File, font and color pickers use the active window when parent is null.
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
