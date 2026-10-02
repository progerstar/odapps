#include "lfmemorydialog.h"

#include <rfidcardreaderoperator.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegExp>
#include <QRegExpValidator>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString emptyValue()
{
    return QString(QChar(0x2014));
}

QTableWidgetItem* memoryItem(const QString& text)
{
    QTableWidgetItem* item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

}

LFMemoryDialog::LFMemoryDialog(QWidget* parent) :
    QDialog(parent), m_operator(nullptr),
    m_scanMode(new QComboBox(this)), m_password(new QLineEdit(this)),
    m_status(new QLabel(this)), m_airProtocol(new QLabel(this)),
    m_chipType(new QLabel(this)), m_evidence(new QLabel(this)),
    m_chipInfo(new QLabel(this)), m_message(new QLabel(this)),
    m_progress(new QProgressBar(this)), m_memory(new QTableWidget(this)),
    m_readButton(new QPushButton(tr("Read Memory"), this))
{
    setWindowTitle(tr("125 kHz Memory"));
    resize(610, 560);

    QVBoxLayout* root = new QVBoxLayout(this);
    QLabel* description = new QLabel(
                tr("Detect and read T55xx, EM4x05, and EM4x50 memory tags."),
                this);
    description->setWordWrap(true);
    root->addWidget(description);

    QGroupBox* optionsGroup = new QGroupBox(tr("Read Options"), this);
    QFormLayout* options = new QFormLayout(optionsGroup);
    m_scanMode->addItem(tr("Fast scan"), false);
    m_scanMode->addItem(tr("Full scan"), true);
    options->addRow(tr("Detection:"), m_scanMode);
    m_password->setMaxLength(8);
    m_password->setPlaceholderText(tr("Optional, 8 HEX characters"));
    m_password->setValidator(new QRegExpValidator(
                                 QRegExp(QStringLiteral("[0-9A-Fa-f]{0,8}")),
                                 m_password));
    options->addRow(tr("T55xx password:"), m_password);
    root->addWidget(optionsGroup);

    QGroupBox* tagGroup = new QGroupBox(tr("Detected Tag"), this);
    QFormLayout* tag = new QFormLayout(tagGroup);
    tag->addRow(tr("Status:"), m_status);
    tag->addRow(tr("Air protocol:"), m_airProtocol);
    tag->addRow(tr("Memory chip:"), m_chipType);
    tag->addRow(tr("Evidence:"), m_evidence);
    tag->addRow(tr("Chip info:"), m_chipInfo);
    root->addWidget(tagGroup);

    m_memory->setColumnCount(5);
    m_memory->setHorizontalHeaderLabels(QStringList()
                                        << tr("Address") << tr("Byte 0")
                                        << tr("Byte 1") << tr("Byte 2")
                                        << tr("Byte 3"));
    m_memory->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for(int column = 1; column < m_memory->columnCount(); ++column)
    {
        m_memory->horizontalHeader()->setSectionResizeMode(
                    column, QHeaderView::ResizeToContents);
    }
    m_memory->verticalHeader()->setVisible(false);
    m_memory->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_memory->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_memory, 1);

    m_message->setWordWrap(true);
    root->addWidget(m_message);
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_progress->hide();
    root->addWidget(m_progress);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close,
                                                     Qt::Horizontal, this);
    buttons->addButton(m_readButton, QDialogButtonBox::ActionRole);
    root->addWidget(buttons);

    connect(m_readButton, &QPushButton::clicked, this, &LFMemoryDialog::startRead);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    clearResult();
}

void LFMemoryDialog::setOperator(RFIDCardReaderOperator* op)
{
    if(m_operator)
    {
        disconnect(m_operator, &RFIDCardReaderOperator::lfMemoryReadFinished,
                   this, &LFMemoryDialog::showResult);
        disconnect(m_operator, &RFIDCardReaderOperator::readerValid,
                   this, &LFMemoryDialog::readerValidChanged);
    }

    m_operator = op;
    if(m_operator)
    {
        connect(m_operator, &RFIDCardReaderOperator::lfMemoryReadFinished,
                this, &LFMemoryDialog::showResult);
        connect(m_operator, &RFIDCardReaderOperator::readerValid,
                this, &LFMemoryDialog::readerValidChanged);
    }
    m_readButton->setEnabled(m_operator && m_operator->supportsLfMemory());
}

void LFMemoryDialog::startRead()
{
    if(!m_operator || !m_operator->supportsLfMemory())
    {
        m_message->setText(
                    tr("The connected interface does not support 125 kHz memory reading."));
        return;
    }

    const QString passwordText = m_password->text().trimmed();
    if(!passwordText.isEmpty() && passwordText.size() != 8)
    {
        QMessageBox::warning(this, tr("125 kHz Memory"),
                             tr("T55xx password must contain exactly 8 HEX characters."));
        return;
    }

    clearResult();
    setBusy(true);
    m_message->setText(tr("Detecting the 125 kHz memory chip…"));
    m_operator->readLfMemory(m_scanMode->currentData().toBool(),
                             QByteArray::fromHex(passwordText.toLatin1()));
}

void LFMemoryDialog::setBusy(bool busy)
{
    m_scanMode->setEnabled(!busy);
    m_password->setEnabled(!busy);
    m_readButton->setEnabled(!busy && m_operator &&
                             m_operator->supportsLfMemory());
    m_progress->setVisible(busy);
}

void LFMemoryDialog::clearResult()
{
    m_status->setText(emptyValue());
    m_airProtocol->setText(emptyValue());
    m_chipType->setText(emptyValue());
    m_evidence->setText(emptyValue());
    m_chipInfo->setText(emptyValue());
    m_message->clear();
    m_memory->setRowCount(0);
}

void LFMemoryDialog::showResult(const LFMemoryReadResult& result)
{
    setBusy(false);
    m_status->setText(statusText(result.classification.status));
    m_airProtocol->setText(airProtocolText(result.classification.airProtocol));
    m_chipType->setText(result.classification.chipType.isEmpty()
                        ? emptyValue() : result.classification.chipType);
    m_evidence->setText(result.classification.evidence.isEmpty()
                        ? emptyValue() : result.classification.evidence);
    m_chipInfo->setText(result.classification.chipInfo.size() == 4
                        ? QString::fromLatin1(
                              result.classification.chipInfo.toHex().toUpper())
                        : emptyValue());

    const int rowCount = qMax(result.blockLabels.size(), result.blocks.size());
    m_memory->setRowCount(rowCount);
    for(int row = 0; row < rowCount; ++row)
    {
        const QString label = row < result.blockLabels.size()
                ? result.blockLabels.at(row) : QString::number(row);
        const QByteArray data = row < result.blocks.size()
                ? result.blocks.at(row) : QByteArray();
        const QString error = row < result.blockErrors.size()
                ? result.blockErrors.at(row) : QString();

        QTableWidgetItem* address = memoryItem(label);
        if(!error.isEmpty())
        {
            address->setToolTip(error);
            address->setForeground(Qt::red);
        }
        m_memory->setItem(row, 0, address);
        for(int byte = 0; byte < 4; ++byte)
        {
            const QString value = data.size() == 4
                    ? QStringLiteral("%1").arg(
                          static_cast<quint8>(data.at(byte)), 2, 16,
                          QLatin1Char('0')).toUpper()
                    : emptyValue();
            QTableWidgetItem* item = memoryItem(value);
            if(!error.isEmpty())
            {
                item->setToolTip(error);
            }
            m_memory->setItem(row, byte + 1, item);
        }
    }

    if(result.success)
    {
        const QString summary = tr("Read %n memory block(s).", "", result.readBlockCount());
        m_message->setText(result.error.isEmpty()
                           ? summary : summary + QLatin1Char(' ') + result.error);
    }
    else
    {
        m_message->setText(result.error.isEmpty()
                           ? tr("The memory could not be read.") : result.error);
    }
}

void LFMemoryDialog::readerValidChanged(bool valid)
{
    if(!valid)
    {
        setBusy(false);
        m_message->setText(tr("The RFID reader was disconnected."));
    }
    else
    {
        m_readButton->setEnabled(m_operator &&
                                 m_operator->supportsLfMemory());
    }
}

QString LFMemoryDialog::statusText(const QString& status) const
{
    if(status == QLatin1String("OK"))
        return tr("Memory chip detected");
    if(status == QLatin1String("AIR_ONLY"))
        return tr("Air protocol only");
    if(status == QLatin1String("NO_SIGNAL"))
        return tr("No signal");
    if(status == QLatin1String("BUSY"))
        return tr("Reader busy");
    return status.isEmpty() ? emptyValue() : status;
}

QString LFMemoryDialog::airProtocolText(const QString& protocol) const
{
    if(protocol == QLatin1String("EM4100_COMPAT"))
        return tr("EM4100-compatible");
    if(protocol == QLatin1String("HID_PROX"))
        return tr("HID Prox");
    if(protocol == QLatin1String("UNKNOWN"))
        return tr("Unknown");
    if(protocol == QLatin1String("NONE"))
        return tr("None");
    return protocol.isEmpty() ? emptyValue() : protocol;
}
