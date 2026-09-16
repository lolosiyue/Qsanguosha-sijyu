import QtQuick
import QtQuick.Layouts
import "."

RowLayout {
    id: root
    property int currentIndex: 0
    property int itemCount: -1
    property bool detailsEnabled: false
    property alias listButton: listButton
    property alias detailButton: detailButton
    property alias filterButton: filterButton
    signal activated(int index)
    spacing: HomeTheme.compactGap

    BAToolButton {
        id: listButton
        Layout.fillWidth: true
        implicitWidth: 0
        implicitHeight: HomeTheme.compactTouch
        text: root.itemCount >= 0 ? qsTr("一覽（%1）").arg(root.itemCount) : qsTr("一覽")
        opacity: root.currentIndex === 0 ? 1 : 0.65
        onClicked: root.activated(0)
    }
    BAToolButton {
        id: detailButton
        Layout.fillWidth: true
        implicitWidth: 0
        implicitHeight: HomeTheme.compactTouch
        text: qsTr("詳情")
        enabled: root.detailsEnabled
        opacity: root.currentIndex === 1 ? 1 : 0.65
        onClicked: root.activated(1)
    }
    BAToolButton {
        id: filterButton
        Layout.fillWidth: true
        implicitWidth: 0
        implicitHeight: HomeTheme.compactTouch
        text: qsTr("篩選")
        opacity: root.currentIndex === 2 ? 1 : 0.65
        onClicked: root.activated(2)
    }
}
