#include "mifareblockrawtablemodel.h"

#include <QBrush>
#include <QIcon>

#include <qmaterialfont.h>
#include <themedetector.h>

static inline
QString excelHeader(int section)
{
    QString ret;
    if(section > ('Z'-'A'+1)) {
        ret = excelHeader(section / ('Z'-'A'+1) - 1);
        section = section % ('Z'-'A'+1);
    }
    ret += 'A' + section;
    return ret;
}

MifareBlockRawTableModel::MifareBlockRawTableModel() :
    mf_sector(nullptr), hstyle(Header_Hex), dfmt(DR_Hex)
{
    icon_color = ThemeDetector::iconColor();
}

void MifareBlockRawTableModel::setSector(MifareSectorInterface *sector)
{
    if(mf_sector)
    {
        int cols = columnCount();
        beginRemoveRows(QModelIndex(),0,mf_sector->blockCount()-1);
        mf_sector = nullptr;
        endRemoveRows();

        if(cols)
        {
            beginRemoveColumns(QModelIndex(),1,cols-1);
            endRemoveColumns();
        }
    }
    if(!sector)
        return;

    beginInsertRows(QModelIndex(),0,sector->blockCount()-1);
    mf_sector = sector;
    endInsertRows();

    beginInsertColumns(QModelIndex(),1,sector->blockSize());
    endInsertColumns();
}

void MifareBlockRawTableModel::setHeaderStyle(MifareBlockRawTableModel::HeaderStyle st)
{
    hstyle = st;
    if(sector()) {
        emit headerDataChanged(Qt::Horizontal, 0, columnCount()-1);
        emit headerDataChanged(Qt::Vertical, 0, rowCount()-1);
    }
}

QVariant MifareBlockRawTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    switch(orientation)
    {
        case Qt::Horizontal:
        {
            if(role==Qt::TextAlignmentRole)
                return Qt::AlignCenter;

            if(section == 0)
            {
                switch(role)
                {
                    case Qt::DisplayRole:
                        return tr("Edit:");
                    case Qt::DecorationRole:
                        return MaterialIcon("vpn_key", icon_color);
                    case Qt::ToolTipRole:
                        return tr("Edit Read/Write Access and/or Keys");
                        /*case Qt::BackgroundRole:
                        return QBrush(Qt::blue)*/
                    default:
                        return QVariant();
                }
            }
            else
            {
                switch(role)
                {
                    case Qt::DisplayRole:
                    {
                        switch(hstyle)
                        {
                            case Header_Dec:
                                return QString::number(section-1);
                            case Header_Hex:
                                return QString("%1").arg(section-1,2,16,QLatin1Char('0')).toUpper()+"h";
                            case Header_ABC:
                                return excelHeader(section-1);
                        }
                        break;
                    }
                    default:return QVariant();
                }
            }

            break;
        }
        case Qt::Vertical:
        {
            //qDebug()<<"Requested vertical header for "<<section;
            if(role==Qt::DisplayRole)
            {
                switch(hstyle)
                {
                    case Header_Dec:
                        return QString::number(section);
                    case Header_Hex:
                        return QString("%1").arg(section,2,16,QLatin1Char('0')).toUpper()+"h";
                    case Header_ABC:
                        return excelHeader(section);
                }
            }
            break;
        }
    }
    return QVariant();
}

QBrush MifareBlockRawTableModel::cellBackground(const QModelIndex& index) const
{
    if(!mf_sector)
        return Qt::white;

    if(index.column()==0)
    {
        if(mf_sector->modified(index.row()))
        {
            return colors.color(MifareBlockRawTableColors::Background, MifareBlockRawTableColors::Editor_Modified); //QColor("#14541B");
        }
        else
        {
            return colors.color(MifareBlockRawTableColors::Background, MifareBlockRawTableColors::Editor_Normal); //Qt::white
        }
    }

    if(mf_sector->isEditable(index.row(),index.column()-1, BD_Current))
    {
        if(mf_sector->isReadProtected(index.row(),index.column()-1, BD_Current))
        {
            return colors.color(MifareBlockRawTableColors::Background, MifareBlockRawTableColors::Cell_ReadProtected); //QColor("#9F1110");
        }
        else if(mf_sector->isWriteProtected(index.row(),index.column()-1, BD_Current))
        {
            return colors.color(MifareBlockRawTableColors::Background, MifareBlockRawTableColors::Cell_WriteProtected); //QColor("#05006B");
        }
        else if(mf_sector->customEdit(index.row(),index.column()-1))
        {
            return colors.color(MifareBlockRawTableColors::Background, MifareBlockRawTableColors::Cell_CustomEdit); //QColor("#6B005C");
        }
        else
            return colors.color(MifareBlockRawTableColors::Background, MifareBlockRawTableColors::Cell_Normal); //Qt::white;
    }
    //else
    return colors.color(MifareBlockRawTableColors::Background, MifareBlockRawTableColors::Cell_Locked); //QColor("#383838");
}

QBrush MifareBlockRawTableModel::cellForeground(const QModelIndex& index) const
{
    if(!mf_sector)
        return colors.color(MifareBlockRawTableColors::Background, MifareBlockRawTableColors::Editor_Normal);

    if(index.column()==0)
    {
        if(mf_sector->modified(index.row()))
        {
            return colors.color(MifareBlockRawTableColors::Foreground, MifareBlockRawTableColors::Editor_Modified); //Qt::white;
        }
        else
        {
            return colors.color(MifareBlockRawTableColors::Foreground, MifareBlockRawTableColors::Editor_Normal); //Qt::black;
        }
    }

    if(mf_sector->isEditable(index.row(),index.column()-1, BD_Current))
    {
        if(mf_sector->isReadProtected(index.row(),index.column()-1, BD_Current))
        {
            return colors.color(MifareBlockRawTableColors::Foreground, MifareBlockRawTableColors::Cell_ReadProtected); //Qt::white;
        }
        else if(mf_sector->isWriteProtected(index.row(),index.column()-1, BD_Current))
        {
            return colors.color(MifareBlockRawTableColors::Foreground, MifareBlockRawTableColors::Cell_WriteProtected); //Qt::white;
        }
        else if(mf_sector->customEdit(index.row(),index.column()-1))
        {
            return colors.color(MifareBlockRawTableColors::Foreground, MifareBlockRawTableColors::Cell_CustomEdit); //Qt::white;
        }
        else
        {
            return colors.color(MifareBlockRawTableColors::Foreground, MifareBlockRawTableColors::Cell_Normal); //Qt::black;
        }
    }
    //else
    return colors.color(MifareBlockRawTableColors::Foreground, MifareBlockRawTableColors::Cell_Locked); //Qt::white;
}

QVariant MifareBlockRawTableModel::data(const QModelIndex &index, int role) const
{
#if 1
    switch(role)
    {
        case Qt::BackgroundRole:    return cellBackground(index);
        case Qt::ForegroundRole:    return cellForeground(index);
        case Qt::TextAlignmentRole: return Qt::AlignCenter;
        default:break;
    }
#endif

    if(!mf_sector)
        return QVariant();

    if(index.column()==0)
    {
        if(role==Qt::DisplayRole)
        {
            return mf_sector->customEditName(index.row());
        }
        else if(role==Qt::ToolTipRole)
        {
            QString ttip;
            if(!mf_sector->customEditName(index.row()).isEmpty())
            {
                ttip = tr("Click to open custom editor\nBlock is %1")
                        .arg(mf_sector->modified(index.row())? tr("modified") : tr("not modified"));
            }
            else
            {
                ttip = tr("Block is %1")
                        .arg(mf_sector->modified(index.row())? tr("modified") : tr("not modified"));
            }
            QString aux = mf_sector->blockTooltip(index.row());
            if(!aux.isEmpty())
            {
                ttip += "\n" + aux;
            }
            return ttip;
        }
        return QVariant();
    }
    else
    {
        switch(role)
        {
            case Qt::DisplayRole:
            {
                return numberToString(mf_sector->value(index.row(),index.column()-1),
                                      (mf_sector->forceBinDisplay(index.row(),index.column()-1)? DR_Bin : dfmt));
            }
            case Qt::EditRole:
            {
                if(mf_sector->isEditable(index.row(),index.column()-1, BD_Saved))
                    return numberToString(mf_sector->value(index.row(),index.column()-1),
                                          (mf_sector->forceBinDisplay(index.row(),index.column()-1)? DR_Bin : dfmt));
                break;
            }
            case Qt::ToolTipRole:
            {
                QString ret;
                if(!mf_sector->isEditable(index.row(),index.column()-1, BD_Current) ||
                   mf_sector->customEdit(index.row(),index.column()-1))
                    ret += mf_sector->editHint(index.row(),index.column()-1)+"\n";
                if(mf_sector->isReadProtected(index.row(),index.column()-1, BD_Current))
                    ret += tr("Read Protected")+"\n";
                if(mf_sector->isWriteProtected(index.row(),index.column()-1, BD_Current))
                    ret += tr("Write Protected");
                return ret.trimmed();
            }
            case DataFormatRole:
            {
                return int(mf_sector->forceBinDisplay(index.row(),index.column()-1)? DR_Bin : dfmt);
            }
            case AccessModeRole:
            {
                AccessModes mode = AccessBlocked;
                if(!mf_sector->isReadProtected(index.row(),index.column()-1, BD_Saved))
                    mode |= ReadAccess;
                if(!mf_sector->isWriteProtected(index.row(),index.column()-1, BD_Saved))
                    mode |= WriteAccess;
                return QVariant::fromValue<AccessModes>(mode);
            }
            default:break;
        }
    }

    return QVariant();
}

bool MifareBlockRawTableModel::setData(const QModelIndex & mindex, const QVariant & value, int role)
{
    if(role != Qt::EditRole)
    {
        qDebug()<<"setData failed - only EditRole supported, not "<<role;
        return false;
    }
    if((mindex.column()==0)||!mf_sector)
    {
        qDebug()<<"setData failed - mf_sector==0 or column==0";
        return false;
    }

    if(!mf_sector->isEditable(mindex.row(),mindex.column()-1, BD_Saved))
    {
        qDebug()<<"setData failed - block "<<mindex.row()<<" byte "<<mindex.column()-1<<" is not editable";
        return false;
    }

    bool res;  //(*mf_sector)[mindex.row()].set(mindex.column()-1,value.toString(),dfmt);
    int n = stringToNumber(value.toString(),dfmt,&res);
    if(res && (n>=0) && (n<=255))
    {
        if(n != mf_sector->byte(mindex.row(),mindex.column()-1))
        {
            mf_sector->byte(mindex.row(),mindex.column()-1) = n;
            if(!mf_sector->customEdit(mindex.row(),mindex.column()-1))
            {
                emit dataChanged(mindex,mindex,QVector<int>()<<Qt::DisplayRole<<Qt::EditRole);
            }
            else
            {
                //access bits - any block could have changed
                emit dataChanged(index(0,0),index(rowCount()-1,columnCount()-1));
            }
        }
        return true;
    }
    qDebug()<<"setData failed - value "<<value.toString()<<" is not an integer of out of bounds [0;255]";
    return false;
}

void MifareBlockRawTableModel::touch(int block)
{
    if((block>=0)&&(block<rowCount()))
        emit dataChanged(index(block,1),index(block,columnCount()-1));
    else
        emit dataChanged(index(0,1),index(rowCount()-1,columnCount()-1));
}

void MifareBlockRawTableModel::blockWritten(int block)
{
    if(mf_sector)
    {
        mf_sector->written(block);
        emit dataChanged(index(block,0),index(block,columnCount()-1));
    }
}

Qt::ItemFlags MifareBlockRawTableModel::flags(const QModelIndex & index) const
{
    if((index.column()==0)||!mf_sector)
        return Qt::ItemIsEnabled;

    if(mf_sector->isEditable(index.row(),index.column()-1, BD_Saved) &&
       !mf_sector->customEdit(index.row(),index.column()-1) &&
       !mf_sector->isWriteProtected(index.row(),index.column()-1, BD_Saved))
        return (Qt::ItemIsEnabled|Qt::ItemIsEditable|Qt::ItemIsSelectable);

    return Qt::ItemIsEnabled;
}

void MifareBlockRawTableModel::setFormat(int fmt)
{
    switch(fmt)
    {
        case DR_Bin:
        case DR_Oct:
        case DR_Dec:
        case DR_Hex:
        case DR_ASCII:
        {
            dfmt = (DataFormat)fmt;
            if(mf_sector)
            {
                emit dataChanged(index(0,0),index(rowCount()-1,columnCount()-1),QVector<int>()<<Qt::DisplayRole);
            }
            break;
        }
        default:break;
    }
}
