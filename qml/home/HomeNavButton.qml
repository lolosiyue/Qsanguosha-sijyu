import QtQuick
import QtQuick.Controls
import "."

AbstractButton {
    id: control

    property url iconSource: ""
    property url hoverIconSource: ""
    property url pressedIconSource: ""

    property bool active: false
    property real characterScale: 1.0
    property bool highContrast: Config.getValue("VisualMode", "normal") === "highcontrast"

    implicitWidth: 132
    implicitHeight: 104

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: control.text
    Accessible.description: control.text

    scale: control.activeFocus
           ? 1.05
           : down
             ? 0.94
             : hovered
               ? 1.045
               : 1.0

    Behavior on scale {
        NumberAnimation {
            duration: 110
            easing.type: Easing.OutCubic
        }
    }

    background: Item {
        // 鍵盤焦點外框
        Rectangle {
            anchors.fill: parent
            anchors.margins: -5

            radius: 22
            color: "transparent"

            border.width: 3
            border.color: control.activeFocus ? HomeTheme.focusBorderHigh : "transparent"

            visible: control.activeFocus

            Behavior on border.color {
                ColorAnimation {
                    duration: 120
                }
            }
        }

        // 選中項目後方的柔光底板
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4

            width: Math.min(parent.width - 10, 124)
            height: 76
            radius: 18

            visible: control.active
                     || control.hovered
                     || control.activeFocus

            color: control.down
                   ? HomeTheme.navBgDown
                   : control.active
                     ? HomeTheme.navBgActive
                     : HomeTheme.navBgHover

            border.width: control.activeFocus ? 2 : 1

            border.color: control.activeFocus
                          ? HomeTheme.navBorderFocus
                          : control.active
                            ? HomeTheme.navBorderActive
                            : HomeTheme.navBorderHover
        }

        // 選中時底部藍色發光線
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom

            width: control.active ? 62 : 0
            height: 3
            radius: 2

            color: HomeTheme.navLine
            visible: width > 0

            Behavior on width {
                NumberAnimation {
                    duration: 180
                    easing.type: Easing.OutCubic
                }
            }
        }
    }

    contentItem: Item {
        id: content

        // 選中或 hover 時整個內容稍微升高
        y: control.active
           ? -7
           : control.hovered
             ? -4
             : 0

        Behavior on y {
            NumberAnimation {
                duration: 150
                easing.type: Easing.OutCubic
            }
        }

        // 角色後方光圈
        Rectangle {
            id: outerGlow

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 1

            width: control.active ? 66 : 58
            height: width
            radius: width / 2

            color: control.active
                   ? HomeTheme.navGlowActive
                   : control.hovered
                     ? HomeTheme.navGlowHover
                     : "transparent"

            border.width: control.active ? 2 : 0
            border.color: HomeTheme.navLine

            opacity: control.down ? 0.65 : 1.0

            Behavior on width {
                NumberAnimation {
                    duration: 160
                    easing.type: Easing.OutCubic
                }
            }
        }

        // 第二層較小光圈
        Rectangle {
            anchors.centerIn: outerGlow

            width: outerGlow.width * 0.78
            height: width
            radius: width / 2

            visible: control.active || control.hovered
            color: HomeTheme.navGlowInner
        }

        Image {
            id: characterIcon

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: control.active ? -5 : 1

            width: control.active ? 68 : 60
            height: width

            source: {
                if (control.down
                        && control.pressedIconSource.toString().length > 0) {
                    return control.pressedIconSource
                }

                if ((control.hovered || control.active)
                        && control.hoverIconSource.toString().length > 0) {
                    return control.hoverIconSource
                }

                return control.iconSource
            }

            fillMode: Image.PreserveAspectFit
            asynchronous: true
            mipmap: true

            opacity: control.active
                     ? 1.0
                     : control.hovered
                       ? 0.95
                       : 0.76

            Behavior on width {
                NumberAnimation {
                    duration: 140
                    easing.type: Easing.OutBack
                }
            }

            Behavior on anchors.topMargin {
                NumberAnimation {
                    duration: 150
                    easing.type: Easing.OutCubic
                }
            }

            Behavior on opacity {
                NumberAnimation {
                    duration: 120
                }
            }
        }

        Text {
            id: label

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 5

            text: control.text

            color: control.activeFocus
                   ? HomeTheme.navTextFocus
                   : control.active
                     ? HomeTheme.navTextActive
                     : control.hovered
                       ? HomeTheme.navTextHover
                       : HomeTheme.navTextIdle

            font.pixelSize: control.highContrast
                            ? (control.active ? 19 : 18)
                            : (control.active ? 15 : 14)
            font.weight: control.highContrast
                         ? Font.Bold
                         : (control.active ? Font.DemiBold : Font.Medium)

            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter

            Behavior on color {
                ColorAnimation {
                    duration: 120
                }
            }
        }
    }
}
