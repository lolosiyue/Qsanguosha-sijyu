import QtQuick
import QtQuick.Controls
import "."

pragma ComponentBehavior: Bound

ComboBox {
    id: control

    implicitHeight: HomeTheme.cardControlHeight
    hoverEnabled: true
    font.pixelSize: HomeTheme.cardControlFontSize
    palette.text: HomeTheme.cardTextPrimary
    palette.buttonText: HomeTheme.cardTextPrimary
    property bool highContrast: homeController.visualMode === "highcontrast"
    property string accessibleLabel: ""
    property var tabTarget: null
    property var backtabTarget: null
    property var leftTarget: null
    property var rightTarget: null
    Accessible.name: accessibleLabel.length > 0
                     ? accessibleLabel + ": " + displayText
                     : displayText

    function moveFocus(target, event) {
        if (!target)
            return
        target.forceActiveFocus()
        event.accepted = true
    }

    Keys.onTabPressed: function(event) { moveFocus(tabTarget, event) }
    Keys.onBacktabPressed: function(event) { moveFocus(backtabTarget, event) }
    Keys.onLeftPressed: function(event) { moveFocus(leftTarget, event) }
    Keys.onRightPressed: function(event) { moveFocus(rightTarget, event) }

    delegate: ItemDelegate {
        id: optionDelegate

        required property int index
        required property var modelData

        width: control.width
        height: HomeTheme.cardControlHeight
        hoverEnabled: true
        highlighted: control.highlightedIndex === index
        text: control.textRole ? optionDelegate.modelData[control.textRole]
                               : optionDelegate.modelData

        contentItem: Text {
            text: optionDelegate.text
            color: optionDelegate.enabled ? HomeTheme.cardTextPrimary
                                          : HomeTheme.cardControlDisabledText
            font.pixelSize: HomeTheme.cardControlFontSize
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: HomeTheme.cardControlRadius
            color: !optionDelegate.enabled ? HomeTheme.cardControlDisabledFill
                 : optionDelegate.down ? HomeTheme.cardControlPressedFill
                 : optionDelegate.highlighted || optionDelegate.hovered
                   ? HomeTheme.cardControlHoverFill
                   : HomeTheme.cardPanelBottom
        }
    }

    indicator: Text {
        x: control.width - width - HomeTheme.cardControlHPadding
        anchors.verticalCenter: parent.verticalCenter
        text: "⌄"
        color: control.enabled ? HomeTheme.cardTextSecondary
                               : HomeTheme.cardControlDisabledText
        font.pixelSize: HomeTheme.cardBodyFontSize
        font.bold: true
    }

    contentItem: Text {
        leftPadding: HomeTheme.cardControlHPadding
        rightPadding: control.indicator.width + HomeTheme.cardControlHPadding * 2
        text: control.displayText
        color: control.enabled ? HomeTheme.cardTextPrimary
                               : HomeTheme.cardControlDisabledText
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: HomeTheme.cardControlRadius
        color: !control.enabled ? HomeTheme.cardControlDisabledFill
             : control.down ? HomeTheme.cardControlPressedFill
             : control.hovered ? HomeTheme.cardControlHoverFill
                               : HomeTheme.cardInputFill
        border.width: control.activeFocus
                      ? (control.highContrast ? HomeTheme.cardHighContrastFocusBorderWidth
                                              : HomeTheme.cardSelectedBorderWidth)
                                          : HomeTheme.cardBorderWidth
        border.color: control.activeFocus ? HomeTheme.focusBorderHigh
                     : control.hovered || control.down ? HomeTheme.cardInteractive
                                                       : HomeTheme.cardPanelBorder
        opacity: control.enabled ? 1.0 : HomeTheme.cardControlDisabledOpacity
    }

    popup: Popup {
        y: control.height + HomeTheme.cardPopupOffset
        width: control.width
        padding: HomeTheme.cardPopupPadding
        implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding

        contentItem: ListView {
            clip: true
            implicitHeight: Math.min(contentHeight, HomeTheme.cardPopupMaxHeight)
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: HomeScrollBar { policy: ScrollBar.AsNeeded }
        }

        background: Rectangle {
            radius: HomeTheme.cardControlRadius
            color: HomeTheme.cardPanelBottom
            border.width: HomeTheme.cardBorderWidth
            border.color: HomeTheme.cardPanelBorder
        }
    }
}
