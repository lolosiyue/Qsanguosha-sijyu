import QtQuick

Item {
    id: root

    // Extend the artwork below the window by half its height so the enlarged character tucks behind the bottom dock.
    property real baseBottomMargin: -parent.height * 0.5
    property bool compact: false

    Image {
        id: character

        anchors.left: parent.left
        anchors.leftMargin: root.compact ? -parent.width * 0.15 : 0
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.baseBottomMargin

        width: parent.width * (root.compact ? 1.3 : 1.1)
        height: parent.height * (root.compact ? 1.1 : 1.42)

        source: homeController.characterImage
        fillMode: Image.PreserveAspectFit
        horizontalAlignment: root.compact ? Image.AlignHCenter : Image.AlignLeft
        verticalAlignment: Image.AlignBottom
        visible: source.toString() !== "" && status === Image.Ready

        mipmap: false
        asynchronous: true
        cache: true

        onStatusChanged: {
            if (status === Image.Error)
                console.error("Character load failed:", source)
        }
    }
}
