import QtQuick
import QtQuick.Effects

// 斜切面板本體：只負責底板／邊線／陰影／可選上沿，不含文字或 icon。
Item {
    id: root

    property real slant: -0.12

    property color topColor: "#F3F8FD"
    property color bottomColor: "#DCEAF6"

    property color borderColor: "#8AB3CC"
    property real borderWidth: 1

    property color shadowColor: "#260A2A50"
    property real shadowBlur: 12
    property real shadowOffset: 5

    property real cornerRadius: 10

    property bool accentVisible: false
    property color accentColor: "#FFD84D"
    property real accentHeight: 2

    clip: false

    Item {
        id: plateHost

        anchors.fill: parent
        anchors.bottomMargin: root.shadowOffset

        transform: Shear {
            origin.x: plateHost.width / 2
            origin.y: plateHost.height / 2
            xFactor: root.slant
        }

        RectangularShadow {
            anchors.fill: plate
            offset.y: root.shadowOffset
            blur: root.shadowBlur
            color: root.shadowColor
            radius: root.cornerRadius
        }

        Rectangle {
            id: plate

            anchors.fill: parent
            radius: root.cornerRadius
            antialiasing: true

            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: root.topColor
                }

                GradientStop {
                    position: 1
                    color: root.bottomColor
                }
            }

            border.width: root.borderWidth
            border.color: root.borderColor
        }

        Rectangle {
            visible: root.accentVisible
            anchors.left: plate.left
            anchors.right: plate.right
            anchors.top: plate.top
            anchors.leftMargin: Math.max(8, root.cornerRadius)
            anchors.rightMargin: Math.max(8, root.cornerRadius)

            height: root.accentHeight
            radius: height / 2
            color: root.accentColor
        }
    }
}
