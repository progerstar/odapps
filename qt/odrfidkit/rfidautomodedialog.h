#ifndef RFIDAUTOMODEDIALOG_H
#define RFIDAUTOMODEDIALOG_H

#include <QDialog>
#include <QFile>
#include <QHash>

#include <rfidcardreaderoperator.h>

namespace Ui {
class RFIDAutoModeDialog;
}

class RFIDAutoModeDialog : public QDialog
{
        Q_OBJECT

    public:
        enum State
        {
            Idle,
            Running,
            Wait_Card,
            Read_Card,
            Write_Card,
        };
        Q_ENUM(State)
        enum ExitCode
        {
            Finished,
            Aborted,
            Disconnected
        };
        Q_ENUM(ExitCode)

        explicit RFIDAutoModeDialog(QWidget *parent = 0);
        ~RFIDAutoModeDialog();

        bool setOperator(RFIDCardReaderOperator* ptr);

        ExitCode exitCode() const
        {
            return ecode;
        }
    private slots:
        void on_runButton_clicked();

        void readerValid(bool on);
        void readerVersion(const QString& v);

        void passwordChanged(bool success, quint32 pwd);
        void packReceived(bool success, quint16 pack);

        void cardStateChanged(RFIDCardReaderOperator::CardState state);
        void cardDetected(const QByteArray& uid, int type);
        void cardRemoved();
        void cardUidChanged();

        void blockRead(int number, QByteArray data, int result);
        void readFinished();
        void blockWritten(int number, int code);
        void writeFinished(bool success);

        void operatorError(RFIDCardReaderOperator::Level lev, const QString& title, const QString& msg);
    private:
        Ui::RFIDAutoModeDialog* ui;
        State state;
        ExitCode ecode;
        RFIDCardReaderOperator* _op;

        QFile scriptFile;
        int currentLine;
        QHash<QString,QString> variableHash;
        QHash<QString,int> labelHash;

        void scriptEOD();
        void scriptCanceled();
        void invalidLine(const QString& reason);
        void processConditionalBlock(const QStringList& block);

        QString getNextLine(bool& ok);
        void nextScriptLine();
        bool gotoLine(int n);
};

#endif // RFIDAUTOMODEDIALOG_H
