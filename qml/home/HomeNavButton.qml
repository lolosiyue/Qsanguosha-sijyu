import QtQuick
import QtQuick.Controls
import "."

AbstractButton {
    id: control

    property url iconSource: ""
    property url hoverIconSource: ""
    property url pressedIconSource: ""
    property url characterSource: ""

    property bool active: false
    property real characterScale: 1.0
    property bool highContrast: Config ? Config.getValue("VisualMode", "normal") === "highcontrast" : false

    implicitWidth: 160
    implicitHeight: 136

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Keys.onShortcutOverride: function(event) {
        if (event.key === Qt.Key_Space)
            event.accepted = true
    }

    Accessible.role: Accessible.Button
    Accessible.name: control.text
    Accessible.description: control.text

    scale: control.down ? 0.92 : 1.0

    Behavior on scale {
        NumberAnimation {
            duration: 90
            easing.type: Easing.OutCubic
        }
    }

    readonly property url resolvedIcon: {
        if (control.down && control.pressedIconSource.toString().length > 0)
            return control.pressedIconSource
        if ((control.hovered || control.active)
                && control.hoverIconSource.toString().length > 0)
            return control.hoverIconSource
        if (characterProbe.status === Image.Ready)
            return control.characterSource
        return control.iconSource
    }

    Image {
        id: characterProbe
        source: control.characterSource
        visible: false
        asynchronous: true
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.margins: -5

            radius: 12
            color: "transparent"

            border.width: control.highContrast ? 3 : 2
            border.color: control.activeFocus
                          ? (control.highContrast ? HomeTheme.focusBorderHigh : HomeTheme.baFocusRing)
                          : "transparent"

            visible: control.activeFocus

            Behavior on border.color {
                ColorAnimation {
                    duration: 120
                }
            }
        }

        BASlantedPanel {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6

            width: Math.min(parent.width - 12, 148)
            height: 72

            visible: control.active || control.hovered || control.activeFocus

            slant: -0.12
            cornerRadius: 8
            shadowOffset: 4
            shadowBlur: 8
            borderWidth: control.activeFocus ? 2 : 1

            topColor: control.down
                      ? HomeTheme.navBgDown
                      : control.active
                        ? HomeTheme.baNavBgActive
                        : HomeTheme.baNavBgHover
            bottomColor: topColor
            borderColor: control.activeFocus
                         ? HomeTheme.navBorderFocus
                         : control.active
                           ? HomeTheme.navBorderActive
                           : HomeTheme.navBorderHover
            shadowColor: HomeTheme.baDockShadow
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4

            width: control.active ? 48 : 0
            height: 3
            radius: 1.5

            color: HomeTheme.baYellow
            visible: width > 0

            Behavior on width {
                NumberAnimation {
                    duration: 170
                    easing.type: Easing.OutCubic
                }
            }
        }
    }

    contentItem: Item {
        id: content

        y: control.down
           ? 3
           : control.active
             ? -8
             : control.hovered
               ? -4
               : 0

        Behavior on y {
            NumberAnimation {
                duration: control.down ? 90 : (control.active ? 170 : 140)
                easing.type: Easing.OutCubic
            }
        }

        Rectangle {
            id: outerGlow

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 2

            width: control.active ? 96 : (control.hovered ? 88 : 80)
            height: width
            radius: width / 2

            color: control.active
                   ? HomeTheme.baHaloOuter
                   : control.hovered
                     ? HomeTheme.navGlowHover
                     : "transparent"

            opacity: control.down ? 0.7 : 1.0

            Behavior on width {
                NumberAnimation {
                    duration: 160
                    easing.type: Easing.OutCubic
                }
            }
        }

        Rectangle {
            anchors.centerIn: outerGlow

            width: outerGlow.width * 0.72
            height: width
            radius: width / 2

            visible: control.active || control.hovered
            color: HomeTheme.baHaloInner
        }

        Image {
            id: characterIcon

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 0

            width: 105 * control.characterScale
            height: width

            source: control.resolvedIcon
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            mipmap: false
            visible: source.toString() !== "" && status !== Image.Error

            scale: control.active
                   ? 1.09
                   : control.hovered
                     ? 1.05
                     : 1.0

            opacity: control.active
                     ? 1.0
                     : control.hovered
                       ? 0.95
                       : 0.82

            Behavior on scale {
                NumberAnimation {
                    duration: 170
                    easing.type: Easing.OutBack
                }
            }

            Behavior on opacity {
                NumberAnimation {
                    duration: 140
                    easing.type: Easing.OutCubic
                }
            }
        }

        Text {
            id: label

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8

            text: control.text

            color: control.activeFocus
                   ? HomeTheme.navTextFocus
                   : control.active
                     ? HomeTheme.baNavTextActive
                     : control.hovered
                       ? HomeTheme.baNavTextHover
                       : HomeTheme.baNavTextIdle

            font.pixelSize: control.highContrast
                            ? (control.active ? 18 : 17)
                            : (control.active ? 16 : 15)
            font.weight: control.highContrast
                         ? Font.Bold
                         : (control.active ? Font.DemiBold : Font.Medium)

            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter

            Behavior on color {
                ColorAnimation {
                    duration: 140
                }
            }
        }
    }
}
