# QSanguosha Home UI Design System

## 1. Visual direction

The home UI uses a dark navy, ice-glass interface with restrained gold accents and cyan interactive states. Card Overview follows the existing home visual language instead of reproducing the bright reference image literally.

## 2. Color system

All QML colors come from `qml/home/HomeTheme.qml`. New card-browser surfaces use semantic `card*` tokens for page chrome, panels, text, filters, selection, badges, focus, and suit colors. Components must not contain raw color literals.

## 3. Typography

Use the application font. The hierarchy is 28-32 px page titles, 18-22 px section and card titles, 15-16 px body and control labels, and 12-13 px secondary metadata. CJK text wraps only in detail content; compact controls elide.

## 4. Layout and spacing

The home scene is composed on the existing 1920 x 1080 canvas. Embedded subpages reserve 148 px for the persistent bottom dock. Card Overview uses a header, a left filter rail, a 4 x 3 card-type grid, and a right detail panel with 16 px primary gaps and 8 px compact gaps. The grid contains one tile per stable card `objectName`; the first engine card is the representative artwork and copy. Physical cards are exposed only in the detail panel, grouped into suit + number + package rows with their IDs merged in engine order.

## 5. Components and states

`BASlantedPanel`, `BAToolButton`, and `HomeScrollBar` remain the base primitives. Card-specific components are `CardFilterPanel`, `CardTagChip`, `CardBrowserTile`, `CardDetailPanel`, `CardVariantList`, and `CardPagination`. `CardTagChip` is a checkbox-like multi-select control with an explicit mark, selected fill, pointer feedback, and a high-contrast focus ring. Every interactive component exposes idle, hover, pressed, selected, disabled, and keyboard-focus states where applicable.

The tag facet contains only stable catalog traits: damage, single-target, recastable, translated YingBian effects, and translated character tags. Selecting several tags uses OR inside the tag facet; that result is combined with text, type, kind, suit, and package filters using AND.

## 6. Motion

Use only opacity and transform transitions. Micro-interactions stay within 90-160 ms; page and loader reveals stay within 200-320 ms. No layout-property animation is permitted.

## 7. Depth and borders

Glass panels use a tonal fill plus one semantic border. Shadows are limited to major panels and selected cards. Nested sections use tonal separation rather than repeated bordered containers.

## 8. Accessibility and reference deviations

All Card Overview actions support Tab, Backtab, arrow keys, Enter/Space, and Escape. Tab order runs from the header into search, single-select filters, tag chips, reset, the card grid, details/audio, pagination, and the persistent dock; Backtab reverses that route. Tag chips use checkbox accessibility semantics and announce their checked state. Focus rings remain visible at high contrast, controls use descriptive accessible names, and CJK labels elide or wrap deliberately.

The card grid keeps arrow navigation inside the grid, then transfers focus at its four boundaries: left/up return to filtering, right enters details, and down enters pagination. Tab and Backtab always leave the grid. Card-effect copy is a read-only selectable text surface that supports mouse selection, Shift+arrow keyboard selection, and normal Tab traversal before audio and physical variants.

The complete surface must remain legible in light and dark color schemes and under the existing grayscale and high-contrast post-processing modes. Component colors come from semantic `HomeTheme.card*` tokens; high contrast increases focus-border width and never relies on hue alone. The supplied bright fantasy mockup is treated as an information-architecture reference only; the shipped scene retains the existing dark glass, gold, and cyan home theme as requested.
