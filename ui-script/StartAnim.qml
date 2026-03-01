import QtQuick 2.12

Image {
	id: gamestartImg
    property int currentImage: 0
    source: "../image/animate/util/gamestart/" + currentImage + ".png"
    x: sceneWidth * 0.5 - width * 0.5
    y: sceneHeight * 0.5 - height * 0.5 - 100
	scale: 1.5
	z: 991
    NumberAnimation on currentImage {
        id: gamestartAnim
	    running: false
		to: 21
		duration: 1500
        onStopped: {
            gamestartImg.visible = false
			container.visible = false
			container.animationCompleted()
        }
    }

    Component.onCompleted: {
        gamestartAnim.start()
    }
}
