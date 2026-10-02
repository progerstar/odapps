#include "qmaterialfont.h"

#include <QStyle>
#include <QDebug>
#include <QFontDatabase>
#include <QFile>
#include <QHash>
#include <QDebug>

#include <QAbstractButton>
#include <QLabel>

QFontIconEngine::QFontIconEngine() :QIconEngine()
{

}

QFontIconEngine::~QFontIconEngine()
{

}

void QFontIconEngine::paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state)
{
    QFont font = QFont(mFontFamily);
    int drawSize = qRound(rect.height() * 0.8);
    font.setPixelSize(drawSize);

    QColor penColor;
    if (!mBaseColor.isValid())
    {
        penColor = QApplication::palette("QWidget").color(QPalette::Normal, QPalette::ButtonText);
    }
    else
    {
        penColor = mBaseColor;
    }

    switch(mode)
    {
        case QIcon::Disabled:
        {
            penColor = QApplication::palette("QWidget").color(QPalette::Disabled, QPalette::ButtonText);
            break;
        }
        case QIcon::Selected:
        {
            penColor = QApplication::palette("QWidget").color(QPalette::Active, QPalette::ButtonText);
            break;
        }
        case QIcon::Active:
        case QIcon::Normal:
        {
            if((state == QIcon::On) && mCheckColor.isValid())
            {
                penColor = mCheckColor;
            }
            break;
        }
    }

    painter->save();
    painter->setPen(QPen(penColor));
    painter->setFont(font);
    painter->drawText(rect, Qt::AlignCenter, (state == QIcon::On) ? onLetter : offLetter);

    painter->restore();
}

QPixmap QFontIconEngine::pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state)
{
    QPixmap pix(size);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    paint(&painter, QRect(QPoint(0,0),size), mode, state);
    return pix;

}

void QFontIconEngine::setFontFamily(const QString &family)
{
    mFontFamily = family;
}

void QFontIconEngine::setLetter(const QChar &letter, QIcon::State mode)
{
    *((mode == QIcon::On)?(&onLetter) : (&offLetter)) = letter;
}

void QFontIconEngine::setBaseColor(const QColor &baseColor)
{
    mBaseColor = baseColor;
}

void QFontIconEngine::setCheckColor(const QColor &checkColor)
{
    mCheckColor = checkColor;
}


QIconEngine *QFontIconEngine::clone() const
{
    QFontIconEngine * engine = new QFontIconEngine;
    engine->setFontFamily(mFontFamily);
    engine->setBaseColor(mBaseColor);
    return engine;
}

/*********************************************************************/

QMaterialIcon::QMaterialIcon(QObject *parent) :QObject(parent), _first_init(true)
{
    _colors = {{QColor(Qt::black), QColor(Qt::green), QColor(Qt::green), QColor(Qt::darkYellow), QColor(Qt::red)}};
    init();
}

QMaterialIcon::~QMaterialIcon()
{
    deinit();
}

void QMaterialIcon::init()
{
    materialFontID = QFontDatabase::addApplicationFont(":/qtmf/latest/MaterialIcons-Regular.ttf");
    if(materialFontID >= 0)
    {
        mfamilies = QFontDatabase::applicationFontFamilies(materialFontID);

        QFile cpfile(":/qtmf/latest/codepoints");
        if(!cpfile.open(QFile::ReadOnly))
        {
            qWarning()<<"Cannot load the codepoints file";
        }
        else
        {
            QString cpLine;bool convok;
            while(!(cpLine = QString::fromLatin1(cpfile.readLine())).isEmpty()) {
#if QT_VERSION >= 0x050F00
                QStringList name_code = cpLine.trimmed().split(" ", Qt::SkipEmptyParts);
#else
                QStringList name_code = cpLine.trimmed().split(" ", QString::SkipEmptyParts);
#endif
                convok = false;
                if(name_code.size() == 2) {
                    codepoints.insert(name_code.at(0), name_code.at(1).toUInt(&convok, 16));
                }
                if(!convok) {
                    qWarning()<<"Invalid codepoint line "<<cpLine.trimmed();
                }
            }
            cpfile.close();
        }
    }
    else
    {
        qWarning()<<"Cannot load MaterialIcons font";
    }
}

void QMaterialIcon::deinit()
{
    if(materialFontID >= 0) {
        QFontDatabase::removeApplicationFont(materialFontID);
        materialFontID = -1;
    }
}

QMaterialIcon * QMaterialIcon::instance()
{
    static QMaterialIcon mInstance;
    return &mInstance;
}

QChar QMaterialIcon::codepoint(const QString& name, bool* ok)
{
    QMaterialIcon* inst = QMaterialIcon::instance();
    if(inst->codepoints.contains(name)) {
        if(ok) *ok = true;
        return QChar(inst->codepoints.value(name));
    } else {
        if(ok) *ok = false;
        return 0;
    }
}

void QMaterialIcon::setIcon(const QString& name, QAbstractButton* button, const QColor& baseColor, const QString& family)
{
    QMaterialIcon::setIcon(name, name, button, baseColor, family);
}

void QMaterialIcon::setIcon(const QString& name_on, const QString& name_off, QAbstractButton* button, const QColor& baseColor, const QColor& checkColor, const QString& family)
{
#if defined(Q_OS_ANDROID)
    if(!instance()->_sys_icon_size)
    {
        instance()->_sys_icon_size = qApp->style()->pixelMetric(QStyle::PM_LargeIconSize);
        if(!instance()->_sys_icon_size)
        {
            instance()->_sys_icon_size = 24;
        }
    }
#endif
    button->setIcon(QMaterialIcon::multiIcon(name_on, name_off, baseColor, checkColor, family));
#if defined(Q_OS_ANDROID)
    button->setIconSize(QSize(instance()->_sys_icon_size, instance()->_sys_icon_size));
#endif
}

void QMaterialIcon::setIcon(const QString& name, QAction* button, const QColor& baseColor, const QString& family)
{
    QMaterialIcon::setIcon(name, name, button, baseColor, family);
}

void QMaterialIcon::setIcon(const QString& name_on, const QString& name_off, QAction* button, const QColor& baseColor, const QString& family)
{
    button->setIcon(QMaterialIcon::multiIcon(name_on, name_off, baseColor, family));
}

QIcon QMaterialIcon::icon(const QString& name, const QColor& baseColor, const QString& family)
{
    return QMaterialIcon::multiIcon(name, name, baseColor, family);
}

QIcon QMaterialIcon::multiIcon(const QString& name_on, const QString& name_off, const QColor& baseColor, const QColor& checkColor, const QString& family)
{
    if (!instance()->isValid())
    {
        qWarning()<<"Material font not loaded";
        return QIcon();
    }

    QString useFamily = family;
    if (useFamily.isEmpty())
        useFamily = instance()->mfamilies.at(0);


    QFontIconEngine * engine = new QFontIconEngine;
    engine->setFontFamily(useFamily);
    engine->setLetter(QMaterialIcon::codepoint(name_on), QIcon::On);
    engine->setLetter(QMaterialIcon::codepoint(name_off), QIcon::Off);
    engine->setBaseColor(baseColor.isValid() ? baseColor : instance()->base());
    engine->setCheckColor(checkColor.isValid() ? checkColor : instance()->checked());
    return QIcon(engine);
}

void QMaterialIcon::setDefaultColors(bool dark)
{
    setColor(Base, dark ? Qt::white : Qt::black);
    setColor(Checked, dark ? QString("#A5D6A7") : QString("#4CAF50"));
    setColor(Success, dark ? QString("#A5D6A7") : QString("#4CAF50"));
    setColor(Warning, dark ? QString("#FF9800") : QString("#FFCC80"));
    setColor(Error, dark ? QString("#F44336") : QString("#EF9A9A"));
}
