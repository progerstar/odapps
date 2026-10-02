#include "odwidgetstyle.h"

#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QFile>
#include <QPalette>
#include <QString>

namespace {

struct ThemeColors
{
    const char* window;
    const char* surface;
    const char* base;
    const char* subtle;
    const char* border;
    const char* borderStrong;
    const char* text;
    const char* muted;
    const char* disabled;
    const char* accent;
    const char* accentHover;
    const char* accentPressed;
    const char* accentSoft;
    const char* accentText;
    const char* success;
    const char* warning;
    const char* warningText;
    const char* danger;
    const char* track;
};

const ThemeColors LightTheme = {
    "#F5F6FA", "#FFFFFF", "#FFFFFF", "#EEF0F5",
    "#D8DCE6", "#B8BECA", "#20232A", "#667085",
    "#98A2B3", "#5B45D6", "#6C56E2", "#4936B6",
    "#ECE9FF", "#FFFFFF", "#16865B", "#B76A00",
    "#20232A", "#C63C4A", "#E4E7EC"
};

const ThemeColors DarkTheme = {
    "#202226", "#292C31", "#1D1F23", "#34373D",
    "#454952", "#5B606B", "#F1F3F5", "#B5BAC4",
    "#7D828C", "#8B7CF6", "#9E91FF", "#7566DC",
    "#38334F", "#20232A", "#4ECB92", "#F0AD4E",
    "#20232A", "#FF6B78", "#383B42"
};

void replaceToken(QString& styleSheet, const char* token, const char* value)
{
    styleSheet.replace(QString::fromLatin1(token), QString::fromLatin1(value));
}

void applyPalette(QApplication* application, const ThemeColors& colors)
{
    QPalette palette = application->palette();
    const QColor window(QString::fromLatin1(colors.window));
    const QColor surface(QString::fromLatin1(colors.surface));
    const QColor base(QString::fromLatin1(colors.base));
    const QColor subtle(QString::fromLatin1(colors.subtle));
    const QColor border(QString::fromLatin1(colors.border));
    const QColor text(QString::fromLatin1(colors.text));
    const QColor muted(QString::fromLatin1(colors.muted));
    const QColor disabled(QString::fromLatin1(colors.disabled));
    const QColor accent(QString::fromLatin1(colors.accent));

    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, subtle);
    palette.setColor(QPalette::ToolTipBase, text);
    palette.setColor(QPalette::ToolTipText, window);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, surface);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, QColor(QString::fromLatin1(colors.danger)));
    palette.setColor(QPalette::Link, accent);
    palette.setColor(QPalette::LinkVisited, QColor(QString::fromLatin1(colors.accentPressed)));
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, QColor(QString::fromLatin1(colors.accentText)));
    palette.setColor(QPalette::Light, subtle);
    palette.setColor(QPalette::Mid, border);
    palette.setColor(QPalette::Dark, muted);

    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, border);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, muted);
    application->setPalette(palette);
}

}

bool ODWidgetStyle::apply(QApplication* application, bool dark)
{
    if(!application) {
        return false;
    }

    QFile styleFile(QStringLiteral(":/odwidgetstyle/opendev.qss"));
    if(!styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning()<<"Cannot load the common widget style: "<<styleFile.errorString();
        return false;
    }

    QString styleSheet = QString::fromUtf8(styleFile.readAll());
    const ThemeColors& colors = dark ? DarkTheme : LightTheme;
    replaceToken(styleSheet, "@window@", colors.window);
    replaceToken(styleSheet, "@surface@", colors.surface);
    replaceToken(styleSheet, "@base@", colors.base);
    replaceToken(styleSheet, "@subtle@", colors.subtle);
    replaceToken(styleSheet, "@border@", colors.border);
    replaceToken(styleSheet, "@border-strong@", colors.borderStrong);
    replaceToken(styleSheet, "@text@", colors.text);
    replaceToken(styleSheet, "@muted@", colors.muted);
    replaceToken(styleSheet, "@disabled@", colors.disabled);
    replaceToken(styleSheet, "@accent@", colors.accent);
    replaceToken(styleSheet, "@accent-hover@", colors.accentHover);
    replaceToken(styleSheet, "@accent-pressed@", colors.accentPressed);
    replaceToken(styleSheet, "@accent-soft@", colors.accentSoft);
    replaceToken(styleSheet, "@accent-text@", colors.accentText);
    replaceToken(styleSheet, "@success@", colors.success);
    replaceToken(styleSheet, "@warning@", colors.warning);
    replaceToken(styleSheet, "@warning-text@", colors.warningText);
    replaceToken(styleSheet, "@danger@", colors.danger);
    replaceToken(styleSheet, "@track@", colors.track);

    if(styleSheet.contains(QLatin1Char('@'))) {
        qWarning()<<"The common widget style contains an unresolved color token";
        return false;
    }

    applyPalette(application, colors);
    application->setProperty("odDarkTheme", dark);
    application->setStyleSheet(styleSheet);
    return true;
}
