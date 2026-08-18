import QtQuick
import QtQuick.Controls
import "."

AbstractButton {
    id: control

    property url iconSource: ""
    property bool highContrast: Config ? Config.getValue("VisualMode", "normal") === "highcontrast" : false

    implicitWidth: control.text.length > 0 ? 124 : 72
    implicitHeight: 56

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Keys.onShortcutOverride: function(event) {
        if (event.key === Qt.Key_Space)
            event.accepted = true
    }

    Accessible.role: Accessible.Button
    Accessible.name: control.text
    Accessible.description: control.text

    scale: control.down ? 0.90 : 1.0

    transform: Translate {
        x: control.hovered && !control.down ? 4 : 0

        Behavior on x {
            NumberAnimation {
                duration: 140
                easing.type: Easing.OutCubic
            }
        }
    }

    Behavior on scale {
        NumberAnimation {
            duration: 90
            easing.type: Easing.OutCubic
        }
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.margins: -4

            radius: 10
            color: "transparent"

            border.width: control.highContrast ? 3 : 2
            border.color: control.activeFocus
                          ? (control.highContrast ? HomeTheme.focusBorderHigh : HomeTheme.baFocusRing)
                          : "transparent"

            visible: control.activeFocus
        }

        BASlantedPanel {
            anchors.fill: parent

            slant: -0.10
            cornerRadius: 8
            shadowBlur: 8
            shadowOffset: 4
            borderWidth: control.activeFocus ? 2 : 1

            topColor: control.hovered ? HomeTheme.baWhite : HomeTheme.baToolTop
            bottomColor: HomeTheme.baToolBottom
            borderColor: HomeTheme.baDockBorder
            shadowColor: HomeTheme.baDockShadow
        }
    }

    contentItem: Item {
        Row {
            anchors.centerIn: parent
            spacing: 8

            Image {
                id: iconImage

                anchors.verticalCenter: parent.verticalCenter

                width: control.text.length > 0 ? 22 : 26
                height: width

                source: control.iconSource
                fillMode: Image.PreserveAspectFit
                mipmap: false
                visible: source.toString() !== "" && status !== Image.Error
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.text.length > 0

                text: control.text
                color: HomeTheme.baNavy
                font.pixelSize: control.highContrast ? 14 : 12
                font.weight: control.highContrast ? Font.Bold : Font.Medium
            }
        }
    }
}
