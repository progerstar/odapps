#ifndef LFMEMORYDIALOG_H
#define LFMEMORYDIALOG_H

#include <QDialog>

#include <lfmemoryprotocol.h>

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class RFIDCardReaderOperator;

class LFMemoryDialog : public QDialog
{
        Q_OBJECT
    public:
        explicit LFMemoryDialog(QWidget* parent = nullptr);

        void setOperator(RFIDCardReaderOperator* op);
        void startRead();

    private:
        RFIDCardReaderOperator* m_operator;
        QComboBox* m_scanMode;
        QLineEdit* m_password;
        QLabel* m_status;
        QLabel* m_airProtocol;
        QLabel* m_chipType;
        QLabel* m_evidence;
        QLabel* m_chipInfo;
        QLabel* m_message;
        QProgressBar* m_progress;
        QTableWidget* m_memory;
        QPushButton* m_readButton;

        void setBusy(bool busy);
        void clearResult();
        void showResult(const LFMemoryReadResult& result);
        void readerValidChanged(bool valid);
        QString statusText(const QString& status) const;
        QString airProtocolText(const QString& protocol) const;
};

#endif // LFMEMORYDIALOG_H
