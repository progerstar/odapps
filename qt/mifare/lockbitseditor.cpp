#include "lockbitseditor.h"

#include <QPushButton>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLCDNumber>
#include <QVariant>
#include <QDebug>

#define RFUI_Role "rfui"

LockBitsEditor::LockBitsEditor(bool is_otp, QWidget *parent) : QFrame(parent),
    otp(is_otp), bits_set(false),labels_set(false)
{
    QGridLayout* mlayout = new QGridLayout(this);
    mlayout->setContentsMargins(4,4,4,4);
    mlayout->setSpacing(2);
    setObjectName(QString("LockBitsEditor"));
}

quint32 LockBitsEditor::bits(int cnt) const
{
    quint32 ret = 0;
    for(int i=0;i<cnt;++i)
    {
        ret |= ((!m_bits.at(i)->property(RFUI_Role).toBool() && m_bits.at(i)->isChecked()) ? (1<<i) : 0);
    }
    qWarning()<<"LockBitsEd: compiled bits "<<QString("0x%1").arg(ret,8,16,QLatin1Char('0'));
    return ret;
}

void LockBitsEditor::setBits(quint32 bits, int cnt, const QSet<int>& rfui)
{
    if(bits_set)
        return;
    bits_set=true;

    qWarning()<<"LockBitsEd: set bits "<<QString("0x%1").arg(bits,8,16,QLatin1Char('0'));
    QGridLayout* mlayout = ((QGridLayout*)layout());
    foreach(QPushButton* bit, m_bits)
    {
        mlayout->removeWidget(bit);
    }
    qDeleteAll(m_bits);
    m_bits.clear();

    m_bits.reserve(cnt);

    int rows = ((cnt+7)>>3);
    for(int i=0;i<cnt;++i)
    {
        m_bits.append(new QPushButton(this));
        m_bits[i]->setCheckable(true);
        m_bits[i]->setProperty(RFUI_Role, rfui.contains(i));
        m_bits[i]->setChecked(bits & (1<<i));
        m_bits[i]->setDisabled(m_bits[i]->isChecked());
        //mlayout->addWidget(m_bits[i],rows - (i>>3)-1,1 + 7 -(i%8));
        mlayout->addWidget(m_bits[i], (i>>3), 1 + 7 -(i%8));
        connect(m_bits[i], SIGNAL(toggled(bool)), this, SLOT(bitToggled(bool)));
    }
    for(int i=0;i<rows;++i)
    {
        QLabel* rowLabel = new QLabel(QString("%1-%2").arg(8*i).arg(8*i+7),this);
        rowLabel->setAlignment(Qt::AlignCenter);
        mlayout->addWidget(rowLabel,i,0);
        //mlayout->addWidget(rowLabel, rows-i-1, 0);
        QLCDNumber* num = new QLCDNumber(this);
        //mlayout->addWidget(num, rows-i-1, 9);
        mlayout->addWidget(num, i, 9);
        m_bytes.append(num);
    }

    //force redraw
    bitToggled(true);
}

void LockBitsEditor::setLabels(const QStringList& labels)
{
    if(labels_set)
        return;

    if(m_bits.size() && labels.isEmpty())
    {
        QIcon bitIcon;
        bitIcon.addPixmap(QPixmap(":/mifare/images/bit-on.svg"),QIcon::Normal,QIcon::On);
        bitIcon.addPixmap(QPixmap(":/mifare/images/bit-off.svg"),QIcon::Normal,QIcon::Off);
        setStyleSheet("");
        for(int i=0;i<m_bits.size();++i)
        {
            m_bits[i]->setIcon(bitIcon);
        }
    }
    else if(labels.size()>m_bits.size())
    {
        return;
    }
    else
    {
        int rows = (m_bits.size()+7)>>3;
        QGridLayout* mlayout = ((QGridLayout*)layout());
        for(int i=0;i<rows;++i)
        {
            dynamic_cast<QLabel*>(mlayout->itemAtPosition(i,0)->widget())->setText(tr("Byte %1").arg(i));
        }

        setStyleSheet("LockBitsEditor{border: 1px solid black;border-radius:3px; padding:1px;background-color:black;}"
                      "QPushButton{padding:4px; border:1px solid white; border-radius:2px; background-color:green;}"
                      "QPushButton:checked{background-color:red;}QLabel{color:white;}");

        int min_w = 0;
        int i=0;
        for(;i<labels.size();++i)
        {
            m_bits[i]->setText(labels.at(i));
            if(m_bits[i]->width() > min_w)
                min_w = m_bits[i]->width();
        }
        for(;i<m_bits.size();++i)
        {
            m_bits[i]->setProperty(RFUI_Role, true);
            m_bits[i]->setDisabled(true);
        }
        for(int i=0;i<m_bits.size();++i)
        {
            m_bits[i]->setMinimumWidth(min_w);
        }
    }
    labels_set=true;
    adjustSize();
}

void LockBitsEditor::bitToggled(bool on)
{
    Q_UNUSED(on);
    quint32 value = bits(m_bits.size());
    for(int i=0;i<m_bytes.size();++i)
    {
        m_bytes[i]->display(QString("%1h").arg((value>>(8*i))&0xFF,2,16,QLatin1Char('0')));
    }
}

LockBitsDialog::LockBitsDialog(bool is_otp, QWidget* parent) : QDialog(parent), editor(is_otp, this)
{
    QVBoxLayout* mlayout = new QVBoxLayout(this);
    QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,Qt::Horizontal,this);
    connect(bb,SIGNAL(accepted()),this,SLOT(accept()));
    connect(bb,SIGNAL(rejected()),this,SLOT(reject()));
    mlayout->addWidget(&editor);
    mlayout->addStretch();
    mlayout->addWidget(bb);
    mlayout->setContentsMargins(2,2,2,2);
    mlayout->setSpacing(2);
    adjustSize();
    setWindowTitle(tr("Bits Editor"));
}
