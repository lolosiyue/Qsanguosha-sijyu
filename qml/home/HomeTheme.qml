pragma Singleton
import QtQuick

// 主頁主題色集中表：依 homeController.isDarkTheme 在亮/暗兩套色間切換。
// 各元件一律引用 HomeTheme.xxx，不得再寫死色值。
Item {
    id: theme

    readonly property bool isDark: homeController.isDarkTheme

    // 背景底層（底色，背景圖半透明疊其上；亮色主題用較深的灰藍，避免全窗過亮刺眼）
    readonly property color windowBg: isDark ? "#0A0E27" : "#D8E2F0"

    // 背景圖不透明度（亮色主題下略微調淡但保留質感，避免畫面平坦過亮）
    readonly property real backdropOpacity: isDark ? 0.45 : 0.32

    // 背景遮罩漸層（上緣微白 → 下緣透明融入底色）
    readonly property color gradientTop:    "#00FFFFFF"
    readonly property color gradientMidTop: isDark ? "#10FFFFFF" : "#14FFFFFF"
    readonly property color gradientMidBot: isDark ? "#05FFFFFF" : "#06FFFFFF"
    readonly property color gradientBottom: isDark ? "#000A0E27" : "#00D8E2F0"

    // 底部導航列面板
    readonly property color panelTop:    isDark ? "#F0101428" : "#F2F7FD"
    readonly property color panelBottom: isDark ? "#D0080D22" : "#D6E2F2"
    readonly property color panelBorder: isDark ? "#45A9CFFF" : "#4A6E9E"

    // 版本藥丸／小型膠囊
    readonly property color pillBg:   isDark ? "#20FFFFFF" : "#33000000"
    readonly property color pillText: isDark ? "#80FFFFFF" : "#46536B"

    // 主按鈕：主要（藍色；亮色主題改用深藍以在淺底保有對比）
    readonly property color btnPrimary:       isDark ? "#4C83ED" : "#2E6BD8"
    readonly property color btnPrimaryDown:   isDark ? "#3167CC" : "#2458B5"
    readonly property color btnPrimaryBorder: isDark ? "#D9ECFF" : "#1E4E9E"
    readonly property color btnPrimaryText:   "#FFFFFF"
    readonly property color btnPrimaryIconBg:   "#35FFFFFF"
    readonly property color btnPrimaryIconBdr: isDark ? "#B8D9FF" : "#9CC3F5"

    // 主按鈕：次要（暗色主題為深藍底+淺字；亮色主題為白底+深色邊線）
    readonly property color btnSecondary:       isDark ? "#263A5C" : "#FFFFFF"
    readonly property color btnSecondaryDown:   isDark ? "#1D2E4B" : "#E4EDF8"
    readonly property color btnSecondaryBorder: isDark ? "#42608F" : "#A9BDD6"
    readonly property color btnSecondaryText:   isDark ? "#DCE6F5" : "#1A3A66"
    readonly property color btnSecondaryIconBg:   isDark ? "#24FFFFFF" : "#E8F0FB"
    readonly property color btnSecondaryIconBdr: isDark ? "#4E6FA3" : "#B9CCE4"

    // 按鈕底部投影（亮色主題陰影加深，按鈕從淺底浮起）
    readonly property color btnShadow: isDark ? "#450A315F" : "#350A2A50"

    // 鍵盤焦點外框
    readonly property color focusBorder:     isDark ? "#55FFFFFF" : "#3D6AA8"
    readonly property color focusBorderHigh: isDark ? "#FFFFFFFF" : "#1A3A66"

    // 側欄／底部導航按鈕
    readonly property color navGlowActive: isDark ? "#263F9FFF" : "#3A59A6"
    readonly property color navGlowHover:  isDark ? "#183C7ED8" : "#2E4A86"
    readonly property color navGlowInner:  isDark ? "#124D91F2" : "#24407E"
    readonly property color navBgActive:   isDark ? "#283F75CC" : "#2F5FA8"
    readonly property color navBgHover:    isDark ? "#162E5EA8" : "#274E8A"
    readonly property color navBgDown:     isDark ? "#324D82D9" : "#3565B0"
    readonly property color navBorderFocus:  isDark ? "#B8DBFFFF" : "#1A3A66"
    readonly property color navBorderActive: isDark ? "#659CDFFF" : "#2C5288"
    readonly property color navBorderHover:  isDark ? "#357DAFEA" : "#2E4A86"
    readonly property color navLine:         isDark ? "#70B7FF" : "#2C5288"
    readonly property color navTextIdle:   isDark ? "#9FB0CE" : "#46536B"
    // hover 時文字疊在深藍色柔光板上，兩主題統一用亮白才能與背景區隔
    readonly property color navTextHover:  "#FFFFFF"
    readonly property color navTextActive: isDark ? "#FFFFFF" : "#FFFFFF"
    readonly property color navTextFocus:  isDark ? "#FFFFFF" : "#0F2A4E"
}
