#ifndef BITFIELDEDITOR_H
#define BITFIELDEDITOR_H

#include <QWidget>

class QTextToolButton;

class BitFieldEditor : public QWidget
{
        Q_OBJECT

    public:
        enum Mode
        {
            Icon,
            Text
        };

        explicit BitFieldEditor(QWidget *parent = 0);
        ~BitFieldEditor();

        void setSize(int size);
        quint8 size() const;

        void setMode(Mode mode);

        void setLabels(const QStringList& onLabels, const QStringList& offLabels);
        void setLabels(const QString& on, const QString& off);
        void setToolTips(const QStringList& tooltips);

        quint8 read() const;
        void write(quint8 value);

        void setBit(int bit, bool on);
        bool getBit(int bit) const;
        void setBlocked(int bit, bool locked);
    signals:
        void changed();
        void bitToggled(int bit, bool on);
    private:
        QList<QTextToolButton*> bits;
        Mode m_mode;
        quint8 b_size;
};

#endif // BITFIELDEDITOR_H
