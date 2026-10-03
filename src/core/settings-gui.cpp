#include "settings.h"
#include "engine.h"
#ifdef QSAN_XP_LEGACY
#include "runtime-paths.h"
#endif
#include <QApplication>
#include <QStyleFactory>
#include <QStyleHints>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QFont>
#include <QMessageBox>

// Build the light/dark base palette without relying on standardPalette().
// 0 follows the system, 1 forces light, and 2 forces dark.
static QPalette buildColorSchemePalette(int scheme)
{
    int s = qBound(0, scheme, 2);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    Qt::ColorScheme systemScheme = QGuiApplication::styleHints()->colorScheme();
    bool dark = (s == 2) || (s == 0 && systemScheme == Qt::ColorScheme::Dark);
#else
    // XP has no system dark-mode contract; System follows the classic light palette.
    bool dark = (s == 2);
#endif

    // Create a fresh Fusion style so standardPalette() has no cached state.
    // Reusing the previous style would carry its old palette forward.
    QStyle *fusion = QStyleFactory::create("Fusion");
    qApp->setStyle(fusion);
    QPalette pal;
    if (dark) {
        // Dark palette follows Qt's official example, with softer gray text and a neutral blue-gray highlight.
        // The system accent is too bright against the dark background.
        const QColor softText(0xcf, 0xcf, 0xcf);
        const QColor softDisabled(0x80, 0x80, 0x80);
        pal.setColor(QPalette::Window, QColor(0x35, 0x35, 0x35));
        pal.setColor(QPalette::WindowText, softText);
        pal.setColor(QPalette::Base, QColor(0x1e, 0x1e, 0x1e));
        pal.setColor(QPalette::AlternateBase, QColor(0x35, 0x35, 0x35));
        pal.setColor(QPalette::ToolTipBase, QColor(0x35, 0x35, 0x35));
        pal.setColor(QPalette::ToolTipText, softText);
        pal.setColor(QPalette::Text, softText);
        pal.setColor(QPalette::Button, QColor(0x35, 0x35, 0x35));
        pal.setColor(QPalette::ButtonText, softText);
        pal.setColor(QPalette::BrightText, QColor(0xff, 0x45, 0x45));
        pal.setColor(QPalette::Link, QColor(0x2a, 0x82, 0xda));
        pal.setColor(QPalette::Highlight, QColor(0x2a, 0x82, 0xda));
        pal.setColor(QPalette::HighlightedText, Qt::black);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, softDisabled);
        pal.setColor(QPalette::Disabled, QPalette::Text, softDisabled);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, softDisabled);
        pal.setColor(QPalette::Disabled, QPalette::Button, QColor(0x2b, 0x2b, 0x2b));
    } else {
        // Set explicit Fusion light values even when the system is dark; otherwise
        // standardPalette() would return dark colors.
        const QColor button(0xef, 0xef, 0xef);
        const QColor text(Qt::black);
        const QColor disabled(0x80, 0x80, 0x80);
        pal.setColor(QPalette::Window, button);
        pal.setColor(QPalette::WindowText, text);
        pal.setColor(QPalette::Base, Qt::white);
        pal.setColor(QPalette::AlternateBase, QColor(0xe7, 0xe7, 0xe7));
        pal.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xdc));
        pal.setColor(QPalette::ToolTipText, text);
        pal.setColor(QPalette::Text, text);
        pal.setColor(QPalette::Button, button);
        pal.setColor(QPalette::ButtonText, text);
        pal.setColor(QPalette::BrightText, Qt::white);
        pal.setColor(QPalette::Link, QColor(0x00, 0x00, 0xff));
        pal.setColor(QPalette::Highlight, QColor(0x30, 0x8c, 0xc6));
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
        pal.setColor(QPalette::Disabled, QPalette::Text, disabled);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
        pal.setColor(QPalette::Disabled, QPalette::Button, button);
    }
    // Route every branch through setPalette so switching light -> dark -> light updates the palette.
    return pal;
}

// Shared entry point: set the palette and repolish the stylesheet.
// QSS palette(base/window) values are resolved when setStyleSheet() runs.
// Clear and reapply the stylesheet so tabs and buttons use the new palette.
// This repolishes tab contents and button backgrounds with the new palette.
static void applyPalette(const QPalette &pal)
{
    qApp->setPalette(pal);
    QString ss = qApp->styleSheet();
    if (!ss.isEmpty()) {
        qApp->setStyleSheet(QString());
        qApp->setStyleSheet(ss);
    }
}

void applyColorScheme(int scheme)
{
    // Headless modes such as -server, --lua-test and --headless use only
    // QCoreApplication, so skip QApplication-only style and palette APIs.
    if (!qobject_cast<QApplication *>(qApp))
        return;
    applyPalette(buildColorSchemePalette(scheme));
}

// Convert luma to grayscale (Rec. 601).
static QColor grayColor(const QColor &c)
{
    int lum = qRound(0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue());
    return QColor(lum, lum, lum, c.alpha());
}

void applyVisualMode(const QString &mode)
{
    if (!qobject_cast<QApplication *>(qApp))
        return;
    // Normal mode restores the current light/dark theme palette.
    if (mode == "normal") {
        applyColorScheme(Config.ColorScheme);
        return;
    }

    QPalette pal = buildColorSchemePalette(Config.ColorScheme);
    if (mode == "grayscale") {
        // Grayscale desaturates role colors on top of the current theme.
        const QList<QPalette::ColorRole> roles = {
            QPalette::Window, QPalette::WindowText, QPalette::Base,
            QPalette::AlternateBase, QPalette::ToolTipBase, QPalette::ToolTipText,
            QPalette::Text, QPalette::Button, QPalette::ButtonText,
            QPalette::BrightText, QPalette::Link, QPalette::Highlight,
            QPalette::HighlightedText
        };
        foreach (QPalette::ColorRole role, roles) {
            pal.setColor(role, grayColor(pal.color(role)));
            pal.setColor(QPalette::Disabled, role, grayColor(pal.color(QPalette::Disabled, role)));
        }
    } else {
        // High-contrast mode uses a black-and-white palette.
        pal.setColor(QPalette::Window, Qt::white);
        pal.setColor(QPalette::WindowText, Qt::black);
        pal.setColor(QPalette::Base, Qt::white);
        pal.setColor(QPalette::AlternateBase, QColor(0xe0, 0xe0, 0xe0));
        pal.setColor(QPalette::ToolTipBase, Qt::white);
        pal.setColor(QPalette::ToolTipText, Qt::black);
        pal.setColor(QPalette::Text, Qt::black);
        pal.setColor(QPalette::Button, Qt::white);
        pal.setColor(QPalette::ButtonText, Qt::black);
        pal.setColor(QPalette::BrightText, Qt::black);
        pal.setColor(QPalette::Link, Qt::black);
        pal.setColor(QPalette::Highlight, Qt::black);
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x80, 0x80, 0x80));
        pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0x80, 0x80, 0x80));
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x80, 0x80, 0x80));
        pal.setColor(QPalette::Disabled, QPalette::Button, QColor(0xe0, 0xe0, 0xe0));
    }
    applyPalette(pal);
}

UiSettings UiConfig;

UiSettings::UiSettings()
    : Rect(-1280 * 0.8 / 2, -800 * 0.8 / 2, 1280 * 0.8, 800 * 0.8)
{
}

void UiSettings::init()
{
    lua_State *lua = Sanguosha->getLuaState();
    LuaLocker lua_locker;
    QString font_path = Config.value("DefaultFontPath", "font/simli.ttf").toString();
    int font_id = QFontDatabase::addApplicationFont(font_path);
    if (font_id != -1) {
        QString font_family = QFontDatabase::applicationFontFamilies(font_id).first();
        BigFont.setFamily(font_family);
        SmallFont.setFamily(font_family);
        TinyFont.setFamily(font_family);
    } else {
        // Without a font directory, automated/headless runs need a non-blocking warning to avoid hanging the client.
        qWarning("Font file %s could not be loaded; falling back to system font", qPrintable(font_path));
    }

    BigFont.setPixelSize(GetConfigFromLuaState(lua, "big_font").toInt());
    SmallFont.setPixelSize(GetConfigFromLuaState(lua, "small_font").toInt());
    TinyFont.setPixelSize(GetConfigFromLuaState(lua, "tiny_font").toInt());
    SmallFont.setWeight(QFont::Bold);
    QFont appDefaultFont = QApplication::font("QMainWindow");
    QFont textDefaultFont = QApplication::font("QTextEdit");
#ifdef QSAN_XP_LEGACY
    // XP may lack CJK system fonts; use the bundled regular face for UI text
    // instead of letting the system substitute the decorative title font.
    const QString uiFontPath = QSanRuntimePaths::assetPath(QStringLiteral("font/simsun.ttf"));
    const int uiFontId = QFontDatabase::addApplicationFont(uiFontPath);
    const QStringList uiFontFamilies = uiFontId == -1
        ? QStringList() : QFontDatabase::applicationFontFamilies(uiFontId);
    if (!uiFontFamilies.isEmpty()) {
        QFont *defaultFonts[] = { &appDefaultFont, &textDefaultFont };
        for (QFont *font : defaultFonts) {
            font->setFamily(uiFontFamilies.first());
            font->setStyleName(QString());
            font->setStyle(QFont::StyleNormal);
            font->setWeight(QFont::Normal);
            // Underline is a separate decoration, independent of normal style.
            font->setUnderline(false);
            font->setStyleStrategy(QFont::PreferAntialias);
        }
    } else {
        qWarning("UI font file %s could not be loaded; falling back to system font", qPrintable(uiFontPath));
    }
#endif
    // Saved font choices remain authoritative on every platform.
    AppFont = Config.value("AppFont", appDefaultFont).value<QFont>();
    UIFont = Config.value("UIFont", textDefaultFont).value<QFont>();
    TextEditColor = QColor(Config.value("TextEditColor", "white").toString());
}
