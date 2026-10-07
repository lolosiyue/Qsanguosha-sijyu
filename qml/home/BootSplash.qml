import QtQuick
import QtQuick.Shapes
import QSanguosha.Boot 1.0

// Boot animation in its own window while the engine and the home pages load: a clip
// from video/boot, or, when there is none, a drawn Big Bang that settles into a black
// hole (shaders/boot-cosmos.frag). Every motion is an Animator,
// so it keeps running on the render thread while the GUI thread is blocked. A
// SequentialAnimation would need the GUI thread to advance, so each start delay is part
// of the easing curve instead (see after()).
Rectangle {
    id: root

    signal outroFinished()

    // Set by BootSplash.
    property bool videoMode: false
    property real progress: 0
    property int outroMs: 560

    readonly property real unit: Math.min(width, height) / 1080

    color: "#05070c"
    radius: videoMode ? 0 : Math.round(24 * unit)

    // BezierSpline control points that hold the start value until `delay` (a fraction
    // of the duration), then follow the cubic (x1, y1, x2, y2) for the rest.
    function after(delay, x1, y1, x2, y2) {
        var rest = 1 - delay
        return [delay / 3, 0, delay * 2 / 3, 0, delay, 0,
                delay + rest * x1, y1, delay + rest * x2, y2, 1, 1]
    }

    function playOutro(duration) {
        outroMs = duration
        outro.start()
    }

    Item {
        anchors.fill: parent
        opacity: 0

        OpacityAnimator on opacity {
            from: 0
            to: 1
            duration: 1400
            easing.type: Easing.OutCubic
        }

        Shape {
            id: backdrop
            anchors.fill: parent

            ShapePath {
                strokeWidth: -1
                fillGradient: RadialGradient {
                    centerX: backdrop.width / 2
                    centerY: backdrop.height / 2
                    centerRadius: Math.max(backdrop.width, backdrop.height) * 0.62
                    focalX: centerX
                    focalY: centerY
                    GradientStop { position: 0; color: "#122038" }
                    GradientStop { position: 1; color: "#05070c" }
                }
                PathRectangle {
                    width: backdrop.width
                    height: backdrop.height
                    radius: root.radius
                }
            }
        }
    }

    Loader {
        anchors.fill: parent
        active: root.videoMode
        sourceComponent: Component {
            BootVideo { }
        }
    }

    Rectangle {
        anchors.fill: parent
        z: 1
        radius: root.radius
        color: "transparent"
        border.color: "#26ffffff"
        border.width: 1
    }

    Item {
        id: stage
        anchors.fill: parent
        visible: !root.videoMode

        BootCosmos {
            anchors.fill: parent
            cornerRadius: root.radius
        }

        Item {
            id: wordmark
            anchors.horizontalCenter: parent.horizontalCenter
            // Letter spacing also trails the last glyph; shift back to the optical centre.
            anchors.horizontalCenterOffset: title.font.letterSpacing / 2
            // Below the accretion disk, once the black hole has formed.
            y: Math.round(parent.height * 0.73)
            width: title.implicitWidth
            height: title.implicitHeight
            opacity: 0

            OpacityAnimator on opacity {
                from: 0
                to: 1
                duration: 4400
                easing.type: Easing.BezierSpline
                easing.bezierCurve: root.after(3600 / 4400, 0.22, 0.61, 0.36, 1)
            }

            Text {
                id: title
                y: 18 * root.unit
                text: qsTranslate("MainWindow", "Sanguosha")
                color: "#efe7d4"
                font.pixelSize: Math.max(12, Math.round(44 * root.unit))
                font.letterSpacing: 16 * root.unit
                font.weight: Font.DemiBold

                YAnimator on y {
                    from: 18 * root.unit
                    to: 0
                    duration: 4400
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: root.after(3600 / 4400, 0.16, 1, 0.3, 1)
                }
            }
        }
    }

    // Keeps the progress bar readable over a clip.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.round(120 * root.unit)
        visible: root.videoMode
        gradient: Gradient {
            GradientStop { position: 0; color: "#00000000" }
            GradientStop { position: 1; color: "#a0000000" }
        }
    }

    // Real progress: BootSplash moves it through the engine load and the catalog pages.
    // The sweep never stops, which also keeps the window drawing new clip frames.
    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Math.round(48 * root.unit)
        anchors.rightMargin: Math.round(48 * root.unit)
        anchors.bottomMargin: Math.round(36 * root.unit)
        height: Math.max(2, Math.round(4 * root.unit))
        opacity: 0

        OpacityAnimator on opacity {
            from: 0
            to: 1
            duration: 1000
            easing.type: Easing.BezierSpline
            easing.bezierCurve: root.after(0.4, 0.22, 0.61, 0.36, 1)
        }

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: "#1fffffff"
        }

        Item {
            id: track
            anchors.fill: parent
            clip: true

            Rectangle {
                width: track.width
                height: track.height
                x: -width * (1 - root.progress)
                radius: height / 2
                color: "#c79a4d"

                Behavior on x {
                    XAnimator {
                        duration: 450
                        easing.type: Easing.OutCubic
                    }
                }
            }

            Rectangle {
                id: sweep
                width: track.width * 0.25
                height: track.height
                x: -width
                radius: height / 2
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "#00fff1cf" }
                    GradientStop { position: 0.5; color: "#ccfff1cf" }
                    GradientStop { position: 1; color: "#00fff1cf" }
                }

                XAnimator on x {
                    from: -sweep.width
                    to: track.width
                    duration: 1800
                    loops: Animation.Infinite
                    easing.type: Easing.InOutSine
                }
            }
        }
    }

    ParallelAnimation {
        id: outro

        // Into the black hole.
        ScaleAnimator {
            target: stage
            from: 1
            to: 1.25
            duration: root.outroMs
            easing.type: Easing.InCubic
        }
        OpacityAnimator {
            target: root
            from: 1
            to: 0
            duration: root.outroMs
            easing.type: Easing.InOutSine
        }

        onFinished: root.outroFinished()
    }
}
