import QtQuick
import QtQuick.Controls
import "."

AbstractButton {
    id: control

    property url iconSource: ""
    property string leadingText: ""
    property bool primary: false
    property bool compact: false
    property bool tile: false
    property bool highContrast: homeController && homeController.visualMode === "highcontrast"

    implicitWidth: control.primary ? 470 : 440
    implicitHeight: control.primary ? 80 : 76

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Keys.onShortcutOverride: function(event) {
        if (event.key === Qt.Key_Space)
            event.accepted = true
    }

    Accessible.role: Accessible.Button
    Accessible.name: control.leadingText !== ""
                     ? (control.leadingText + " " + control.text)
                     : control.text
    Accessible.description: Accessible.name

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
            anchors.leftMargin: control.tile ? 12 : 14
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: control.tile ? -17 : 0

            width: control.tile ? 28 : control.height - 28
            height: width
            radius: width / 2

            color: control.primary ? HomeTheme.btnPrimaryIconBg : HomeTheme.btnSecondaryIconBg

            border.width: 2
            border.color: control.primary ? HomeTheme.btnPrimaryIconBdr : HomeTheme.btnSecondaryIconBdr

            Image {
                anchors.centerIn: parent

                width: parent.width * (control.tile ? 0.8 : 0.5)
                height: width

                source: control.iconSource
                fillMode: Image.PreserveAspectFit
                mipmap: false
            }
        }

        Text {
            id: leadingLabel

            visible: control.leadingText !== ""
            // Stack the mode above the action on narrow screens; keep the original icon.
            width: visible ? (control.compact ? Math.max(0, control.width - iconCircle.width - (control.tile ? 32 : 58))
                                              : Math.min(168, implicitWidth)) : 0

            anchors.left: iconCircle.right
            anchors.leftMargin: visible ? 12 : 0
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: control.compact ? -16 : 0

            text: control.leadingText
            elide: Text.ElideRight
            color: control.primary ? HomeTheme.btnPrimaryText : HomeTheme.baNavy
            opacity: 0.82
            font.pixelSize: control.tile ? 14 : control.highContrast
                            ? Math.max(16, control.height * 0.24)
                            : Math.max(15, control.height * 0.22)
            font.weight: Font.DemiBold
        }

        Rectangle {
            id: leadingDivider

            visible: leadingLabel.visible && !control.compact
            width: 1
            height: Math.max(18, control.height * 0.36)

            anchors.left: leadingLabel.right
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter

            color: control.primary ? HomeTheme.btnPrimaryText : HomeTheme.baNavy
            opacity: 0.28
        }

        Text {
            id: label

            anchors.left: control.tile ? parent.left : (leadingDivider.visible ? leadingDivider.right : iconCircle.right)
            anchors.leftMargin: control.tile ? 8 : (leadingDivider.visible ? 12 : 20)
            anchors.right: parent.right
            anchors.rightMargin: control.tile ? 8 : 24
            anchors.verticalCenter: parent.verticalCenter

            text: control.text
            anchors.verticalCenterOffset: control.tile || (control.compact && leadingLabel.visible) ? 16 : 0
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter

            color: control.primary ? HomeTheme.btnPrimaryText : HomeTheme.baNavy
            font.pixelSize: control.tile ? 18 : control.highContrast
                            ? Math.max(22, control.height * 0.32)
                            : Math.max(20, control.height * 0.28)
            font.weight: control.highContrast ? Font.Bold : Font.DemiBold
        }
    }
}
