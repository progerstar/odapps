#ifndef QINTERNATIONALIZATION_H_
#define QINTERNATIONALIZATION_H_

#include <QCoreApplication>
#include <QTranslator>
#include <QSettings>
#include <QLocale>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QString>
#include <QStringBuilder>
#include <QLibraryInfo>
#include <QDebug>
#include <QList>

#if QT_GUI_LIB
class QComboBox;
#include <QMessageBox>
#endif

#define SETTINGS_LANG "UI/Language"

namespace QLANG
{

    inline QString languageDisplayName(const QLocale& loc)
    {
        return QLocale::languageToString(loc.language()) % QLatin1String(" (")  % QLocale::countryToString(loc.country()) % QLatin1String(")");
    }

    QString effectiveLanguage(QCoreApplication* app, QSettings* set = 0);

    QList<QLocale> availableLanguages();

#if QT_GUI_LIB
    void setupLangCombo(QSettings& set, QComboBox* langCombo);
    void applyLangCombo(QSettings& set, const QComboBox* langCombo, QWidget* parent = 0);
#endif

    QList<QTranslator*> installLanguage(QCoreApplication* app, QSettings* set = 0, const QString& prefix="lang");
    void uninstallLanguage(QCoreApplication* app, QList<QTranslator*>* langs);
}

#endif // QINTERNATIONALIZATION_H_
