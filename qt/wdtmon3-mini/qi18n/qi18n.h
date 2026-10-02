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
#include <QLibraryInfo>
#include <QDebug>
#include <QList>

#if QT_GUI_LIB
#include <QComboBox>
#include <QMessageBox>
#endif

#define SETTINGS_LANG "UI/Language"

namespace QLANG
{

inline QString languageDisplayName(const QLocale& loc)
{
    return QLocale::languageToString(loc.language())+" ("+QLocale::countryToString(loc.country())+")";
}

inline QString effectiveLanguage(QCoreApplication* app, QSettings* set = 0)
{
    QString lang;
    if(set)
    {
        lang = set->value(SETTINGS_LANG).toString().trimmed();
    }
    else
    {
        lang = QSettings(app->organizationName(), app->applicationName())
                   .value(SETTINGS_LANG).toString().trimmed();
    }

    if(lang.isEmpty() || lang.compare(QLatin1String("system"), Qt::CaseInsensitive) == 0)
    {
        return QLocale::system().name();
    }

    const QLocale configuredLocale(lang);
    if(configuredLocale.language() == QLocale::C &&
       lang.compare(QLatin1String("C"), Qt::CaseInsensitive) != 0)
    {
        return QLocale::system().name();
    }
    return configuredLocale.name();
}

inline QList<QLocale> availableLanguages()
{
    QStringList transl_files = QDir(":/lang/").entryList(QStringList()<<QString("*.qm"));
    //convention: translation file <appname>_<lang>.qm
    QList<QLocale> ret;

    ret.append(QLocale("en_US"));

    for(int i=0;i<transl_files.size();++i)
    {
        transl_files[i].replace(".qm","");
        if(transl_files[i].contains("_"))
        {
            QStringList parts = transl_files.at(i).split(QLatin1Char('_'), Qt::KeepEmptyParts);
            parts.removeAt(0);
            transl_files[i] = parts.join("_");

            ret.append(QLocale(transl_files.at(i)));
        }
    }
    return ret;
}

#if QT_GUI_LIB
inline void setupLangCombo(QSettings& set, QComboBox* langCombo)
{
    langCombo->clear();
    langCombo->addItem(QCoreApplication::translate("QLang", "System"));
    langCombo->setItemData(0,QVariant::fromValue<QLocale>(QLocale::C));
    QList<QLocale> availLangs = QLANG::availableLanguages();
    foreach(const QLocale& loc, availLangs)
    {
        langCombo->addItem(QLANG::languageDisplayName(loc));
        langCombo->setItemData(langCombo->count()-1,QVariant::fromValue<QLocale>(loc));
    }
    if(set.value(SETTINGS_LANG,"").toString().isEmpty())
        langCombo->setCurrentIndex(0);
    else
    {
        langCombo->setCurrentText(languageDisplayName(QLocale(set.value(SETTINGS_LANG,"").toString())));
    }
}

inline void applyLangCombo(QSettings& set, const QComboBox* langCombo, QWidget* parent = 0)
{
    QString lang = (langCombo->currentIndex()==0)? QString() :
                                                   langCombo->itemData(langCombo->currentIndex())
                                                   .value<QLocale>().name();
    if(!set.value(SETTINGS_LANG).toString().isEmpty() && (lang != set.value(SETTINGS_LANG).toString()))
    {
        QMessageBox::warning(parent,
                             QCoreApplication::translate("QLang", "Change Language"),
                             QCoreApplication::translate("QLang", "Language will be changed after an application restart."));
    }
    set.setValue(SETTINGS_LANG,lang);
}
#endif

inline QList<QTranslator*> installLanguage(QCoreApplication* app, QSettings* set = 0, const QString& prefix="lang")
{
    QList<QTranslator*> ret;
    const QString lang = effectiveLanguage(app, set);

    QTranslator* qtTranslator = new QTranslator();
    if(qtTranslator->load("qt_" + lang, QLibraryInfo::location(QLibraryInfo::TranslationsPath))) {
    app->installTranslator(qtTranslator);
        ret.append(qtTranslator);
    } else {
        delete qtTranslator;
        qtTranslator = nullptr;
    }

    QStringList transl_files = QDir(QString(":/%1/").arg(prefix)).entryList(QStringList()<<QString("*_%1*.qm").arg(lang.split("_").first()));
    if(!transl_files.isEmpty())
    {
        QTranslator* translator = new QTranslator();
        qDebug()<<"Found translation files "<<transl_files.join(";")<<" for "<<lang;
        if(translator->load(QString(":/%1/%2").arg(prefix).arg(transl_files.at(0)))) {
            qDebug()<<"Installed translation "<<QString(":/%1/%2").arg(prefix).arg(transl_files.at(0));
    app->installTranslator(translator);
            ret.append(translator);
        } else {
            delete translator;
            translator = nullptr;
        }
    }
    return ret;
}

inline void uninstallLanguage(QCoreApplication* app, QList<QTranslator*>* langs)
{
    for(auto it = langs->cbegin(); it != langs->cend(); ++it)
    {
        app->removeTranslator(*it);
    }
    qDeleteAll(*langs);
    langs->clear();
}

}

#endif // QINTERNATIONALIZATION_H_
