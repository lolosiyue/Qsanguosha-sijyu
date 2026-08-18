pragma Singleton
import QtQuick

// 主頁主題色集中表：依 homeController.isDarkTheme 在亮/暗兩套色間切換。
// 亮色為 BA-style 主視覺；暗色用深海軍藍／暗冰藍，避免純黑。
// 各元件一律引用 HomeTheme.xxx，不得再寫死色值。
Item {
    id: theme

    readonly property bool isDark: homeController.isDarkTheme

    // —— BA 語意色 ——
    readonly property color baNavy: isDark ? "#D6E8F4" : "#073B5B"
    readonly property color baBlue: isDark ? "#8FBDD4" : "#185879"
    readonly property color baSky: isDark ? "#3AA8D4" : "#4EB8EA"
    readonly property color baIce: isDark ? "#1A334C" : "#DCEAF6"
    readonly property color baWhite: isDark ? "#E8F3FA" : "#F3F8FD"
    readonly property color baYellow: "#FFD84D"

    readonly property color baDockTop: isDark ? "#CC243A58" : "#F0F3F8FD"
    readonly property color baDockBottom: isDark ? "#E0142844" : "#E6DCEAF6"
    readonly property color baDockBorder: isDark ? "#5A8AAB" : "#8AB3CC"
    readonly property color baDockShadow: isDark ? "#40061428" : "#260A2A50"

    readonly property color baNavTextIdle: isDark ? "#8AA4B8" : "#61768B"
    readonly property color baNavTextHover: isDark ? "#D2E7F4" : "#185879"
    readonly property color baNavTextActive: isDark ? "#F2F8FC" : "#073B5B"

    readonly property color baHaloOuter: isDark ? "#553AA8D4" : "#554EB8EA"
    readonly property color baHaloInner: isDark ? "#66245880" : "#66185879"

    readonly property color baNavBgHover: isDark ? "#332A4A66" : "#66F3F8FD"
    readonly property color baNavBgActive: isDark ? "#44305070" : "#88E8F3FB"

    readonly property color baToolTop: isDark ? "#F01E334C" : "#F2FFFFFF"
    readonly property color baToolBottom: isDark ? "#E6142844" : "#E6E8F3FA"

    readonly property color baPrimaryTop: isDark ? "#4AA8D0" : "#5EC4EE"
    readonly property color baPrimaryBottom: isDark ? "#1E6A94" : "#2F8EC4"
    readonly property color baSecondaryTop: isDark ? "#F01E334C" : "#F2FFFFFF"
    readonly property color baSecondaryBottom: isDark ? "#E6142844" : "#E6DCEAF6"

    readonly property color baFocusRing: isDark ? "#CCFFFFFF" : "#4EB8EA"
    readonly property color baFocusRingHigh: isDark ? "#FFFFFFFF" : "#FFFFFF"

    // 背景底層（底色，背景圖半透明疊其上）
    readonly property color windowBg: isDark ? "#0B1A2E" : "#D8E2F0"

    readonly property real backdropOpacity: isDark ? 0.45 : 0.32

    readonly property color gradientTop:    "#00FFFFFF"
    readonly property color gradientMidTop: isDark ? "#10FFFFFF" : "#14FFFFFF"
    readonly property color gradientMidBot: isDark ? "#05FFFFFF" : "#06FFFFFF"
    readonly property color gradientBottom: isDark ? "#000B1A2E" : "#00D8E2F0"

    // 底部 Dock／面板（對應 baDock*）
    readonly property color panelTop:    baDockTop
    readonly property color panelBottom: baDockBottom
    readonly property color panelBorder: baDockBorder

    readonly property color pillBg:   isDark ? "#33243A58" : "#66FFFFFF"
    readonly property color pillText: isDark ? "#B8D0E0" : "#073B5B"

    readonly property color btnPrimary:       baPrimaryBottom
    readonly property color btnPrimaryDown:   isDark ? "#185878" : "#2478A8"
    readonly property color btnPrimaryBorder: isDark ? "#8FD4EE" : "#FFFFFF"
    readonly property color btnPrimaryText:   "#FFFFFF"
    readonly property color btnPrimaryIconBg:   "#55FFFFFF"
    readonly property color btnPrimaryIconBdr: isDark ? "#C8E8F8" : "#FFFFFF"

    readonly property color btnSecondary:       baSecondaryTop
    readonly property color btnSecondaryDown:   isDark ? "#1A2E46" : "#D4E4F2"
    readonly property color btnSecondaryBorder: baDockBorder
    readonly property color btnSecondaryText:   baNavy
    readonly property color btnSecondaryIconBg:   isDark ? "#332A4A66" : "#E8F0FB"
    readonly property color btnSecondaryIconBdr: baDockBorder

    readonly property color btnShadow: baDockShadow

    readonly property color focusBorder:     baFocusRing
    readonly property color focusBorderHigh: isDark ? "#FFFFFFFF" : "#073B5B"

    readonly property color navGlowActive: baHaloOuter
    readonly property color navGlowHover:  isDark ? "#333AA8D4" : "#334EB8EA"
    readonly property color navGlowInner:  baHaloInner
    readonly property color navBgActive:   baNavBgActive
    readonly property color navBgHover:    baNavBgHover
    readonly property color navBgDown:     isDark ? "#55305070" : "#99DCEAF6"
    readonly property color navBorderFocus:  baFocusRing
    readonly property color navBorderActive: baSky
    readonly property color navBorderHover:  baDockBorder
    readonly property color navLine:         baYellow
    readonly property color navTextIdle:   baNavTextIdle
    readonly property color navTextHover:  baNavTextHover
    readonly property color navTextActive: baNavTextActive
    readonly property color navTextFocus:  baNavTextActive
}
