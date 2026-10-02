#include "qi18n.h"

#if QT_GUI_LIB
#include <QComboBox>
#endif

enum I18N_String {
    I18N_System,
    I18N_ChangeLanguage,
    I18N_AfterRestart,
};

static const char* kLocStrings[] = {
    QT_TRANSLATE_NOOP("QLang", "System"),
    QT_TRANSLATE_NOOP("QLang","Change Language"),
    QT_TRANSLATE_NOOP("QLang", "Language will be changed after an application restart."),
};

#define qTs(type) ((qApp)->translate("QLang", kLocStrings[I18N_##type]))

QString QLANG::effectiveLanguage(QCoreApplication* app, QSettings* set)
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

inline QList<QLocale> QLANG::availableLanguages()
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
#if QT_VERSION >= 0x050F00
            QStringList parts = transl_files.at(i).split("_", Qt::KeepEmptyParts);
#else
            QStringList parts = transl_files.at(i).split("_", QString::KeepEmptyParts);
#endif
            parts.removeAt(0);
            transl_files[i] = parts.join("_");

            ret.append(QLocale(transl_files.at(i)));
        }
    }
    return ret;
}

#if QT_GUI_LIB
void QLANG::setupLangCombo(QSettings& set, QComboBox* langCombo)
{
    langCombo->clear();
    langCombo->addItem(qTs(System));
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

void QLANG::applyLangCombo(QSettings& set, const QComboBox* langCombo, QWidget* parent)
{
    QString lang = (langCombo->currentIndex()==0)? QString() :
                                                   langCombo->itemData(langCombo->currentIndex())
                                                   .value<QLocale>().name();
    if(!set.value(SETTINGS_LANG).toString().isEmpty() && (lang != set.value(SETTINGS_LANG).toString()))
    {
        QMessageBox::warning(parent, qTs(ChangeLanguage), qTs(AfterRestart));
    }
    set.setValue(SETTINGS_LANG,lang);
}
#endif

QList<QTranslator*> QLANG::installLanguage(QCoreApplication* app, QSettings* set, const QString& prefix)
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

void QLANG::uninstallLanguage(QCoreApplication* app, QList<QTranslator*>* langs)
{
    for(auto it = langs->cbegin(); it != langs->cend(); ++it)
    {
        app->removeTranslator(*it);
    }
    qDeleteAll(*langs);
    langs->clear();
}
