#ifndef LOCKBITSEDITOR_H
#define LOCKBITSEDITOR_H

#include <QFrame>
#include <QDialog>
#include <QSet>

class QPushButton;
class QLCDNumber;

/*is_otp is not used - all OTP for now*/
class LockBitsEditor : public QFrame
{
        Q_OBJECT
    public:
        explicit LockBitsEditor(bool is_otp = true, QWidget *parent = 0);

        inline bool isOTP() const
        {
            return otp;
        }

        inline quint16 bits16() const
        {
            return (m_bits.size()>=16) ? quint16(bits(16)) : 0;
        }

        inline void setBits16(quint16 bits, const QSet<int>& rfui = QSet<int>())
        {
            setBits(bits,16, rfui);
        }

        inline quint32 bits32() const
        {
            return (m_bits.size()>=32) ? bits(32) : 0;
        }

        inline void setBits32(quint32 bits, const QSet<int>& rfui = QSet<int>())
        {
            setBits(bits,32, rfui);
        }

        //empty list - display 1/0
        void setLabels(const QStringList& labels = QStringList());
    private slots:
        void bitToggled(bool);
    private:
        const bool otp;
        QList<QPushButton*> m_bits;
        QList<QLCDNumber*> m_bytes;
        bool bits_set,labels_set;

        quint32 bits(int cnt) const;
        void setBits(quint32 bits, int cnt, const QSet<int>& rfui = QSet<int>());
};

class LockBitsDialog : public QDialog
{
        Q_OBJECT
    public:
        explicit LockBitsDialog(bool is_otp, QWidget* parent = nullptr);

        LockBitsEditor editor;
};

#endif // LOCKBITSEDITOR_H
