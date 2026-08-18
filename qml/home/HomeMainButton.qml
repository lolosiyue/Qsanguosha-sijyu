import QtQuick
import QtQuick.Controls
import "."

AbstractButton {
    id: control

    property url iconSource: ""
    property bool primary: false
    property bool highContrast: Config ? Config.getValue("VisualMode", "normal") === "highcontrast" : false

    implicitWidth: control.primary ? 470 : 440
    implicitHeight: control.primary ? 80 : 76

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: control.text
    Accessible.description: control.text

    scale: control.down ? 0.91 : 1.0

    transform: Translate {
        y: control.hovered && !control.down ? -3 : 0

        Behavior on y {
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
            anchors.margins: -6

            radius: 12
            color: "transparent"

            border.width: control.activeFocus ? (control.highContrast ? 4 : 2) : 0
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
            anchors.fill: parent

            slant: -0.12
            cornerRadius: 10
            shadowBlur: 12
            shadowOffset: 5
            borderWidth: control.activeFocus ? 2 : 1

            topColor: control.primary
                      ? HomeTheme.baPrimaryTop
                      : HomeTheme.baSecondaryTop
            bottomColor: control.primary
                         ? (control.down ? HomeTheme.btnPrimaryDown : HomeTheme.baPrimaryBottom)
                         : (control.down ? HomeTheme.btnSecondaryDown : HomeTheme.baSecondaryBottom)
            borderColor: control.primary
                         ? HomeTheme.btnPrimaryBorder
                         : HomeTheme.baDockBorder
            shadowColor: HomeTheme.baDockShadow

            accentVisible: control.primary
            accentColor: HomeTheme.baYellow
            accentHeight: 3
        }
    }

    contentItem: Item {
        Rectangle {
            id: iconCircle

            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter

            width: control.height - 28
            height: width
            radius: width / 2

            color: control.primary ? HomeTheme.btnPrimaryIconBg : HomeTheme.btnSecondaryIconBg

            border.width: 2
            border.color: control.primary ? HomeTheme.btnPrimaryIconBdr : HomeTheme.btnSecondaryIconBdr

            Image {
                anchors.centerIn: parent

                width: parent.width * 0.5
                height: width

                source: control.iconSource
                fillMode: Image.PreserveAspectFit
                mipmap: true
            }
        }

        Text {
            id: label

            anchors.left: iconCircle.right
            anchors.leftMargin: 20
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter

            text: control.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter

            color: control.primary ? HomeTheme.btnPrimaryText : HomeTheme.baNavy
            font.pixelSize: control.highContrast
                            ? Math.max(22, control.height * 0.32)
                            : Math.max(20, control.height * 0.28)
            font.weight: control.highContrast ? Font.Bold : Font.DemiBold
        }
    }
}
