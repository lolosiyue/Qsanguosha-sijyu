/********************************************************************
    Copyright (c) 2013-2014-QSanguosha-Rara

    This file is part of QSanguosha-Hegemony.

    This game is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 3.0
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    See the LICENSE file for more details.

    QSanguosha-Rara
    *********************************************************************/

import QtQuick 2.12

Item {
    function resolveFullskinImageSource(heroSpec) {
        var spec = heroSpec ? String(heroSpec) : ""
        var arr = spec.split(":")
        var token = arr.length > 1 ? arr.slice(1).join(":") : spec

        if (!token) return ""

        var lower = token.toLowerCase()
        var hasExt = lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".webp")
        if (token.indexOf("/") >= 0 || hasExt) {
            if (token.startsWith("../")) return token
            if (token.startsWith("image/")) return "../" + token
            return token
        }

        return "../image/fullskin/generals/full/" + token + ".jpg"
    }

    Rectangle {
        id:mask
        x:0
        width:sceneWidth
        height:180
        y:sceneHeight*0.5
        color:"black"
        opacity:0
        z:0
    }
	Image {
		id:heroPic
		x:sceneWidth
		y:sceneHeight*0.35
		fillMode:Image.PreserveAspectFit
        source:resolveFullskinImageSource(hero)
		opacity:0.6
		scale:0.8
		z:1
	}
    Rectangle {
        id:heroPicBg
        color:"transparent"
 		x:sceneWidth*0.27
		y:sceneHeight*0.24
		opacity:0
        z:2
        Image {
			y:sceneHeight*0.073
            fillMode:Image.PreserveAspectFit
            source:resolveFullskinImageSource(hero)
            scale:1.4
        }
        Image {
			x:heroPicBg.x*-0.7
			y:sceneHeight*-0.03
            fillMode:Image.PreserveAspectFit
            source:"../image/fullskin/generals/bluemask.png"
			opacity:0.4
			scale:1.5
        }
        Image {
			x:heroPicBg.x*-0.23
            fillMode:Image.PreserveAspectFit
            source:"../image/fullskin/generals/broadcast.png"
            scale:1.35
        }
    }
    Text {
        id:text
        color:"white"
        text:skill
        font.family: "隶书"
        style:Text.Outline
        font.pointSize:900
        opacity:0
        x:sceneWidth*0.45
        y:sceneHeight*0.54
        z:3
    }
    ParallelAnimation {
        id:step1
        running:false
        PropertyAnimation {
            target:heroPic
            property:"x"
            to:sceneWidth/2-500
            duration:800
            easing.type:Easing.OutQuad
			easing.overshoot:3
        }
        PropertyAnimation{
            target:mask
            property:"opacity"
            to:0.6
            duration:0
        }
        onStopped:{
			step2.start()
        }
    }
    SequentialAnimation {
        id:step2
        onStopped:{
            container.visible = false
            container.animationCompleted()
        }
        ParallelAnimation {
			PropertyAnimation {
                target:heroPicBg
                property:"opacity"
                to:1
                duration:800
				easing.overshoot:3
				easing.type:Easing.OutQuad
            }
            PropertyAnimation {
                target:text
                property:"opacity"
                to:1.0
                duration:800
            }
            PropertyAnimation {
                target:text
                property:"font.pointSize"
                to:90
                duration:800
            }
        }
        PauseAnimation { duration:2500 }
    }
    Component.onCompleted:{
        step1.start()
    }
}

/*
Item {
    Rectangle {
        id:mask
        height:sceneHeight
        width:sceneWidth
        color:"black"
        opacity:0
    }
    Image {
        id:heroPic
        x:sceneWidth/2-width/2
        y:sceneHeight/2-height/2
        fillMode:Image.PreserveAspectFit
        source:"../image/fullskin/generals/full/"+hero+".png"
        scale:0.4
        rotation:-10
        z:998
    }
    FontLoader {
        id:bwk
        source:"../font/FZBWKSK.TTF"
    }
    Text {
        id:text
        color:"white"
        text:skill
        font.family:bwk.name
        style:Text.Outline
        font.pointSize:90
        opacity:0
        z:999
        x:sceneWidth/2
        y:sceneHeight/2+45
    }
    ParallelAnimation {
        id:step1
        running:false
        PropertyAnimation {
            target:heroPic
            property:"scale"
            to:2.2
            duration:400
            easing.type:Easing.OutQuad
			easing.overshoot:3
        }
        PropertyAnimation{
            target:mask
            property:"opacity"
            to:0.4
            duration:400
        }
        onStopped:{
			step2.start()
        }
    }
    SequentialAnimation {
        id:step2
        onStopped:{
            container.visible = false
            container.animationCompleted()
        }
        PauseAnimation {
            duration:100
        }
        ParallelAnimation {
            PropertyAnimation {
                target:heroPic
                property:"scale"
                to:1
                duration:500
                easing.type:Easing.InQuad
            }
            PropertyAnimation {
                target:heroPic
                property:"x"
                to:sceneWidth/2-heroPic.width/2-150
                duration:500
                easing.type:Easing.InQuad
            }
            SequentialAnimation {
                PauseAnimation {
                    duration:240
                }
                ParallelAnimation {
                    PropertyAnimation {
                        target:text
                        property:"opacity"
                        to:1.0
                        duration:800
                    }
                    PropertyAnimation {
                        target:text
                        property:"x"
                        to:sceneWidth/2+15
                        duration:800
                    }
                }
            }
        }
        PauseAnimation { duration:2400 }
    }
    Component.onCompleted:{
        step1.start()
    }
}
*/
