import QtQuick
import QtQuick.Controls.Basic
import "."

ScrollBar {
    id: root

    policy: ScrollBar.AsNeeded
    implicitWidth: 8
    implicitHeight: 8
    minimumSize: 0.08

    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: 3
        color: HomeTheme.baDockBorder
        opacity: root.active || root.hovered ? 0.95 : 0.45
    }

    background: Item {}
}
