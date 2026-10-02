#ifndef RAWTABLEMODELITEMDELEGATE_H
#define RAWTABLEMODELITEMDELEGATE_H

#include <QStyledItemDelegate>
#include <QWidget>

class RawTableModelItemDelegate : public QStyledItemDelegate
{
        Q_OBJECT
    public:
        enum CustomEditHint
        {
            EditHint_None = 0,
            EditHint_Up,
            EditHint_Down,
            EditHint_Left,
            EditHint_Right,
            EditHint_Next
        };

        RawTableModelItemDelegate(QObject *parent = 0) : QStyledItemDelegate(parent)
        {
            installEventFilter(this);
        }

        virtual ~RawTableModelItemDelegate(){}

        virtual QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem& /*option*/,
                                      const QModelIndex& index) const Q_DECL_OVERRIDE;

        virtual void setEditorData(QWidget *editor, const QModelIndex &index) const Q_DECL_OVERRIDE;

        virtual void setModelData(QWidget *editor, QAbstractItemModel *model,
                                  const QModelIndex &index) const Q_DECL_OVERRIDE;

        inline virtual void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem& option,
                                                 const QModelIndex& /*index*/) const Q_DECL_OVERRIDE
        {
            editor->setGeometry(option.rect);
        }

        virtual bool eventFilter ( QObject * obj, QEvent * event ) Q_DECL_OVERRIDE;
    signals:
        void customCloseEditor(QWidget* editor, int hints);
};

#endif // RAWTABLEMODELITEMDELEGATE_H
