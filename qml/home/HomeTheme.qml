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

    // 技能名：亮／暗都維持對比，勿用 btnPrimary（暗色過深）
    readonly property color skillName: isDark ? "#7EDAF2" : "#0B6B8A"
    readonly property color skillNameRelated: isDark ? "#B7D4E4" : "#3A7A94"
    readonly property color skillPlate: isDark ? "#553AA8D4" : "#CCE4F4"
    readonly property color skillPlateRelated: isDark ? "#221A334C" : "#14FFFFFF"
    readonly property color nativeSkillBg: isDark ? "#221A334C" : "#66E8F3FB"
    readonly property color relatedSkillBg: isDark ? "#18142838" : "#55F3F0EA"
    readonly property color hiddenBadge: isDark ? "#C4A15A" : "#B8892E"
    readonly property color hiddenBadgeText: "#FFF8EC"
    readonly property color tabSkillsBg: isDark ? "#33203A58" : "#88DCEAF6"
    readonly property color tabVoiceBg: isDark ? "#33201838" : "#88E8DFEE"
    readonly property color tabSkillsBorder: isDark ? "#5AA8D4" : "#4EB8EA"
    readonly property color tabVoiceBorder: isDark ? "#8A7AB0" : "#8B73A8"

    // 疊在立繪／卡圖上的標籤（不隨亮暗主題反轉，保證對比）
    readonly property color onArtScrim: "#D10B1A2E"
    readonly property color onArtText: "#F4F8FC"

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

    readonly property color tableRowSelected: isDark ? "#16324A" : "#2A5574"
    readonly property color tableRowHidden: "#A0A0A0"
    readonly property color tableRowHiddenText: "#2C2C2C"
    readonly property color tableRowSelectedText: isDark ? "#F2F8FC" : "#FFFFFF"

    readonly property color cardTransparent: "transparent"
    readonly property color cardPageTint: isDark ? "#9907162A" : "#B8EDF5FB"
    readonly property color cardPanelTop: isDark ? "#F01A3049" : "#F2F8FCFF"
    readonly property color cardPanelBottom: isDark ? "#ED0D2037" : "#E8DCEAF6"
    readonly property color cardPanelBorder: isDark ? "#7295B4C9" : "#8AB3CC"
    readonly property color cardPanelInner: isDark ? "#66233C56" : "#8AEAF3FA"
    readonly property color cardAccent: isDark ? baYellow : "#8A5A00"
    readonly property color cardAccentSoft: isDark ? "#44FFD84D" : "#55F4C932"
    readonly property color cardInteractive: isDark ? baSky : "#126F9B"
    readonly property color cardInteractiveSoft: isDark ? "#443AA8D4" : "#554EB8EA"
    readonly property color cardTextPrimary: isDark ? "#F2F8FC" : "#073B5B"
    readonly property color cardTextSecondary: isDark ? "#D2E4F0" : "#355E77"
    readonly property color cardTextMuted: isDark ? "#AEC4D5" : "#526B80"
    readonly property color cardInputFill: isDark ? "#B00B1C31" : "#DDF5F9FD"
    readonly property color cardTileFill: isDark ? "#C5152A42" : "#EAF6FAFD"
    readonly property color cardTileHover: isDark ? "#E01C3C59" : "#F5FFFFFF"
    readonly property color cardTileSelected: isDark ? "#E5255574" : "#F1DCEFF8"
    readonly property color cardTileBorder: isDark ? "#567E9CB2" : "#769FB8"
    readonly property color cardBadgeBasic: isDark ? "#2B9A82" : "#1F806C"
    readonly property color cardBadgeTrick: isDark ? "#7566C4" : "#6553B5"
    readonly property color cardBadgeEquip: isDark ? "#3D7FC7" : "#2D6FAF"
    readonly property color cardBadgeSkill: isDark ? "#B06A9B" : "#95527F"
    readonly property color cardBadgeText: "#FFFFFF"
    readonly property color cardSuitRed: isDark ? "#FF7E8A" : "#B82437"
    readonly property color cardSuitBlack: isDark ? "#DCE8F0" : "#182632"
    readonly property color cardPositive: isDark ? "#72D6A1" : "#23794A"
    readonly property color cardWarning: isDark ? "#F2BE66" : "#9A6417"
    readonly property color cardDanger: isDark ? "#F0808D" : "#A52535"
    readonly property color cardControlHoverFill: isDark ? "#D21A334C" : "#F8FFFFFF"
    readonly property color cardControlPressedFill: isDark ? "#E5244B68" : "#E4D7EEF9"
    readonly property color cardControlDisabledFill: isDark ? "#66101B28" : "#88E4EBF0"
    readonly property color cardControlDisabledText: isDark ? "#738695" : "#7D8C97"
    readonly property color cardTagFill: isDark ? "#B8172A40" : "#DDEAF3F9"
    readonly property color cardTagHover: isDark ? "#DC23435D" : "#F2FFFFFF"
    readonly property color cardTagChecked: isDark ? "#D42B6A82" : "#17658D"
    readonly property color cardTagMark: "#FFFFFF"
    readonly property color cardVariantFill: isDark ? "#8A142A42" : "#BDEAF3FA"
    readonly property color cardVariantAlternateFill: isDark ? "#A31B334B" : "#D6F3F8FC"

    readonly property int cardHeaderHeight: 92
    readonly property int cardPageHMargin: 28
    readonly property int cardPageTopMargin: 18
    readonly property int cardPageBottomMargin: 8
    readonly property int cardPanelGap: 16
    readonly property int cardPanelRadius: 10
    readonly property int cardHeaderPadding: 26
    readonly property int cardPanelContentPadding: 18
    readonly property int cardPanelFocusInset: 4
    readonly property int cardFilterWidth: 300
    readonly property int cardDetailWidth: 470
    readonly property int cardGridGap: 10
    readonly property int cardControlHeight: 42
    readonly property int cardControlRadius: 7
    readonly property int cardControlHPadding: 12
    readonly property int cardPopupMaxHeight: 280
    readonly property int cardSectionGap: 11
    readonly property int cardTitleFontSize: 29
    readonly property int cardSectionTitleFontSize: 21
    readonly property int cardBodyFontSize: 14
    readonly property int cardCaptionFontSize: 13
    readonly property int cardMetaFontSize: 12
    readonly property int cardControlFontSize: 13
    readonly property int cardBorderWidth: 1
    readonly property int cardSelectedBorderWidth: 2
    readonly property int cardFocusBorderWidth: 3
    readonly property int cardHighContrastFocusBorderWidth: 4
    readonly property int cardHeaderButtonWidth: 104
    readonly property int cardActionButtonExtent: 48
    readonly property int cardHeaderTitleGap: 1
    readonly property int cardCountBadgeHPadding: 14
    readonly property int cardCountBadgeHeight: 38
    readonly property int cardSortWidth: 170
    readonly property int cardGridBottomInset: 4
    readonly property int cardPaginationBottomInset: 7
    readonly property int cardPaginationHeight: 48
    readonly property int cardPaginationButtonWidth: 54
    readonly property int cardPaginationLabelWidth: 150
    readonly property int cardPaginationFontSize: 15
    readonly property int cardEmptyFontSize: 17
    readonly property int cardTileAccentWidth: 5
    readonly property int cardTileAccentRadius: 3
    readonly property int cardTileMotionDuration: 120
    readonly property int cardTileImageMaxWidth: 116
    readonly property int cardTileImageInset: 3
    readonly property int cardTileTextGap: 6
    readonly property int cardTileTitleFontSize: 17
    readonly property int cardBadgeHPadding: 8
    readonly property int cardBadgeHeight: 24
    readonly property int cardBadgeRadius: 5
    readonly property int cardSuitIconSize: 18
    readonly property int cardDetailEmptyTopMargin: 40
    readonly property int cardDetailHeroGap: 13
    readonly property int cardDetailImageWidth: 170
    readonly property int cardDetailImageHeight: 238
    readonly property int cardDetailImageInset: 4
    readonly property int cardDetailMetaGap: 7
    readonly property int cardDetailNameFontSize: 24
    readonly property int cardDetailSuitFontSize: 16
    readonly property int cardDetailTypeFontSize: 15
    readonly property int cardDetailFlagHeight: 25
    readonly property int cardDetailFlagRadius: 5
    readonly property int cardDetailSectionFontSize: 17
    readonly property int cardDetailAudioGap: 8
    readonly property int cardDividerHeight: 1
    readonly property int cardFilterDividerTopMargin: 4
    readonly property int cardFilterResetHeight: 46
    readonly property int cardPopupOffset: 2
    readonly property int cardPopupPadding: 4
    readonly property real cardControlDisabledOpacity: 0.82
    readonly property int cardTagHeight: 32
    readonly property int cardTagRadius: 16
    readonly property int cardTagGap: 7
    readonly property int cardTagCheckSize: 17
    readonly property int cardTagHPadding: 10
    readonly property int cardTagCountGap: 5
    readonly property int cardVariantRowRadius: 6
    readonly property int cardVariantRowPadding: 9
    readonly property int cardVariantRowGap: 5
    readonly property int cardVariantSuitWidth: 92
    readonly property int cardVariantPackageWidth: 112
    readonly property int cardSkeletonHeaderPadding: 22
    readonly property int cardSkeletonTitleWidth: 260
    readonly property int cardSkeletonSubtitleWidth: 420
    readonly property int cardSkeletonTitleHeight: 26
    readonly property int cardSkeletonSubtitleHeight: 13

    // 武將頁布局：骨架與載入後畫面共用，避免尺寸對不齊
    readonly property int generalHeaderHeight: 88
    readonly property int generalPageHMargin: 28
    readonly property int generalPageTopMargin: 18
    readonly property int generalPageBottomMargin: 8
    readonly property int generalPanelGap: 16
    readonly property real generalListShare: 0.40
    readonly property int generalGridMargin: 16
    readonly property int generalCellMinWidth: 122
    readonly property int generalGridMinColumns: 5
    readonly property int generalGridMaxColumns: 9
    readonly property int generalTableRowHeight: 30
    readonly property int generalTableHeaderHeight: 28
    readonly property real generalCellAspect: 1.50
    readonly property int generalCellInset: 2

    function resolvedGridColumns(gridWidth, columns) {
        var v = columns > 0 ? columns : generalGridMinColumns
        return Math.max(generalGridMinColumns, Math.min(v, generalGridMaxColumns))
    }

    function generalMaxColumns(gridWidth) {
        return generalGridMaxColumns
    }

    function generalCellWidth(gridWidth, columns) {
        var w = Math.max(1, gridWidth)
        var cols = resolvedGridColumns(w, columns)
        if (cols >= generalGridMaxColumns)
            return w
        return Math.floor(w / cols)
    }

    function generalCellHeight(gridWidth, columns) {
        var cols = resolvedGridColumns(gridWidth, columns)
        if (cols >= generalGridMaxColumns)
            return generalTableRowHeight
        return Math.round(generalCellWidth(gridWidth, columns) * generalCellAspect)
    }

    function generalCellColumns(gridWidth, columns) {
        return resolvedGridColumns(gridWidth, columns)
    }
}
