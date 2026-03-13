# -------------------------------------------------
# Project created by QtCreator 2010-06-13T04:26:52
# Updated for Qt 5.14 compatibility
# -------------------------------------------------
TARGET = QSanguosha

# Qt 5.14 compatible modules
QT += widgets quickwidgets qml quick #multimedia multimediawidgets sql network core gui 

# Add required Qt 5.14 modules (保留用于技能特效)
#QT += qmlmodels

# Android-specific modules
android {
	QT += androidextras
}

TEMPLATE = app
CONFIG(release,debug|release) {
	CONFIG += audio
}
CONFIG += lua
CONFIG -= flat

# Platform-specific libraries
win32 {
	LIBS += -lwinhttp
}

CONFIG += precompile_header
PRECOMPILED_HEADER = src/pch.h
DEFINES += USING_PCH

# Qt 5.14 compatibility defines
DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x050400

SOURCES += \
	src/main.cpp \
	src/client/aux-skills.cpp \
	src/client/client.cpp \
	src/client/clientplayer.cpp \
	src/client/clientstruct.cpp \
	src/core/banpair.cpp \
	src/core/card.cpp \
	src/core/engine.cpp \
	src/core/general.cpp \
	src/core/json.cpp \
	src/core/lua-wrapper.cpp \
	src/core/player.cpp \
	src/core/protocol.cpp \
	src/core/settings.cpp \
	src/core/skill.cpp \
	src/core/structs.cpp \
	src/core/util.cpp \
	src/dialog/cardeditor.cpp \
	src/dialog/cardoverview.cpp \
	src/dialog/choosegeneraldialog.cpp \
	src/dialog/configdialog.cpp \
	src/dialog/connectiondialog.cpp \
	src/dialog/customassigndialog.cpp \
	src/dialog/distanceviewdialog.cpp \
	src/dialog/maxcardsviewdialog.cpp \
	src/dialog/generaloverview.cpp \
	src/dialog/mainwindow.cpp \
	src/dialog/playercarddialog.cpp \
	src/dialog/roleassigndialog.cpp \
	src/dialog/scenario-overview.cpp \
	src/package/exppattern.cpp \
	src/package/maneuvering.cpp \
	src/package/package.cpp \
	src/package/standard.cpp \
	src/package/standard-cards.cpp \
	src/package/exclusive-cards.cpp \
	src/package/yingbian.cpp \
	src/package/standard-generals.cpp \
	src/package/thicket.cpp \
	src/package/wind.cpp \
	src/package/yin.cpp \
	src/package/lei.cpp \
	src/scenario/boss-mode-scenario.cpp \
	src/scenario/couple-scenario.cpp \
	src/scenario/guandu-scenario.cpp \
	src/scenario/miniscenarios.cpp \
	src/scenario/scenario.cpp \
	src/scenario/scenerule.cpp \
	src/scenario/zombie-scenario.cpp \
	src/server/ai.cpp \
	src/server/gamerule.cpp \
	src/server/generalselector.cpp \
	src/server/room.cpp \
	src/server/roomthread.cpp \
	src/server/roomthread1v1.cpp \
	src/server/roomthread3v3.cpp \
	src/server/roomthreadxmode.cpp \
	src/server/server.cpp \
	src/server/serverplayer.cpp \
	src/ui/button.cpp \
	src/ui/cardcontainer.cpp \
	src/ui/carditem.cpp \
	src/ui/chatwidget.cpp \
	src/ui/clientlogbox.cpp \
	src/ui/dashboard.cpp \
	src/ui/indicatoritem.cpp \
	src/ui/magatamas-item.cpp \
	src/ui/photo.cpp \
	src/ui/pixmapanimation.cpp \
	src/ui/qsanbutton.cpp \
	src/ui/rolecombobox.cpp \
	src/ui/roomscene.cpp \
	src/ui/sprite.cpp \
	src/ui/startscene.cpp \
	src/ui/window.cpp \
	src/util/detector.cpp \
	src/util/nativesocket.cpp \
	src/util/recorder.cpp \
	src/core/record-analysis.cpp \
	src/package/hegemony.cpp \
	src/scenario/fancheng-scenario.cpp \
	src/scenario/challengedeveloper-scenario.cpp \
	src/core/room-state.cpp \
	src/core/wrapped-card.cpp \
	src/ui/bubblechatbox.cpp \
	src/ui/generic-cardcontainer-ui.cpp \
	src/ui/qsan-selectable-item.cpp \
	src/ui/skin-bank.cpp \
	src/ui/table-pile.cpp \
	src/ui/timed-progressbar.cpp \
	src/ui/ui-utils.cpp \
	src/ui/graphicspixmaphoveritem.cpp \
	src/core/filehandler.cpp \
	src/ui/EmbeddedQmlLoader.cpp \
	src/ui/SpineGlItem.cpp \
	src/ui/SpineEffectWidget.cpp \
	src/ui/SpineAnimationManager.cpp \
	src/ui/CharacterSpineActionController.cpp \
	src/ui/SpineIndicatorLine.cpp \
	src/spine/Atlas.cpp \
	src/spine/Bone.cpp \
	src/spine/Slot.cpp \
	src/spine/Skin.cpp \
	src/spine/RegionAttachment.cpp \
	src/spine/MeshAttachment.cpp \
	src/spine/SkeletonData.cpp \
	src/spine/Skeleton.cpp \
	src/spine/Animation.cpp \
	src/spine/AnimationState.cpp \
	src/spine/AnimationStateData.cpp \
	src/spine/SkeletonBinary.cpp \
	src/spine/SkeletonClipping.cpp \
	src/spine/IkConstraint.cpp \
	src/spine/TransformConstraint.cpp \
	src/package/assassins.cpp \
	src/package/bgm.cpp \
	src/package/boss.cpp \
	src/package/godlailailai.cpp \
	src/package/fire.cpp \
	src/package/h-formation.cpp \
	src/package/h-momentum.cpp \
	src/package/jiange-defense.cpp \
	src/package/mobile.cpp \
	src/package/joy.cpp \
	src/package/ling.cpp \
	src/package/mountain.cpp \
	src/package/mobileshiji.cpp \
	src/package/mobilemougong.cpp \
	src/package/sp.cpp \
	src/package/tenyear.cpp \
	src/package/tenyear2.cpp \
	src/package/special1v1.cpp \
	src/package/special3v3.cpp \
	src/package/wisdom.cpp \
	src/package/yitian.cpp \
	src/package/yjcm.cpp \
	src/package/yjcm2012.cpp \
	src/package/yjcm2013.cpp \
	src/package/yjcm2014.cpp \
	src/package/yjcm2015.cpp \
	src/package/yczh2016.cpp \
	src/package/yczh2017.cpp \
	src/package/yjcm2022.cpp \
	src/package/yjcm2023.cpp \
	src/package/doudizhu.cpp \
	src/package/tenyear-strengthen.cpp \
	src/package/ol-strengthen.cpp \
	src/package/mobile-strengthen.cpp \
	swig/sanguosha_wrap.cxx \
	src/dialog/banipdialog.cpp \
	src/package/tw.cpp \
	src/package/ol.cpp \
	src/package/happy2v2.cpp \
	src/dialog/mainwindowserverlist.cpp \
	src/dialog/dialogslsettings.cpp \
	src/server/qtupnpportmapping.cpp \
	src/package/olwenwu.cpp \
	src/package/yinhu.cpp \
	src/package/maotu.cpp

# Android-specific sources
android {
	SOURCES += src/core/android_assets.cpp
}

HEADERS += \
	src/client/aux-skills.h \
	src/client/client.h \
	src/client/clientplayer.h \
	src/client/clientstruct.h \
	src/core/audio.h \
	src/core/banpair.h \
	src/core/card.h \
	src/core/compiler-specific.h \
	src/core/engine.h \
	src/core/general.h \
	src/core/json.h \
	src/core/lua-wrapper.h \
	src/core/player.h \
	src/core/protocol.h \
	src/core/settings.h \
	src/core/skill.h \
	src/core/structs.h \
	src/core/util.h \
	src/dialog/cardeditor.h \
	src/dialog/cardoverview.h \
	src/dialog/choosegeneraldialog.h \
	src/dialog/configdialog.h \
	src/dialog/connectiondialog.h \
	src/dialog/customassigndialog.h \
	src/dialog/distanceviewdialog.h \
	src/dialog/maxcardsviewdialog.h \
	src/dialog/generaloverview.h \
	src/dialog/mainwindow.h \
	src/dialog/playercarddialog.h \
	src/dialog/roleassigndialog.h \
	src/dialog/scenario-overview.h \
	src/package/exppattern.h \
	src/package/maneuvering.h \
	src/package/package.h \
	src/package/standard.h \
	src/package/standard-cards.h \
	src/package/exclusive-cards.h \
	src/package/yingbian.h \
	src/package/standard-generals.h \
	src/scenario/boss-mode-scenario.h \
	src/scenario/couple-scenario.h \
	src/scenario/guandu-scenario.h \
	src/scenario/miniscenarios.h \
	src/scenario/scenario.h \
	src/scenario/scenerule.h \
	src/scenario/zombie-scenario.h \
	src/server/ai.h \
	src/server/gamerule.h \
	src/server/generalselector.h \
	src/server/room.h \
	src/server/roomthread.h \
	src/server/roomthread1v1.h \
	src/server/roomthread3v3.h \
	src/server/roomthreadxmode.h \
	src/server/server.h \
	src/server/serverplayer.h \
	src/ui/button.h \
	src/ui/cardcontainer.h \
	src/ui/carditem.h \
	src/ui/chatwidget.h \
	src/ui/clientlogbox.h \
	src/ui/dashboard.h \
	src/ui/indicatoritem.h \
	src/ui/magatamas-item.h \
	src/ui/photo.h \
	src/ui/pixmapanimation.h \
	src/ui/qsanbutton.h \
	src/ui/rolecombobox.h \
	src/ui/roomscene.h \
	src/ui/sprite.h \
	src/ui/startscene.h \
	src/ui/window.h \
	src/util/detector.h \
	src/util/nativesocket.h \
	src/util/recorder.h \
	src/util/socket.h \
	src/core/record-analysis.h \
	src/package/hegemony.h \
	src/scenario/fancheng-scenario.h \
	src/scenario/challengedeveloper-scenario.h \
	src/package/assassins.h \
	src/package/bgm.h \
	src/package/boss.h \
	src/package/godlailailai.h \
	src/package/fire.h \
	src/package/h-formation.h \
	src/package/h-momentum.h \
	src/package/jiange-defense.h \
	src/package/mobile.h \
	src/package/joy.h \
	src/package/yin.h \
	src/package/lei.h \
	src/package/ling.h \
	src/package/mountain.h \
	src/package/sp.h \
	src/package/tenyear.h \
	src/package/special1v1.h \
	src/package/special3v3.h \
	src/package/wisdom.h \
	src/package/yitian.h \
	src/package/yjcm.h \
	src/package/yjcm2012.h \
	src/package/yjcm2013.h \
	src/package/yjcm2014.h \
	src/package/yjcm2015.h \
	src/package/yczh2016.h \
	src/package/yczh2017.h \
	src/package/yjcm2022.h \
	src/package/yjcm2023.h \
	src/package/doudizhu.h \
	src/package/tenyear-strengthen.h \
	src/package/ol-strengthen.h \
	src/package/mobile-strengthen.h \
	src/core/room-state.h \
	src/core/wrapped-card.h \
	src/ui/bubblechatbox.h \
	src/ui/generic-cardcontainer-ui.h \
	src/ui/qsan-selectable-item.h \
	src/ui/skin-bank.h \
	src/ui/table-pile.h \
	src/ui/timed-progressbar.h \
	src/ui/ui-utils.h \
	src/ui/graphicspixmaphoveritem.h \
	src/core/filehandler.h \
	src/ui/EmbeddedQmlLoader.h \
	src/ui/SpineGlItem.h \
	src/ui/SpineEffectWidget.h \
	src/ui/SpineAnimationManager.h \
	src/ui/CharacterSpineActionController.h \
	src/ui/SpineIndicatorLine.h \
	include/spine-cpp/spine/spine.h \
	include/spine-cpp/spine/SpineString.h \
	include/spine-cpp/spine/Color.h \
	include/spine-cpp/spine/Vector.h \
	include/spine-cpp/spine/TextureLoader.h \
	include/spine-cpp/spine/Atlas.h \
	include/spine-cpp/spine/BoneData.h \
	include/spine-cpp/spine/Bone.h \
	include/spine-cpp/spine/SlotData.h \
	include/spine-cpp/spine/Slot.h \
	include/spine-cpp/spine/Attachment.h \
	include/spine-cpp/spine/RegionAttachment.h \
	include/spine-cpp/spine/MeshAttachment.h \
	include/spine-cpp/spine/SkeletonData.h \
	include/spine-cpp/spine/Skin.h \
	include/spine-cpp/spine/Animation.h \
	include/spine-cpp/spine/Skeleton.h \
	include/spine-cpp/spine/AnimationStateData.h \
	include/spine-cpp/spine/EventData.h \
	include/spine-cpp/spine/AnimationState.h \
	include/spine-cpp/spine/SkeletonBinary.h \
	include/spine-cpp/spine/SkeletonClipping.h \
	src/package/thicket.h \
	src/package/wind.h \
	src/dialog/banipdialog.h \
	src/package/tw.h \
	src/package/ol.h \
	src/package/happy2v2.h \
	src/pch.h \
	src/dialog/mainwindowserverlist.h \
	src/dialog/dialogslsettings.h \
	src/core/defines.h \
	src/server/qtupnpportmapping.h \
	src/package/mobileshiji.h \
	src/package/mobilemougong.h \
	src/package/olwenwu.h \
	src/package/yinhu.h \
	src/package/maotu.h

# Android-specific headers
android {
	HEADERS += src/core/android_assets.h
}

FORMS += \
	src/dialog/cardoverview.ui \
	src/dialog/configdialog.ui \
	src/dialog/connectiondialog.ui \
	src/dialog/generaloverview.ui \
	src/dialog/mainwindow.ui \
	src/dialog/mainwindowserverlist.ui \
	src/dialog/dialogslsettings.ui

CONFIG(buildbot) {
	DEFINES += USE_BUILDBOT
	SOURCES += src/bot_version.cpp
}

INCLUDEPATH += include
INCLUDEPATH += include/spine-cpp
INCLUDEPATH += src/client
INCLUDEPATH += src/core
INCLUDEPATH += src/dialog
INCLUDEPATH += src/package
INCLUDEPATH += src/scenario
INCLUDEPATH += src/server
INCLUDEPATH += src/ui
INCLUDEPATH += src/util

win32{
	RC_FILE += resource/icon.rc
}

macx{
	ICON = resource/icon/sgs.icns
}

LIBS += -L.
win32-msvc*{
	DEFINES += _CRT_SECURE_NO_WARNINGS
	QMAKE_CXXFLAGS += /utf-8
	LIBS += legacy_stdio_definitions.lib
	!contains(QMAKE_HOST.arch, x86_64) {
		DEFINES += WIN32
		LIBS += -L"$$_PRO_FILE_PWD_/lib/win/x86"
	} else {
		DEFINES += WIN64
		LIBS += -L"$$_PRO_FILE_PWD_/lib/win/x64"
	}
	CONFIG(debug, debug|release) {
		!winrt:INCLUDEPATH += include/vld
	} else {
		QMAKE_LFLAGS_RELEASE = $$QMAKE_LFLAGS_RELEASE_WITH_DEBUGINFO
		DEFINES += USE_BREAKPAD

		SOURCES += src/breakpad/client/windows/crash_generation/client_info.cc \
			src/breakpad/client/windows/crash_generation/crash_generation_client.cc \
			src/breakpad/client/windows/crash_generation/crash_generation_server.cc \
			src/breakpad/client/windows/crash_generation/minidump_generator.cc \
			src/breakpad/client/windows/handler/exception_handler.cc \
			src/breakpad/common/windows/guid_string.cc

		HEADERS += src/breakpad/client/windows/crash_generation/client_info.h \
			src/breakpad/client/windows/crash_generation/crash_generation_client.h \
			src/breakpad/client/windows/crash_generation/crash_generation_server.h \
			src/breakpad/client/windows/crash_generation/minidump_generator.h \
			src/breakpad/client/windows/handler/exception_handler.h \
			src/breakpad/common/windows/guid_string.h

		INCLUDEPATH += src/breakpad
		INCLUDEPATH += src/breakpad/client/windows
	}
}
win32-g++{
	DEFINES += WIN32
	LIBS += -L"$$_PRO_FILE_PWD_/lib/win/MinGW"
	DEFINES += GPP
}
winrt{
	DEFINES += _CRT_SECURE_NO_WARNINGS
	DEFINES += WINRT
	!winphone {
		LIBS += -L"$$_PRO_FILE_PWD_/lib/winrt/x64"
	} else {
		DEFINES += WINPHONE
		contains($$QMAKESPEC, arm): LIBS += -L"$$_PRO_FILE_PWD_/lib/winphone/arm"
		else : LIBS += -L"$$_PRO_FILE_PWD_/lib/winphone/x86"
	}
}
macx{
	DEFINES += MAC
	LIBS += -L"$$_PRO_FILE_PWD_/lib/mac/lib"
}
ios{
	DEFINES += IOS
	CONFIG(iphonesimulator){
		LIBS += -L"$$_PRO_FILE_PWD_/lib/ios/simulator/lib"
	}
	else {
		LIBS += -L"$$_PRO_FILE_PWD_/lib/ios/device/lib"
	}
}
linux{
	android{
		DEFINES += ANDROID
		# Android libraries are provided by Qt and NDK, no custom lib path needed
		# ANDROID_LIBPATH = $$_PRO_FILE_PWD_/lib/android/$$ANDROID_ARCHITECTURE/lib
		# LIBS += -L"$$ANDROID_LIBPATH"

		# Android specific settings - compatible with Qt 5.14.2
		ANDROID_COMPILE_SDK_VERSION = 28
		ANDROID_TARGET_SDK_VERSION = 28
		ANDROID_MIN_SDK_VERSION = 21

		CONFIG -= audio  # Disable FMOD audio for now
        # 根据架构添加FMOD库
        contains(ANDROID_TARGET_ARCH, arm64-v8a) {
            LIBS += -L"$$_PRO_FILE_PWD_/lib/android/arm64-v8a"
        } else: contains(ANDROID_TARGET_ARCH, armeabi-v7a) {
            LIBS += -L"$$_PRO_FILE_PWD_/lib/android/armeabi-v7a"
        } else: contains(ANDROID_TARGET_ARCH, x86_64) {
            LIBS += -L"$$_PRO_FILE_PWD_/lib/android/x86_64"
        } else: contains(ANDROID_TARGET_ARCH, x86) {
            LIBS += -L"$$_PRO_FILE_PWD_/lib/android/x86"
        }
		# 添加FMOD初始化代码
		ANDROID_EXTRA_LIBS += \
			$$_PRO_FILE_PWD_/lib/android/$${ANDROID_TARGET_ARCH}/libfmod.so \
			$$_PRO_FILE_PWD_/lib/android/$${ANDROID_TARGET_ARCH}/libfmodL.so

		# Android permissions and package info
		ANDROID_PACKAGE_SOURCE_DIR = $$_PRO_FILE_PWD_/resource/android
		assets.files = $$PWD/Bin/newsgs/sanguosha.qm \
					$$PWD/Bin/newsgs/config.ini \
					$$PWD/Bin/newsgs/qt_zh_CN.qm \
					$$PWD/Bin/newsgs/audio \
					$$PWD/Bin/newsgs/diy \
					$$PWD/Bin/newsgs/dmp \
					$$PWD/Bin/newsgs/doc \
					$$PWD/Bin/newsgs/etc \
					$$PWD/Bin/newsgs/extensions \
					$$PWD/Bin/newsgs/font \
					$$PWD/Bin/newsgs/iconengines \
					$$PWD/Bin/newsgs/image \
					$$PWD/Bin/newsgs/imageformats \
					$$PWD/Bin/newsgs/lang \
					$$PWD/Bin/newsgs/listserver \
					$$PWD/Bin/newsgs/lua \
					$$PWD/Bin/newsgs/platforminputcontexts \
					$$PWD/Bin/newsgs/platforms \
					$$PWD/Bin/newsgs/qmltooling \
					$$PWD/Bin/newsgs/qss \
					$$PWD/Bin/newsgs/QtQuick.2 \
					$$PWD/Bin/newsgs/scenarios \
					$$PWD/Bin/newsgs/skins \
					#$$PWD/Bin/newsgs/sqldrivers \
					$$PWD/Bin/newsgs/translations \
					$$PWD/Bin/newsgs/virtualkeyboard \
					$$PWD/Bin/newsgs/ui-script
		assets.path = /assets
		INSTALLS += assets

		# Android app info
		ANDROID_APP_NAME = "QSanguosha"
		ANDROID_PACKAGE_NAME = "org.qsanguosha.game"
		ANDROID_VERSION_NAME = "2.0.0"
		ANDROID_VERSION_CODE = 1
	}
	else {
		DEFINES += LINUX
		!contains(QMAKE_HOST.arch, x86_64) {
			LIBS += -L"$$_PRO_FILE_PWD_/lib/linux/x86"
			QMAKE_LFLAGS += -Wl,--rpath=lib/linux/x86
		}
		else {
			LIBS += -L"$$_PRO_FILE_PWD_/lib/linux/x64"
			QMAKE_LFLAGS += -Wl,--rpath=lib/linux/x64
		}
	}
}

CONFIG(audio){
    DEFINES += AUDIO_SUPPORT
    INCLUDEPATH += include/fmod

    # Platform-specific audio libraries
    win32|linux:!android {
        CONFIG(debug, debug|release): LIBS += -lfmodexL
        else:LIBS += -lfmodex
    }

    # 添加Android平台的FMOD支持
    android {
        CONFIG(debug, debug|release): LIBS += -lfmodL
        else: LIBS += -lfmod
    }

    SOURCES += src/core/audio.cpp
}

CONFIG(lua){

android:DEFINES += "\"getlocaledecpoint()='.'\""

	SOURCES += \
		src/lua/lzio.c \
		src/lua/lvm.c \
		src/lua/lundump.c \
		src/lua/ltm.c \
		src/lua/ltablib.c \
		src/lua/ltable.c \
		src/lua/lstrlib.c \
		src/lua/lstring.c \
		src/lua/lstate.c \
		src/lua/lparser.c \
		src/lua/loslib.c \
		src/lua/lopcodes.c \
		src/lua/lobject.c \
		src/lua/loadlib.c \
		src/lua/lmem.c \
		src/lua/lmathlib.c \
		src/lua/llex.c \
		src/lua/liolib.c \
		src/lua/linit.c \
		src/lua/lgc.c \
		src/lua/lfunc.c \
		src/lua/ldump.c \
		src/lua/ldo.c \
		src/lua/ldebug.c \
		src/lua/ldblib.c \
		src/lua/lctype.c \
		src/lua/lcorolib.c \
		src/lua/lcode.c \
		src/lua/lbitlib.c \
		src/lua/lbaselib.c \
		src/lua/lauxlib.c \
		src/lua/lapi.c
	HEADERS += \
		src/lua/lzio.h \
		src/lua/lvm.h \
		src/lua/lundump.h \
		src/lua/lualib.h \
		src/lua/luaconf.h \
		src/lua/lua.hpp \
		src/lua/lua.h \
		src/lua/ltm.h \
		src/lua/ltable.h \
		src/lua/lstring.h \
		src/lua/lstate.h \
		src/lua/lparser.h \
		src/lua/lopcodes.h \
		src/lua/lobject.h \
		src/lua/lmem.h \
		src/lua/llimits.h \
		src/lua/llex.h \
		src/lua/lgc.h \
		src/lua/lfunc.h \
		src/lua/ldo.h \
		src/lua/ldebug.h \
		src/lua/lctype.h \
		src/lua/lcode.h \
		src/lua/lauxlib.h \
		src/lua/lapi.h
	INCLUDEPATH += src/lua
}

!build_pass{
	system("lrelease $$_PRO_FILE_PWD_/builds/sanguosha.ts -qm $$_PRO_FILE_PWD_/sanguosha.qm")

	SWIG_bin = "swig"
	contains(QMAKE_HOST.os, "Windows"): SWIG_bin = "$$_PRO_FILE_PWD_/tools/swig/swig.exe"

	system("$$SWIG_bin -c++ -lua $$_PRO_FILE_PWD_/swig/sanguosha.i")
}

TRANSLATIONS += builds/sanguosha.ts

# FreeType library - only for desktop platforms
!android {
	CONFIG(debug, debug|release): LIBS += -lfreetype_D
	else:LIBS += -lfreetype

	INCLUDEPATH += $$_PRO_FILE_PWD_/include/freetype
	DEPENDPATH += $$_PRO_FILE_PWD_/include/freetype
}

# Android-specific optimizations and settings
android {    
	# Optimize for mobile devices
	QMAKE_CXXFLAGS += -Os  # Optimize for size
	QMAKE_CXXFLAGS += -ffunction-sections -fdata-sections
	QMAKE_LFLAGS += -Wl,--gc-sections
    ANDROID_EXTRA_CPPFLAGS += -DFMOD_ENABLED

	# Android-specific defines
	DEFINES += MOBILE_DEVICE
	DEFINES += TOUCH_INTERFACE

	# Disable some desktop-only features
	DEFINES += NO_DESKTOP_FEATURES

	# Enable Android logging
	DEFINES += ANDROID_LOGGING
}

# Note: The following lines were corrupted in the original file
# If heroskincontainer files exist, uncomment and fix the paths:
# SOURCES += src/ui/heroskincontainer.cpp
# HEADERS += src/ui/heroskincontainer.h
