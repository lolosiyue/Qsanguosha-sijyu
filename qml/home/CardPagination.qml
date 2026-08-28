import QtQuick
import QtQuick.Layouts
import "."

Item {
    id: root

    property int pageIndex: 0
    property int pageCount: 1
    property alias previousButton: previousButton
    property alias nextButton: nextButton
    readonly property var firstEnabledButton: previousButton.enabled
                                               ? previousButton
                                               : (nextButton.enabled ? nextButton : null)
    readonly property var lastEnabledButton: nextButton.enabled
                                              ? nextButton
                                              : (previousButton.enabled ? previousButton : null)
    signal pageRequested(int pageIndex)

    RowLayout {
        anchors.fill: parent
        spacing: HomeTheme.cardSectionGap

        Item { Layout.fillWidth: true }

        BAToolButton {
            id: previousButton
            Layout.preferredWidth: HomeTheme.cardPaginationButtonWidth
            Layout.preferredHeight: HomeTheme.cardControlHeight
            text: "‹"
            enabled: root.pageIndex > 0
            Accessible.name: homeController.qtTranslate("CardScene", "Previous page")
            KeyNavigation.right: nextButton.enabled ? nextButton : null
            onClicked: root.pageRequested(root.pageIndex - 1)
        }

        Text {
            Layout.preferredWidth: HomeTheme.cardPaginationLabelWidth
            horizontalAlignment: Text.AlignHCenter
            text: homeController.qtTranslate("CardScene", "Page %1 of %2")
                  .arg(root.pageIndex + 1).arg(root.pageCount)
            color: HomeTheme.cardTextSecondary
            font.pixelSize: HomeTheme.cardPaginationFontSize
            font.bold: true
        }

        BAToolButton {
            id: nextButton
            Layout.preferredWidth: HomeTheme.cardPaginationButtonWidth
            Layout.preferredHeight: HomeTheme.cardControlHeight
            text: "›"
            enabled: root.pageIndex + 1 < root.pageCount
            Accessible.name: homeController.qtTranslate("CardScene", "Next page")
            KeyNavigation.left: previousButton.enabled ? previousButton : null
            onClicked: root.pageRequested(root.pageIndex + 1)
        }

        Item { Layout.fillWidth: true }
    }
}
