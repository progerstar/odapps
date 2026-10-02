#ifndef MIFAREBLOCKRAWTABLECOLORS_H_
#define MIFAREBLOCKRAWTABLECOLORS_H_

#include <QObject>
#include <QColor>
#include <QVector>

typedef struct
{
    QColor data[2];
} QColorPair;

class MifareBlockRawTableColors : public QObject
{
        Q_OBJECT
    public:
        enum Group
        {
            Foreground = 0,
            Background = 1
        };

        enum Color
        {
            Editor_Normal    = 0,
            Editor_Modified,
            Cell_ReadProtected,
            Cell_WriteProtected,
            Cell_CustomEdit,
            Cell_Locked,
            Cell_Normal /*Must be THE last one*/
        };

        static const QStringList colorKeys;

        explicit MifareBlockRawTableColors(QObject* parent = 0);

        explicit MifareBlockRawTableColors(const QString& name, QObject* parent = 0) : QObject(parent)
        {
            load(name);
        }

        void load(const QString& name);
        void save(const QString& file);

        inline QColor color(Group group, Color type) const
        {
            return colors.at(type).data[group];
        }

    private:
        Q_DISABLE_COPY(MifareBlockRawTableColors)

        QVector<QColorPair> colors;
};

#endif  // MIFAREBLOCKRAWTABLECOLORS_H_
