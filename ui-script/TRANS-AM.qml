import QtQuick 2.12

Rectangle {
    id: container
	color: "transparent"

    signal animationCompleted()

    //TRANS-AM animation
	Image {
		property int currentImage: 0
		id: transamImg
		source: "../image/animate/TRANS-AM/" + currentImage + ".jpg"
		x: 0
		width: sceneWidth
		height: sceneHeight
		z: 990.5
		visible: false
		fillMode: Image.PreserveAspectCrop
		opacity: 0
		NumberAnimation on currentImage {
			from: 0
			to: 8
			duration: 1000
        }
	}

	SequentialAnimation {
        id: transam
		running: false
        onStopped: {
            container.visible = false
            container.animationCompleted()
        }

		ParallelAnimation {
			ScriptAction {
				script: {
					transamImg.visible = true
				}					
			}
			
			PropertyAnimation {
				target: transamImg
				property: "opacity"
				to: 0.9
				duration: 200
			}
		}
		
		PauseAnimation { duration: 1500 }

    }
    Component.onCompleted: {
        var arr = hero.split(":")
        if (arr[0] == "TRANS-AM") {
        transam.running = true
        }
	}
}
