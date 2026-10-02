#ifndef THEMEDETECTOR_H_
#define THEMEDETECTOR_H_

#include <QtGlobal>
#include <QColor>
#include <QCommandLineParser>

namespace ThemeDetector {
/* call AFTER qApp has been created, before cmd parsing */
bool addOptions(QCommandLineParser& parser);
/* call AFTER qApp has been created, and AFTER cmd parsing */
bool init(const QCommandLineParser* parser = nullptr, const QString& darkModeSetting = QString());
bool isDarkThemeEnabled();

void reinit();

QColor iconColor();
}

#endif  // THEMEDETECTOR_H_
