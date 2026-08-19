#include "settings.h"
#include "engine.h"
#include <QApplication>
#include <QStyleFactory>
#include <QStyleHints>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QFont>
#include <QMessageBox>

// 构建明暗主题的基础 palette(不依赖 standardPalette,详见 applyColorScheme 注释)。
// 0=跟随系统(读 getter 判断当前系统是亮还是暗),1=强制亮色,2=强制暗色。
static QPalette buildColorSchemePalette(int scheme)
{
    int s = qBound(0, scheme, 2);
    Qt::ColorScheme systemScheme = QGuiApplication::styleHints()->colorScheme();
    bool dark = (s == 2) || (s == 0 && systemScheme == Qt::ColorScheme::Dark);

    // 每次都新建 Fusion 实例,确保 standardPalette() 干净;直接复用旧 style 的
    // standardPalette 会拿到上一轮缓存的内容。
    QStyle *fusion = QStyleFactory::create("Fusion");
    qApp->setStyle(fusion);
    QPalette pal;
    if (dark) {
        // 暗色 palette:参考 Qt 官方 Dark Style 例子的配色,文字从纯白柔化为中灰,
        // 避免高对比刺眼。系统强调色(通常蓝色)在暗色下刺眼,高亮改回中性蓝灰。
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
        // 亮色 palette:使用 Qt 标准 Fusion 亮色配色。即使系统处于暗色模式,
        // 也必须给出确定的浅色值,否则 standardPalette() 会带出暗色。
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
    // 不论条件分支都返回,调用方统一 setPalette,否则亮切暗再切亮时旧 palette 不更新。
    return pal;
}

// 统一入口:设置 palette 并重设 stylesheet 触发全局 repolish。
// sanguosha.qss 里 palette(base)/palette(window) 等是 setStyleSheet 时一次性解析的,
// 之后不重设 palette 不会反映到 stylesheet 上色。这里清空再重设触发全局 repolish,
// 让 tab 内容/按钮背景按新 palette 上色。
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
    // -server / --lua-test / --headless 等 headless 模式只用 QCoreApplication,
    // 没有 QApplication 的 setStyle/setPalette/setStyleSheet 语义,直接跳过。
    if (!qobject_cast<QApplication *>(qApp))
        return;
    applyPalette(buildColorSchemePalette(scheme));
}

// 亮度转灰阶 (Rec.601 luma)
static QColor grayColor(const QColor &c)
{
    int lum = qRound(0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue());
    return QColor(lum, lum, lum, c.alpha());
}

void applyVisualMode(const QString &mode)
{
    if (!qobject_cast<QApplication *>(qApp))
        return;
    // normal 直接回到当前主题的明暗 palette。
    if (mode == "normal") {
        applyColorScheme(Config.ColorScheme);
        return;
    }

    QPalette pal = buildColorSchemePalette(Config.ColorScheme);
    if (mode == "grayscale") {
        // 灰阶:以当前明暗主题为基底,将所有角色颜色去饱和。
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
        // highcontrast:纯黑白高对比 palette。
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
        // 自動化測試/無 font 目錄環境: 改非阻塞警告, 避免 QMessageBox 卡死 client
        qWarning("Font file %s could not be loaded; falling back to system font", qPrintable(font_path));
    }

    BigFont.setPixelSize(GetConfigFromLuaState(lua, "big_font").toInt());
    SmallFont.setPixelSize(GetConfigFromLuaState(lua, "small_font").toInt());
    TinyFont.setPixelSize(GetConfigFromLuaState(lua, "tiny_font").toInt());
    SmallFont.setWeight(QFont::Bold);
    AppFont = Config.value("AppFont", QApplication::font("QMainWindow")).value<QFont>();
    UIFont = Config.value("UIFont", QApplication::font("QTextEdit")).value<QFont>();
    TextEditColor = QColor(Config.value("TextEditColor", "white").toString());
}
