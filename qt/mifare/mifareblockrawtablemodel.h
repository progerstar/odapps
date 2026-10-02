#ifndef MIFAREBLOCKRAWTABLEMODEL_H
#define MIFAREBLOCKRAWTABLEMODEL_H

#include "mifareblockrawtablecolors.h"

#include <QAbstractTableModel>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QHash>

#define DataFormatRole         (Qt::UserRole+1)
#define AccessModeRole         (Qt::UserRole+2)

#include "mifaresector.h"

class MifareBlockRawTableModel : public QAbstractTableModel
{
        Q_OBJECT
        Q_PROPERTY(HeaderStyle headerStyle READ headerStyle WRITE setHeaderStyle)
    public:
        enum HeaderStyle
        {
            Header_Dec,
            Header_Hex,
            Header_ABC
        };
        Q_ENUM(HeaderStyle)

        explicit MifareBlockRawTableModel();
        ~MifareBlockRawTableModel()
        {
            mf_sector = 0;
        }

        //MifareBlockRawTableModel does NOT take ownership of the sector!
        void setSector(MifareSectorInterface* sector);
        inline MifareSectorInterface* sector() const
        {
            return mf_sector;
        }

        inline void setColors(const QString& name)
        {
            colors.load(name);
        }

        inline HeaderStyle headerStyle() const
        {
            return hstyle;
        }

        void setHeaderStyle(HeaderStyle st);

        int rowCount(const QModelIndex &parent = QModelIndex()) const Q_DECL_OVERRIDE
        {
            Q_UNUSED(parent)
            return (mf_sector ? mf_sector->blockCount() : 0 );
        }
        int columnCount(const QModelIndex &parent = QModelIndex()) const Q_DECL_OVERRIDE
        {
            Q_UNUSED(parent)
            /*first col - r/w*/
            return 1 + (mf_sector ? mf_sector->blockSize() : 0);
        }

        QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const Q_DECL_OVERRIDE;
        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const Q_DECL_OVERRIDE;
        bool setData(const QModelIndex & index, const QVariant & value, int role = Qt::EditRole) Q_DECL_OVERRIDE;
        void touch(int block = -1);
        void blockWritten(int block);
        Qt::ItemFlags flags(const QModelIndex & index) const Q_DECL_OVERRIDE;

        QBrush cellBackground(const QModelIndex& index) const;
        QBrush cellForeground(const QModelIndex& index) const;

        inline bool insertRows(int /*row*/, int /*count*/, const QModelIndex & parent = QModelIndex()) Q_DECL_OVERRIDE
        {
            Q_UNUSED(parent)
            return false;
        }
        inline bool removeRows(int /*row*/, int /*count*/, const QModelIndex & parent = QModelIndex()) Q_DECL_OVERRIDE
        {
            Q_UNUSED(parent)
            return false;
        }
        inline bool insertColumns(int /*column*/, int /*count*/, const QModelIndex &parent = QModelIndex()) Q_DECL_OVERRIDE
        {
            Q_UNUSED(parent)
            return false;
        }
        inline bool removeColumns(int /*column*/, int /*count*/, const QModelIndex &parent = QModelIndex()) Q_DECL_OVERRIDE
        {
            Q_UNUSED(parent)
            return false;
        }
    public slots:
        void setFormat(int fmt);
    private:
        MifareSectorInterface* mf_sector;
        HeaderStyle hstyle;
        DataFormat dfmt;
        QColor icon_color;

        MifareBlockRawTableColors colors;
};

#endif // MIFAREBLOCKRAWTABLEMODEL_H
