import QtQuick 2.12
Rectangle {
    id: container
	color: "transparent"

    signal animationCompleted()    
    Image {
		id: flicker_mask
        x: 0
        width: sceneWidth
        height: sceneHeight
		visible: false
		fillMode: Image.PreserveAspectCrop
		source: "../image/animate/flicker_mask.jpg"
		z: 990.5
		
		MouseArea {
			id: mask_click
			enabled: false
			anchors.fill: parent
			onClicked: {
				container.animationCompleted()
			}
		}
	}

    //ten draws animation
	Image {
		id: draw1
		visible: false
        x: (sceneWidth - 1400 * 0.7) / 2
        y: (sceneHeight - 680 * 0.7) /2
		z: 1000
		rotation: 10
		scale: 0
	}
	
	Image {
		id: draw2
		visible: false
        x: draw1.x + 300 * 0.7
        y: draw1.y
		z: 1000
		rotation: 10
		scale: 0
	}
	
	Image {
		id: draw3
		visible: false
        x: draw2.x + 300 * 0.7
        y: draw1.y
		z: 1000
		rotation: 10
		scale: 0
	}
	
	Image {
		id: draw4
		visible: false
        x: draw3.x + 300 * 0.7
        y: draw1.y
		z: 1000
		rotation: 10
		scale: 0
	}
	
	Image {
		id: draw5
		visible: false
        x: draw4.x + 300 * 0.7
        y: draw1.y
		z: 1000
		rotation: 10
		scale: 0
	}
	
	Image {
		id: draw6
		visible: false
        x: draw1.x
        y: draw1.y + 400 * 0.7
		z: 1000
		rotation: 10
		scale: 0
	}
	
	Image {
		id: draw7
		visible: false
        x: draw6.x + 300 * 0.7
        y: draw1.y + 400 * 0.7
		z: 1000
		rotation: 10
		scale: 0
	}
	
	Image {
		id: draw8
		visible: false
        x: draw7.x + 300 * 0.7
        y: draw1.y + 400 * 0.7
		z: 1000
		rotation: 10
		scale: 0
	}
	
	Image {
		id: draw9
		visible: false
        x: draw8.x + 300 * 0.7
        y: draw1.y + 400 * 0.7
		z: 1000
		rotation: 10
		scale: 0
	}
	
	Image {
		id: draw10
		visible: false
        x: draw9.x + 300 * 0.7
        y: draw1.y + 400 * 0.7
		z: 1000
		rotation: 10
		scale: 0
	}

	Image {
		id: new1
		source: "../image/animate/ssrNew.png"
		visible: false
		x: draw1.x
		y: draw1.y
		z: 1100
		scale: 0.5
	}
	
	Image {
		id: new2
		source: "../image/animate/ssrNew.png"
		visible: false
		x: draw2.x
		y: draw2.y
		z: 1100
		scale: 0.5
	}
	
	Image {
		id: new3
		source: "../image/animate/ssrNew.png"
		visible: false
		x: draw3.x
		y: draw3.y
		z: 1100
		scale: 0.5
	}
	
	Image {
		id: new4
		source: "../image/animate/ssrNew.png"
		visible: false
		x: draw4.x
		y: draw4.y
		z: 1100
		scale: 0.5
	}
	
	Image {
		id: new5
		source: "../image/animate/ssrNew.png"
		visible: false
		x: draw5.x
		y: draw5.y
		z: 1100
		scale: 0.5
	}
	
	Image {
		id: new6
		source: "../image/animate/ssrNew.png"
		visible: false
		x: draw6.x
		y: draw6.y
		z: 1100
		scale: 0.5
	}
	
	Image {
		id: new7
		source: "../image/animate/ssrNew.png"
		visible: false
		x: draw7.x
		y: draw7.y
		z: 1100
		scale: 0.5
	}
	
	Image {
		id: new8
		source: "../image/animate/ssrNew.png"
		visible: false
		x: draw8.x
		y: draw8.y
		z: 1100
		scale: 0.5
	}
	
	Image {
		id: new9
		source: "../image/animate/ssrNew.png"
		visible: false
		x: draw9.x
		y: draw9.y
		z: 1100
		scale: 0.5
	}
	
	Image {
		id: new10
		source: "../image/animate/ssrNew.png"
		visible: false
		x: draw10.x
		y: draw10.y
		z: 1100
		scale: 0.5
	}
    FontLoader { id: fixedFont; source: "../font/NZBZ.ttf" }

    Image {
        id: heroPic
        x: -1000
        y: sceneHeight / 2 - 350
        fillMode: Image.PreserveAspectFit
        source: "../image/animate/" + hero + ".png"
        scale: 0.3
        z: 991
    }
	Text {
		id: ten_draws_text
		color: "white"
		text: "??蝏?"
		font.family: fixedFont.name
		style: Text.Outline
		font.pointSize: 50
		font.underline: true
		z: 1200
		x: sceneWidth / 2 - 100
		y: draw1.y - 125 * 0.7
		visible: false
    }

	Text {
		id: ten_draws_tips
		color: "white"
		text: "<?孵隞餅?憭???"
		font.family: fixedFont.name
		style: Text.Outline
		font.pointSize: 20
		z: 1200
		x: sceneWidth / 2 - 90
		y: sceneHeight - 125 * 0.7
		visible: false
    }

	Text {
		id: counter
		property int value: 0
		color: "white"
		text: (value % 60).toString()
		font.family: fixedFont.name
		style: Text.Outline
		font.pointSize: 50
		x: sceneWidth - 100
		y: sceneHeight - 100
		z: 1200
		visible: false
	}

	NumberAnimation {
		id: counterAnim

		function begin(n) {
			from = n
			duration = n * 1000
			start()
		}

		target: counter
		property: "value"
		to: 0
	}

    SequentialAnimation {
        id: ten_draws
		running: false
        onStopped: {
            container.visible = false
            container.animationCompleted()
        }

        ParallelAnimation {
			ScriptAction {
				script: {
					ten_draws_text.visible = true
					ten_draws_tips.visible = true
				}					
			}
			
            PropertyAnimation {
                target: draw1
                property: "scale"
                to: 0.7
                duration: 200
            }
			PropertyAnimation {
                target: draw2
                property: "scale"
                to: 0.7
                duration: 200
            }
			PropertyAnimation {
                target: draw3
                property: "scale"
                to: 0.7
                duration: 200
            }
			PropertyAnimation {
                target: draw4
                property: "scale"
                to: 0.7
                duration: 200
            }
			PropertyAnimation {
                target: draw5
                property: "scale"
                to: 0.7
                duration: 200
            }
			PropertyAnimation {
                target: draw6
                property: "scale"
                to: 0.7
                duration: 200
            }
			PropertyAnimation {
                target: draw7
                property: "scale"
                to: 0.7
                duration: 200
            }
			PropertyAnimation {
                target: draw8
                property: "scale"
                to: 0.7
                duration: 200
            }
			PropertyAnimation {
                target: draw9
                property: "scale"
                to: 0.7
                duration: 200
            }
			PropertyAnimation {
                target: draw10
                property: "scale"
                to: 0.7
                duration: 200
            }
        }

		ScriptAction {
			script: {
				counterAnim.begin(5)
				counter.visible = true
			}					
		}

        PauseAnimation { duration: 5000 }

    }

    Component.onCompleted: {
		var arr = hero.split(":")
		if (arr[0] == "ten_draws") {
			var names = arr[1].split("+")
			var news = arr[2].split("+")
			var path = "../image/generals/card/"
            flicker_mask.visible = true
			
			draw1.source = path + names[0] + ".jpg"
			draw1.visible = true
			draw2.source = path + names[1] + ".jpg"
			draw2.visible = true
			draw3.source = path + names[2] + ".jpg"
			draw3.visible = true
			draw4.source = path + names[3] + ".jpg"
			draw4.visible = true
			draw5.source = path + names[4] + ".jpg"
			draw5.visible = true
			draw6.source = path + names[5] + ".jpg"
			draw6.visible = true
			draw7.source = path + names[6] + ".jpg"
			draw7.visible = true
			draw8.source = path + names[7] + ".jpg"
			draw8.visible = true
			draw9.source = path + names[8] + ".jpg"
			draw9.visible = true
			draw10.source = path + names[9] + ".jpg"
			draw10.visible = true
			
			new1.visible = Number(news[0])
			new2.visible = Number(news[1])
			new3.visible = Number(news[2])
			new4.visible = Number(news[3])
			new5.visible = Number(news[4])
			new6.visible = Number(news[5])
			new7.visible = Number(news[6])
			new8.visible = Number(news[7])
			new9.visible = Number(news[8])
			new10.visible = Number(news[9])
			
			//flicker_mask.source = ""
			mask_click.enabled = true
			ten_draws.running = true
		} 
	}
}
