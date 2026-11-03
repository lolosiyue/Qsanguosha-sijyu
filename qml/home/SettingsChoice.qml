import QtQuick
import QtQuick.Layouts
import "."

// form is the hosting page: it provides compact, values, set() and reveal().
CardComboBox {
    id: choice
    required property var form
    required property string key
    required property var options
    Layout.preferredWidth: form.compact ? -1 : HomeTheme.settingsChoiceWidth
    Layout.fillWidth: form.compact
    implicitHeight: form.compact ? HomeTheme.compactTouch : HomeTheme.cardControlHeight
    font.pixelSize: HomeTheme.settingsFontSize
    textRole: "label"
    valueRole: "value"
    model: options
    currentIndex: {
        var current = form.values[key]
        for (var i = 0; i < options.length; ++i) {
            if (options[i].value === current)
                return i
        }
        return 0
    }
    onActivated: form.set(key, currentValue)
    onActiveFocusChanged: if (activeFocus) form.reveal(choice)
}
