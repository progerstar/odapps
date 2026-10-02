#include "bitfieldeditor.h"

#include "qtexttoolbutton.h"
#include <QHBoxLayout>
#include <QButtonGroup>

BitFieldEditor::BitFieldEditor(QWidget *parent) : QWidget(parent), m_mode(Icon), b_size(8)
{
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(1,1,1,1);
    layout->setSpacing(1);
    QIcon bitIcon;
    bitIcon.addPixmap(QPixmap(":/mifare/images/bit-on.svg"),QIcon::Normal,QIcon::On);
    bitIcon.addPixmap(QPixmap(":/mifare/images/bit-off.svg"),QIcon::Normal,QIcon::Off);
    QButtonGroup* bg = new QButtonGroup(this);
    bg->setExclusive(false);
    for(int i=0;i<8;++i)
    {
        QTextToolButton* bit = new QTextToolButton(this);
        bit->setCheckable(true);
        bit->setToolButtonStyle(Qt::ToolButtonIconOnly);
        bit->setIcon(bitIcon);
        bit->setToolTip(tr("Bit %1").arg(i));
        bits.append(bit);
        bg->addButton(bit,i);
        //layout->insertWidget(0,bit);
        layout->addWidget(bit);

        connect(bit,SIGNAL(clicked()),this,SIGNAL(changed()));
    }
    connect(bg,SIGNAL(buttonToggled(int,bool)),this,SIGNAL(bitToggled(int,bool)));

    setStyleSheet("QToolButton{border:none;}");
    setSize(8);
}

BitFieldEditor::~BitFieldEditor()
{
}

void BitFieldEditor::setSize(int size)
{
    if((size>=0)&&(size<=8))
    {
        int i=0;
        for(;i<size;++i)
        {
            bits[i]->setVisible(true);
        }
        for(;i<8;++i)
        {
            bits[i]->setVisible(false);
        }
        b_size = size;
    }
}

quint8 BitFieldEditor::size() const
{
    return b_size;
}

void BitFieldEditor::setMode(Mode mode)
{
    m_mode = mode;
    switch(mode)
    {
        case Icon:
        {
            QIcon bitIcon;
            bitIcon.addPixmap(QPixmap(":/mifare/images/bit-on.svg"),QIcon::Normal,QIcon::On);
            bitIcon.addPixmap(QPixmap(":/mifare/images/bit-off.svg"),QIcon::Normal,QIcon::Off);
            for(int i=0;i<8;++i)
            {
                bits[i]->setToolButtonStyle(Qt::ToolButtonIconOnly);
                bits[i]->setIcon(bitIcon);
            }

            setStyleSheet("QToolButton{border:none;}");
            break;
        }
        case Text:
        {
            for(int i=0;i<8;++i)
            {
                bits[i]->setToolButtonStyle(Qt::ToolButtonTextOnly);
            }

            setStyleSheet("QToolButton{background-color: #858585; border-color: white; border-style: solid;"
                          "border-top-width: 1px; border-right-width: 0px; "
                          "border-bottom-width: 1px; border-left-width: 1px;"
                          "color: #DDDDDD;}"
                          "QToolButton:checked{background-color: #A9D15F; color: black;}"
                          "QToolButton:disabled{background-color: #A22626; color: white;}");
            break;
        }
    }
}

void BitFieldEditor::setLabels(const QStringList &onLabels, const QStringList& offLabels)
{
    if(onLabels.size() == offLabels.size())
    {
        for(int i=0;i<qMin<int>(8,onLabels.size());++i)
        {
            bits[i]->setTexts(onLabels.at(i),offLabels.at(i));
        }
    }
}

void BitFieldEditor::setLabels(const QString& on, const QString& off)
{
    for(int i=0;i<8;++i)
    {
        bits[i]->setTexts(on,off);
    }
}

void BitFieldEditor::setToolTips(const QStringList &tooltips)
{
    if(tooltips.size()==1)
    {
        for(int i=0;i<8;++i)
        {
            bits[i]->setToolTip(tooltips.at(0).arg(i+1));
        }
    }
    else if(tooltips.size()==8)
    {
        for(int i=0;i<8;++i)
        {
            bits[i]->setToolTip(tooltips.at(i));
        }
    }
}

quint8 BitFieldEditor::read() const
{
    quint8 ret = 0;
    for(int i=0;i<b_size;++i)
    {
        if(bits.at(i)->isChecked())
        {
            ret |= (1<<i);
        }
    }
    return ret;
}

void BitFieldEditor::write(quint8 value)
{
    for(int i=0;i<8;++i)
    {
        bits[i]->setChecked(value & (1<<i));
    }
}

void BitFieldEditor::setBit(int bit, bool on)
{
    if((bit>=0)&&(bit<8))
        bits[bit]->setChecked(on);
}

bool BitFieldEditor::getBit(int bit) const
{
    if((bit>=0)&&(bit<8))
        return bits[bit]->isChecked();
    return false;
}

void BitFieldEditor::setBlocked(int bit, bool locked)
{
    bits[bit]->setDisabled(locked);
}
