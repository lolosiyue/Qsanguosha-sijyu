import QtQuick 2.12
Rectangle {
    id: container
    color: "transparent"

    signal animationCompleted() 
    //?陝Notify憭抒?
	Image {
		id: heroCard
        opacity: 0
		rotation: 10
		height: 292
        scale: 1.5
        x: tableWidth / 2
        y: sceneHeight / 2 - 160
		z: 992
	}
	
	Image {
		property int currentImage: 0
		id: heroCardBg
		rotation: 10
		width: 412
		source: "../image/animate/mvp" + currentImage + ".png"
		x: heroCard.x - 121
        y: heroCard.y - 90
		scale: 1.5
		z: 991
		visible: false
		NumberAnimation on currentImage {
			from: 0
			to: 7
			loops: Animation.Infinite
			duration: 800
        }
	}
    Image {
		id: flicker_mask
        x: 0
        width: sceneWidth
        height: sceneHeight
		visible: false
		fillMode: Image.PreserveAspectCrop
		source: "../image/animate/flicker_mask.jpg"
		z: 990.5

	}
    Image {
		id: ssrText //mvpText
		source: "../image/animate/ssr.png" //mvp.png"
		scale: 1.6
        opacity: 0
        x: sceneWidth / 2 - 460
        y: sceneHeight / 2 - 260
		z: 1000
	}

	Image {
		id: ssrNew
		source: "../image/animate/ssrNew.png"
        opacity: 0
        x: tableWidth / 2 + 400
        y: sceneHeight / 8
		z: 1000
		visible: false
	}
     Image {
        id: heroPic
        x: -1000
        y: sceneHeight / 2 - 350
        fillMode: Image.PreserveAspectFit
        source: "../image/animate/" + hero + ".png"
        scale: 0.3
        z: 991
    }
    FontLoader { id: fixedFont; source: "../font/NZBZ.ttf" }

    Text {
        id: text
        color: "white"
        text: skill
        font.family: fixedFont.name//"LiSu"
        style: Text.Outline
        font.pointSize: 900
        opacity: 0
        z: 999
        x: sceneWidth / 2
        y: sceneHeight / 2
    }

    ParallelAnimation {
        id: ssrstep1 //mvpstep1
        running: false
		PropertyAnimation {
			target: heroCard
			property: "x"
			to: tableWidth / 2 + 400
			duration: 400
			easing.type: Easing.OutQuad
			easing.overshoot: 3
		}
		PropertyAnimation {
			target: heroCard
			property: "opacity"
			to: 1
			duration: 400
		}
        PropertyAnimation {
            target: ssrText //mvpText
            property: "opacity"
            to: 1
            duration: 400
        }
		PropertyAnimation {
            target: ssrNew
            property: "opacity"
            to: 1
            duration: 400
        }
        onStopped: {
			text.x = text.x - 150
			text.y = text.y + 150
            ssrstep2.start() //mvpstep2.start()
			heroCardBg.visible = true
        }
    }

    SequentialAnimation {
        id: ssrstep2 //mvpstep2
        onStopped: {
            container.visible = false
            container.animationCompleted()
        }

        ParallelAnimation {
			
            PropertyAnimation {
                target: text
                property: "opacity"
                to: 1.0
                duration: 500
            }
            PropertyAnimation {
                target: text
                property: "font.pointSize"
                to: 90
                duration: 500
            }
        }

        PauseAnimation { duration: 2000 } //3000

    }
    Component.onCompleted: {
		var arr = hero.split(":")
		if (arr[0] == "ssr") {
            flicker_mask.source = "../image/animate/ssrBg.jpg"
            flicker_mask.visible = true
			heroCard.source = "../image/generals/card/" + arr[1] + ".jpg"
			if (arr[2] == "new") {
				ssrNew.visible = true
			}
			//mask_click.enabled = true
            ssrstep1.running = true
        }
    }
}
