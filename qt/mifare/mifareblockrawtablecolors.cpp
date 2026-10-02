#include "mifareblockrawtablecolors.h"

#include <QSettings>
#include <QStringList>

#define STRINGIFY(x) #x

const QStringList MifareBlockRawTableColors::colorKeys = QStringList()<<STRINGIFY(Editor_Normal)
                                                                      <<STRINGIFY(Editor_Modified)
                                                                      <<STRINGIFY(Cell_ReadProtected)
                                                                      <<STRINGIFY(Cell_WriteProtected)
                                                                      <<STRINGIFY(Cell_CustomEdit)
                                                                      <<STRINGIFY(Cell_Locked)
                                                                      <<STRINGIFY(Cell_Normal);

MifareBlockRawTableColors::MifareBlockRawTableColors(QObject* parent) : QObject(parent)
{
    colors.reserve(Cell_Normal+1);
    QColorPair cp;
    for(int i=0;i<=Cell_Normal;++i)
    {
        cp.data[0] = Qt::white;
        cp.data[1] = Qt::black;
        colors.append(cp);
    }
}

void MifareBlockRawTableColors::load(const QString& name)
{
    QSettings set(name,QSettings::IniFormat);
    colors.clear();
    colors.reserve(Cell_Normal+1);
    QColorPair cp;
    for(int i=0;i<=Cell_Normal;++i)
    {
        cp.data[0] = set.value(colorKeys.at(i)+"/Foreground","#000000").toString();
        cp.data[1] = set.value(colorKeys.at(i)+"/Background","#FFFFFF").toString();
        colors.append(cp);
    }
}

void MifareBlockRawTableColors::save(const QString& file)
{
    QSettings set(file,QSettings::IniFormat);
    for(int i=0;i<=Cell_Normal;++i)
    {
        set.setValue(colorKeys.at(i)+"/Foreground",colors.at(i).data[0].name());
        set.setValue(colorKeys.at(i)+"/Background",colors.at(i).data[1].name());
    }
}

