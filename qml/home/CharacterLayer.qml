import QtQuick

Item {
    id: root

    // 底緣向下延伸 0.5 倍視窗高：放大角色並讓下半身疊入底部導覽列被遮蓋
    property real baseBottomMargin: -parent.height * 0.5

    Image {
        id: character

        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.baseBottomMargin

        width: parent.width * 1.1
        height: parent.height * 1.42

        source: homeController.characterImage
        fillMode: Image.PreserveAspectFit
        horizontalAlignment: Image.AlignLeft
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
