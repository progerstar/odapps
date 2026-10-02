#ifndef QMATERIALFONT_H_
#define QMATERIALFONT_H_

/*
 * Based on QFontIcon by Sacha Schutz (MIT License)
 * (https://github.com/dridk/QFontIcon)
 *
 */

#include <QObject>
#include <QPainter>
#include <QIconEngine>
#include <QApplication>
#include <QAbstractButton>
#include <QAction>
#include <QtCore>
#include <QVector>
#include <QPalette>

class QMaterialIcon;
class QFontIconEngine;

#define MaterialUI         QMaterialIcon::setIcon
#define MaterialPaint(s)   (QMaterialIcon::instance()->color(s))

#define MaterialIcon      QMaterialIcon::icon
#define MaterialOnOffIcon QMaterialIcon::multiIcon

class QFontIconEngine : public QIconEngine
{
    public:
        QFontIconEngine();
        virtual ~QFontIconEngine();
        virtual void paint(QPainter * painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) Q_DECL_OVERRIDE;
        virtual QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) Q_DECL_OVERRIDE;
        void setFontFamily(const QString& family);
        void setLetter(const QChar& letter, QIcon::State mode);
        void setBaseColor(const QColor& baseColor);
        void setCheckColor(const QColor& checkColor);
        virtual QIconEngine* clone() const Q_DECL_OVERRIDE;

    private:
        QString mFontFamily;
        QChar onLetter, offLetter;
        QColor mBaseColor;
        QColor mCheckColor;
};

class QMaterialIcon : public QObject
{
        Q_OBJECT

        Q_PROPERTY(QColor base READ base WRITE setBase)
    public:
        enum Color {
            Base = 0,
            Checked,
            Success,
            Warning,
            Error,
        };
        Q_ENUM(Color)

        static QMaterialIcon * instance();
        inline bool isValid() const {
            return (materialFontID>=0);
        }

        static void setIcon(const QString& name, QAbstractButton* button, const QColor& baseColor = QColor(), const QString& family = QString());
        static void setIcon(const QString& name_on, const QString& name_off, QAbstractButton* button, const QColor& baseColor = QColor(), const QColor& checkColor = QColor(), const QString& family = QString());
        static void setIcon(const QString& name, QAction* button, const QColor& baseColor = QColor(), const QString& family = QString());
        static void setIcon(const QString& name_on, const QString& name_off, QAction* button, const QColor& baseColor = QColor(), const QString& family = QString());
        static QIcon icon(const QString& name, const QColor& baseColor = QColor(), const QString& family = QString());
        static QIcon multiIcon(const QString& name_on, const QString& name_off, const QColor& baseColor = QColor(), const QColor& checkColor = QColor(), const QString& family = QString());


        static QChar codepoint(const QString& name, bool* ok = nullptr);

        inline const QStringList& families() const {
            return mfamilies;
        }

        inline void reinit() {
            deinit();
            init();
        }

        inline bool firstRun() {
             bool ret = _first_init;
             _first_init = false;
             return ret;
        }

        inline const QColor& base() const {
            return _colors.at(Base);
        }

        inline void setBase(const QColor& val) {
            _colors[Base] = val;
        }

        inline const QColor& checked() const {
            return _colors.at(Checked);
        }

        inline void setChecked(const QColor& val) {
            _colors[Checked] = val;
        }

        inline const QColor& color(Color cls) const {
            return _colors.at(cls);
        }

        inline void setColor(Color cls, const QColor& val) {
            _colors[cls] = val;
        }

        void setDefaultColors(bool dark);

    private:
        explicit QMaterialIcon(QObject *parent = nullptr);
        ~QMaterialIcon();
        bool _first_init;
        int _sys_icon_size;
        int materialFontID;
        QStringList mfamilies;
        QHash<QString,quint32> codepoints;
        QVector<QColor> _colors;
        //QColor default_color;
        //QColor checked_color;

        void init();
        void deinit();
};

#endif  // QMATERIALFONT_H_
