#include "themedetector.h"

//#include <QDebug>
#if QT_GUI_LIB
#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#endif

#include <QSettings>
#include <QCommandLineOption>

#ifdef Q_OS_WIN
#include <QSettings>
#endif

#ifdef Q_OS_DARWIN
extern QString getOsxCurrentTheme();
#endif

static bool _is_initialized = false;
static bool _is_dark = false;
static bool _force_light = false;

static bool _isDarkThemeEnabled()
{
    _is_initialized = true;
#ifdef Q_OS_DARWIN
    QString mode = getOsxCurrentTheme();
    return (mode == "Dark");
#elif defined(Q_OS_WIN)
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", QSettings::NativeFormat);
    return (settings.value("AppsUseLightTheme")==0);
#elif defined(Q_OS_LINUX) && QT_GUI_LIB
    return (qApp && (qGray(qApp->palette().color(QPalette::Normal, QPalette::Window).rgb()) < 100));
#else
    return false;
#endif
}

#if QT_GUI_LIB

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
/*
 * OG: @leplatrem, @QuantumCD - ref. https://gist.github.com/QuantumCD/6245215
 */
static void _initDarkPalette() {
    qApp->setStyle(QStyleFactory::create("Fusion"));

    QColor darkGray(53, 53, 53);
    QColor gray(128, 128, 128);
    QColor black(25, 25, 25);
    QColor blue(42, 130, 218);

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, darkGray);
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, black);
    darkPalette.setColor(QPalette::AlternateBase, darkGray);
    darkPalette.setColor(QPalette::ToolTipBase, blue);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, darkGray);
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::Link, blue);
    darkPalette.setColor(QPalette::Highlight, blue);
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);

    darkPalette.setColor(QPalette::Active, QPalette::Button, gray.darker());
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, gray);
    darkPalette.setColor(QPalette::Disabled, QPalette::Light, darkGray);

    qApp->setPalette(darkPalette);
#ifdef Q_OS_WIN
    qApp->setStyleSheet("QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid white; }");
#else
    qApp->setStyleSheet("QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid #f0f0f0; border-radius: 2px; }");
#endif
}

/*
 * OG: Qt (QtCreator - Flat Light) and https://forum.qt.io/topic/71818/how-to-apply-qtcreator-s-light-flat-theme-to-my-app/2
 */
static void _initLightPalette() {
    QColor blue("#ff0492C9");
    QColor shadowBackground("#ffe4e4e4");
    QColor text("#ff000000");
    QColor textDisabled("#55000000");
    QColor toolBarItem("#a0010508");
    QColor toolBarItemDisabled("#38000000");
    QColor hoverBackground("#1a000000");
    QColor selectedBackground("#a8ffffff");
    QColor normalBackground("#ffffffff");
    QColor alternateBackground("#ff515151");
    QColor toolTipBackground("#a8111111");
    QColor toolTipOutline("#ffdadada");
    QColor toolTipText("#ffdadada");

    QStyle* baseStyle = QStyleFactory::create("Fusion");

    qApp->setStyle(baseStyle);

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, shadowBackground);
    darkPalette.setColor(QPalette::WindowText, text);
    darkPalette.setColor(QPalette::Base, normalBackground);
    darkPalette.setColor(QPalette::AlternateBase, alternateBackground);
    darkPalette.setColor(QPalette::ToolTipBase, toolTipBackground);
    darkPalette.setColor(QPalette::ToolTipText, toolTipText);
    darkPalette.setColor(QPalette::Text, text);
    darkPalette.setColor(QPalette::Button, shadowBackground);
    darkPalette.setColor(QPalette::ButtonText, text);
    darkPalette.setColor(QPalette::Link, blue);
    darkPalette.setColor(QPalette::Highlight, blue);
    darkPalette.setColor(QPalette::HighlightedText, text);

    darkPalette.setColor(QPalette::Active, QPalette::Button, shadowBackground);
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, textDisabled);
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, textDisabled);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, textDisabled);
    darkPalette.setColor(QPalette::Disabled, QPalette::Light, shadowBackground);

    qApp->setPalette(darkPalette);
}

#endif  // WIN or LIN

#endif  // QT_GUI_LIB

bool ThemeDetector::addOptions(QCommandLineParser &parser)
{
#if QT_GUI_LIB

#ifdef Q_OS_WIN
    QCommandLineOption ignoreDarkMode(QStringList()<<"ignore-dark", "Ignore the system-wide dark theme setting.");
    return parser.addOption(ignoreDarkMode);
#endif

#ifdef Q_OS_LINUX
    QCommandLineOption forceDarkMode(QStringList()<<"force-dark", "Force the application to use the dark theme.");
    QCommandLineOption forceLightMode(QStringList()<<"force-light", "Force the application to use the light theme.");
    return parser.addOption(forceDarkMode) && parser.addOption(forceLightMode);
#endif

#endif  // QT_GUI_LIB

    return true;
}

bool ThemeDetector::init(const QCommandLineParser* parser, const QString& darkModeSetting) {
    if(!_is_initialized) {
        _is_dark = _isDarkThemeEnabled();
    }

#if QT_GUI_LIB

#ifdef Q_OS_WIN
    if(parser && parser->isSet("ignore-dark")) {
        _is_dark = false;
    } else if(!darkModeSetting.isEmpty()) {
        _is_dark = QSettings().value(darkModeSetting, _is_dark).toBool();
    }

    if(_is_dark) {
        _initDarkPalette();
    }
#endif

#ifdef Q_OS_LINUX
    if(parser && parser->isSet("force-dark")) {
        _is_dark = true;
    } else if(parser && parser->isSet("force-light")) {
        _is_dark = false;
        _force_light = true;
    } else if(!darkModeSetting.isEmpty()) {
        _is_dark = QSettings().value(darkModeSetting, _is_dark).toBool();
    }

    if(_is_dark) {
        _initDarkPalette();
    } else if(_force_light) {
        _initLightPalette();
    }
#endif

#endif  // QT_GUI_LIB

    return true;
}

void ThemeDetector::reinit() {
#if defined(Q_OS_WIN)
    if(_is_dark) {
        _initDarkPalette();
    }
#elif defined(Q_OS_LINUX)
    if(_is_dark) {
        _initDarkPalette();
    } else if(_force_light) {
        _initLightPalette();
    }
#endif
}

bool ThemeDetector::isDarkThemeEnabled() {
    if(!_is_initialized) {
        _is_dark = _isDarkThemeEnabled();
    }
    return _is_dark;
}

QColor ThemeDetector::iconColor()
{
    return ThemeDetector::isDarkThemeEnabled() ? QColor(226, 226, 226) : QColor(58, 58, 58);
}
