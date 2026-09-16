#ifndef ROOM_WINDOW_POSTURE_H
#define ROOM_WINDOW_POSTURE_H

#include <QObject>
#include <QRectF>

class RoomWindowPosture : public QObject
{
    Q_OBJECT
public:
    enum class Mode {
        None,
        Book,
        Tabletop
    };
    Q_ENUM(Mode)

    struct Value {
        Mode mode = Mode::None;
        QRectF bounds;
        bool separating = false;
        bool occluding = false;

        bool operator==(const Value &other) const
        {
            return mode == other.mode && bounds == other.bounds
                && separating == other.separating && occluding == other.occluding;
        }
        bool operator!=(const Value &other) const { return !(*this == other); }
    };
    explicit RoomWindowPosture(QObject *parent = nullptr);
    ~RoomWindowPosture() override;

    Value value() const { return m_value; }

    // Test/emulator seam. Android bounds are window-relative Qt logical units;
    // consumers translate them into viewport/scene coordinates as needed.
    // Desktop launchers may also set QSAN_WINDOW_POSTURE as
    // mode,left,top,right,bottom,separating,occluding (booleans: 0/1 or true/false).
    void inject(Mode mode, const QRectF &bounds = QRectF(),
                bool separating = false, bool occluding = false);
    void clearInjection();
    void setResponsivePreview(bool enabled);

    // Called by the Android adapter after its UI-thread callback is marshalled
    // back to the Qt object thread. Ignored while an explicit injection is set.
    void updateFromPlatform(int generation, const Value &value);

signals:
    void postureChanged(const RoomWindowPosture::Value &value);

private:
    void setValue(const Value &value);
    void startPlatformListener();
    void stopPlatformListener();
    bool loadEnvironmentInjection();

    Value m_value;
    bool m_injected = false;
    int m_platformGeneration = 0;
};

Q_DECLARE_METATYPE(RoomWindowPosture::Value)

#endif // ROOM_WINDOW_POSTURE_H
