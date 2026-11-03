import QtQuick
import "."

// A checkbox bound to one key of form.values; edits go through form.set().
SettingsCheckBox {
    required property string key
    checked: form.values[key] === true
    onToggled: form.set(key, checked)
}
