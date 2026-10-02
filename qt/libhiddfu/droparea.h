#ifndef DROPAREA_H
#define DROPAREA_H

#include <QLabel>
#include <QPaintEvent>

#define SETTINGS_LAST_FILE "UI/DropArea_File"

class DropArea : public QLabel
{
        Q_OBJECT

    public:
        DropArea(QWidget* parent = nullptr);

    public slots:
        void clear();

        inline void setText(const QString&){}

    signals:
        void changed(const QString& file);

    protected:
#ifdef Q_OS_ANDROID
        void mouseReleaseEvent(QMouseEvent* ev) override;
#else
        void mouseDoubleClickEvent(QMouseEvent* ev) override;

        void dragEnterEvent(QDragEnterEvent *event) override;
        void dragMoveEvent(QDragMoveEvent *event) override;
        void dragLeaveEvent(QDragLeaveEvent *event) override;
        void dropEvent(QDropEvent *event) override;
#endif

        void paintEvent(QPaintEvent* e) override;

    private:
#ifdef Q_OS_ANDROID
        void selectFile();
#endif
        bool filled;
};

#endif // DROPAREA_H
