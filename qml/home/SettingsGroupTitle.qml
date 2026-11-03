import QtQuick
import QtQuick.Layouts
import "."

Text {
    Layout.fillWidth: true
    Layout.topMargin: HomeTheme.settingsRowGap
    Accessible.role: Accessible.Heading
    color: HomeTheme.cardAccent
    font.pixelSize: HomeTheme.settingsGroupFontSize
    font.bold: true
    wrapMode: Text.Wrap
}
