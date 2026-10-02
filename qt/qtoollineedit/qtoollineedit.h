#ifndef QFILELINEEDIT_H
#define QFILELINEEDIT_H

#include <QLineEdit>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QToolButton>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QColor>

class QToolLineEdit : public QLineEdit
{
        Q_OBJECT
    public:
        enum Actions
        {
            Clear,
            Paste,
            File,
            Dir
        };

        explicit QToolLineEdit(QWidget* parent = 0, Actions act = QToolLineEdit::Clear);

        void setAction(Actions act);
        inline void setFileFilters(const QString& filters) { fileFilters = filters;}

    public slots:
        void open();
        void openDir();
    protected:
        void resizeEvent(QResizeEvent* e);
        void enterEvent(QEvent*);
        void leaveEvent(QEvent*);

        void dragEnterEvent(QDragEnterEvent* e);
        void dropEvent(QDropEvent* e);
    private slots:
        void updateSize();
    private:
        QToolButton* toolButton;
        QString fileFilters;

        void setupToolButton(const QString& toolTip = "",const QIcon& icon = QIcon());
        void setToolButtonToolTip(const QString& text);
        void setToolButtonIcon(const QIcon& icon);
};

#endif // QFILELINEEDIT_H
