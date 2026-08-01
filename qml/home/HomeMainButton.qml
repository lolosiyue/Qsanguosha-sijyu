import QtQuick
import QtQuick.Controls
import "."

AbstractButton {
    id: control

    property url iconSource: ""
    property bool primary: false
    property bool highContrast: Config.getValue("VisualMode", "normal") === "highcontrast"

    implicitWidth: 540
    implicitHeight: 112

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: control.text
    Accessible.description: control.text

    scale: control.activeFocus
           ? 1.03
           : down ? 0.975
                  : hovered ? 1.025
                            : 1.0

    x: hovered && !down ? 6 : 0

    Behavior on scale {
        NumberAnimation {
            duration: 110
            easing.type: Easing.OutCubic
        }
    }

    Behavior on x {
        NumberAnimation {
            duration: 130
            easing.type: Easing.OutCubic
        }
    }

    background: Item {
        // 鍵盤焦點外光環
        Rectangle {
            anchors.fill: parent
            anchors.margins: -6

            radius: height / 2
            color: "transparent"

            border.width: control.activeFocus ? (control.highContrast ? 7 : 5) : 0
            border.color: control.activeFocus
                          ? (control.highContrast ? HomeTheme.focusBorderHigh : HomeTheme.focusBorder)
                          : "transparent"

            visible: control.activeFocus

            Behavior on border.color {
                ColorAnimation {
                    duration: 120
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 8

            radius: height / 2
            color: HomeTheme.btnShadow
        }

        Rectangle {
            anchors.fill: parent

            radius: height / 2

            color: control.primary
                   ? (control.down ? HomeTheme.btnPrimaryDown : HomeTheme.btnPrimary)
                   : (control.down ? HomeTheme.btnSecondaryDown : HomeTheme.btnSecondary)

            border.width: control.activeFocus ? 4
                         : control.hovered ? 3
                                           : 2
            border.color: control.activeFocus
                          ? HomeTheme.focusBorderHigh
                          : control.primary ? HomeTheme.btnPrimaryBorder : HomeTheme.btnSecondaryBorder
        }
    }

    contentItem: Item {
        Rectangle {
            id: iconCircle

            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter

            width: control.height - 20
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
            anchors.leftMargin: 28
            anchors.right: parent.right
            anchors.rightMargin: 38
            anchors.verticalCenter: parent.verticalCenter

            text: control.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter

            color: control.primary ? HomeTheme.btnPrimaryText : HomeTheme.btnSecondaryText
            font.pixelSize: control.highContrast
                            ? Math.max(28, control.height * 0.37)
                            : Math.max(22, control.height * 0.29)
            font.weight: control.highContrast ? Font.Bold : Font.DemiBold
        }
    }
}
