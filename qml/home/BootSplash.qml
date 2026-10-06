import QtQuick
import QtQuick.Shapes

// Boot animation in its own window while the engine and the home pages load. Every
// motion is an Animator, so it keeps running on the render thread while the GUI thread
// is blocked. A SequentialAnimation would need the GUI thread to advance, so each start
// delay is part of the easing curve instead (see after()).
Rectangle {
    id: root

    signal outroFinished()

    readonly property real unit: Math.min(width, height) / 1080
    readonly property real ringRadius: 96 * unit
    readonly property real ringStroke: 15 * unit

    color: "#05070c"
    radius: Math.round(24 * unit)

    // BezierSpline control points that hold the start value until `delay` (a fraction
    // of the duration), then follow the cubic (x1, y1, x2, y2) for the rest.
    function after(delay, x1, y1, x2, y2) {
        var rest = 1 - delay
        return [delay / 3, 0, delay * 2 / 3, 0, delay, 0,
                delay + rest * x1, y1, delay + rest * x2, y2, 1, 1]
    }

    function playOutro() {
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

        // Gold glow behind the emblem; the gate fades it in, the inner shape breathes.
        Item {
            anchors.centerIn: emblem
            width: emblem.width * 3.4
            height: width
            opacity: 0

            OpacityAnimator on opacity {
                from: 0
                to: 1
                duration: 2000
                easing.type: Easing.BezierSpline
                easing.bezierCurve: root.after(0.4, 0.22, 0.61, 0.36, 1)
            }

            Shape {
                id: halo
                anchors.fill: parent

                OpacityAnimator on opacity {
                    from: 1
                    to: 0.55
                    duration: 3600
                    loops: Animation.Infinite
                    easing.type: Easing.SineCurve
                }

                ShapePath {
                    strokeWidth: -1
                    fillGradient: RadialGradient {
                        centerX: halo.width / 2
                        centerY: halo.height / 2
                        centerRadius: halo.width / 2
                        focalX: centerX
                        focalY: centerY
                        GradientStop { position: 0; color: "#60e6c179" }
                        GradientStop { position: 0.4; color: "#1ce6c179" }
                        GradientStop { position: 1; color: "#00e6c179" }
                    }
                    PathRectangle {
                        width: halo.width
                        height: halo.height
                    }
                }
            }
        }

        Item {
            id: emblem
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.round(parent.height / 2 - height * 0.8)
            width: (root.ringRadius + root.ringStroke) * 2
            height: width

            // Three kingdom arcs swing in around the centre, then drift slowly.
            Item {
                id: ring
                anchors.fill: parent

                RotationAnimator on rotation {
                    from: 0
                    to: 360
                    duration: 24000
                    loops: Animation.Infinite
                }

                Repeater {
                    model: [
                        { color: "#2f8fe0", centre: -90 },
                        { color: "#2bc25a", centre: 30 },
                        { color: "#e0303f", centre: 150 }
                    ]

                    Shape {
                        id: arc
                        required property var modelData
                        required property int index
                        readonly property int length: 1000
                        readonly property int delay: 150 + index * 120

                        anchors.fill: parent
                        preferredRendererType: Shape.CurveRenderer
                        opacity: 0
                        scale: 0.6
                        rotation: -140

                        OpacityAnimator on opacity {
                            from: 0
                            to: 1
                            duration: arc.delay + 400
                            easing.type: Easing.BezierSpline
                            easing.bezierCurve: root.after(arc.delay / (arc.delay + 400), 0.22, 0.61, 0.36, 1)
                        }
                        ScaleAnimator on scale {
                            from: 0.6
                            to: 1
                            duration: arc.delay + arc.length
                            easing.type: Easing.BezierSpline
                            easing.bezierCurve: root.after(arc.delay / (arc.delay + arc.length), 0.16, 1, 0.3, 1)
                        }
                        RotationAnimator on rotation {
                            from: -140
                            to: 0
                            duration: arc.delay + arc.length
                            easing.type: Easing.BezierSpline
                            easing.bezierCurve: root.after(arc.delay / (arc.delay + arc.length), 0.16, 1, 0.3, 1)
                        }

                        ShapePath {
                            strokeColor: arc.modelData.color
                            strokeWidth: root.ringStroke
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap

                            PathAngleArc {
                                centerX: arc.width / 2
                                centerY: arc.height / 2
                                radiusX: root.ringRadius
                                radiusY: root.ringRadius
                                startAngle: arc.modelData.centre - 48
                                sweepAngle: 96
                            }
                        }
                    }
                }
            }

            // A ripple leaves the ring as the core lands; the gate keeps it hidden until then.
            Item {
                anchors.fill: parent
                opacity: 0

                OpacityAnimator on opacity {
                    from: 0
                    to: 1
                    duration: 1000
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: root.after(0.95, 0.25, 0.25, 0.75, 0.75)
                }

                Rectangle {
                    id: ripple
                    anchors.centerIn: parent
                    width: root.ringRadius * 2
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.color: "#e6c179"
                    border.width: Math.max(1, 3 * root.unit)
                    opacity: 0.8

                    OpacityAnimator on opacity {
                        from: 0.8
                        to: 0
                        duration: 1900
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: root.after(0.5, 0.22, 0.61, 0.36, 1)
                    }
                    ScaleAnimator on scale {
                        from: 1
                        to: 2.2
                        duration: 1900
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: root.after(0.5, 0.16, 1, 0.3, 1)
                    }
                }
            }

            Rectangle {
                id: core
                anchors.centerIn: parent
                width: root.ringRadius * 1.24
                height: width
                radius: width / 2
                scale: 0
                gradient: Gradient {
                    GradientStop { position: 0; color: "#f7e0a3" }
                    GradientStop { position: 1; color: "#c4923f" }
                }

                ScaleAnimator on scale {
                    from: 0
                    to: 1
                    duration: 1300
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: root.after(0.5, 0.34, 1.56, 0.64, 1)
                }

                Rectangle {
                    x: parent.width * 0.2
                    y: parent.height * 0.14
                    width: parent.width * 0.4
                    height: parent.height * 0.24
                    radius: height / 2
                    rotation: -28
                    color: "#ffffff"
                    opacity: 0.3
                }
            }
        }

        Item {
            id: wordmark
            anchors.horizontalCenter: parent.horizontalCenter
            // Letter spacing also trails the last glyph; shift back to the optical centre.
            anchors.horizontalCenterOffset: title.font.letterSpacing / 2
            y: emblem.y + emblem.height + Math.round(52 * root.unit)
            width: title.implicitWidth
            height: title.implicitHeight
            opacity: 0

            OpacityAnimator on opacity {
                from: 0
                to: 1
                duration: 1800
                easing.type: Easing.BezierSpline
                easing.bezierCurve: root.after(1050 / 1800, 0.22, 0.61, 0.36, 1)
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
                    duration: 1800
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: root.after(1050 / 1800, 0.16, 1, 0.3, 1)
                }
            }
        }

        // Loading sweep; it runs from the start but only shows once the intro is done.
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            y: wordmark.y + wordmark.height + Math.round(30 * root.unit)
            width: Math.round(168 * root.unit)
            height: Math.max(2, Math.round(3 * root.unit))
            opacity: 0

            OpacityAnimator on opacity {
                from: 0
                to: 1
                duration: 2100
                easing.type: Easing.BezierSpline
                easing.bezierCurve: root.after(1600 / 2100, 0.22, 0.61, 0.36, 1)
            }

            Rectangle {
                anchors.fill: parent
                radius: height / 2
                color: "#14ffffff"
            }

            Item {
                id: track
                anchors.fill: parent
                clip: true

                Rectangle {
                    id: sweep
                    width: track.width * 0.4
                    height: track.height
                    x: -width
                    radius: height / 2
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: "#00e6c179" }
                        GradientStop { position: 0.5; color: "#ffe6c179" }
                        GradientStop { position: 1; color: "#00e6c179" }
                    }

                    XAnimator on x {
                        from: -sweep.width
                        to: track.width
                        duration: 1500
                        loops: Animation.Infinite
                        easing.type: Easing.InOutSine
                    }
                }
            }
        }
    }

    ParallelAnimation {
        id: outro

        ScaleAnimator {
            target: stage
            from: 1
            to: 1.08
            duration: 560
            easing.type: Easing.InCubic
        }
        OpacityAnimator {
            target: root
            from: 1
            to: 0
            duration: 560
            easing.type: Easing.InOutSine
        }

        onFinished: root.outroFinished()
    }
}
