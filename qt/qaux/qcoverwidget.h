#ifndef QCOVERWIDGET_H
#define QCOVERWIDGET_H

#include <QWidget>
#include <QString>
#include <QPaintEvent>

class QCoverWidget : public QWidget
{
        Q_OBJECT
    public:
        explicit QCoverWidget(QWidget *parent = nullptr);
        virtual ~QCoverWidget() {scene = 0;}

        virtual bool eventFilter(QObject *watched, QEvent *event) Q_DECL_OVERRIDE;
    signals:

    public slots:
        virtual void start(QWidget *bg);
        virtual void stop();
    protected:
        QWidget* scene;
};

#endif // QCOVERWIDGET_H
