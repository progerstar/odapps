#ifndef ODHIDDFUMAIN_H
#define ODHIDDFUMAIN_H

#include <hiddfu.h>

#ifndef Q_OS_ANDROID
#endif

#include <QMainWindow>

class HidDFUParams {
    public:
        HidDFUParams() :
            meijin(false), auto_mode(false), stmsw(false),
            baudrate(0), address(-1)
        {

        }

        bool meijin;
        bool auto_mode;
        bool stmsw;

        QString type;
        QString product;
        QString fw_file;

        QString medium;
        quint32 baudrate;
        int address;

        /*internal*/
        QString transport;
};

namespace Ui {
class ODHidDFUMain;
}

class ODHidDFUMain : public QMainWindow
{
        Q_OBJECT

    public:
        explicit ODHidDFUMain(const HidDFUParams& params);
        ~ODHidDFUMain();

    public slots:
        void execute(const QStringList& products = QStringList());
    private slots:
        void setup();
        void on_actionAbout_triggered();
        void on_actionDisconnect_triggered();
        void dfuSelected();
        void dfuFinished();

    private:
        Ui::ODHidDFUMain *ui;
        HIDDfu* d_instance;
        HidDFUParams cmd_opts;



        bool selectSerialPort();
        bool selectDeviceAddress();
        bool selectBaudRate();
};

#endif // ODHIDDFUMAIN_H
