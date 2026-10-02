#include "rfidkitwindow.h"
#include <ui_rfidkitwindow.h>

#include "connectionpopup.h"
#include "qrfidpreferences.h"
#include "uidchangedialog.h"
#include "emclonedialog.h"
#include "lfmemorydialog.h"
#include "rfidautomodedialog.h"
#include <rs485setupdialog.h>

#include <mifareclassicvalueeditor.h>
#include <mifareclassictrailereditor.h>
#include <mifarentagconfigeditor.h>
#include <mifareultralightev1configeditor.h>
#include <mifareultralightpasswordeditor.h>
#include <em4100data.h>
#include <lockbitseditor.h>
#include <mifarekeydatabase.h>

#include <rfidcardsimulatorreader.h>
#include <rfidcardcdcatreader.h>
#include <rfidcardhidreader.h>

#include <qautoclosemessagebox.h>

#include <themedetector.h>
#include <qmaterialfont.h>

#include <QFileDialog>
#include <QSaveFile>
#include <QButtonGroup>
#include <QCheckBox>
#include <QInputDialog>
#include <QStandardPaths>
#include <QClipboard>

namespace {
constexpr int MaxNetworkCommandSize = 4096;
constexpr int MaxNetworkClients = 128;

quint32 msbBits(const QByteArray& data, int start, int count)
{
    quint32 value = 0;
    for(int bit = 0; bit < count; ++bit)
    {
        const int bitIndex = start + bit;
        value = (value << 1) |
                ((static_cast<quint8>(data.at(bitIndex / 8)) >>
                  (7 - (bitIndex % 8))) & 1u);
    }
    return value;
}
}

void TcpClient::tcpReadyRead()
{
    buffer.append(fd->readAll());
    int i = 0;
    while(!buffer.isEmpty() && ((i = buffer.indexOf('\n'))>=0))
    {
        if(i > MaxNetworkCommandSize)
        {
            qWarning()<<"Disconnecting TCP client with an oversized command";
            fd->disconnectFromHost();
            return;
        }
        if(i==0)
        {
            buffer.remove(0,1);
            continue;
        }

        QByteArray msg = buffer.left(i);
        if(msg.endsWith('\r'))
        {
            msg.chop(1);
        }
        if(!msg.isEmpty())
        {
            emit newDatagram(msg);
        }
        while((i<buffer.size())&&(buffer[i]=='\n')) ++i;
        buffer.remove(0,i);
    }

    if(buffer.size() > MaxNetworkCommandSize)
    {
        qWarning()<<"Disconnecting TCP client with an oversized partial command";
        fd->disconnectFromHost();
    }
}

void TcpClient::tcpDisconnected()
{
    if(fd)
    {
        fd->disconnect(this);
    }
    emit closed();
}

RFIDKitWindow::RFIDKitWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::RFIDKitWindow), exit_code(Boot_Normal), exit_reader_subtype(RFIDCARDHIDREADER_ID),
    _op(new RFIDCardReaderOperator(this)),
    mfc_model(new MifareBlockRawTableModel), mfc_sector(nullptr),
    ledTimer(this),
    key_db(MifareKeyDatabase::instance()),
    ultralight_password_rejected(false)
{
    ui->setupUi(this);
    ui->sectorList->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->sectorGroup->setVisible(false);
    setTagToolsEnabled(false);
    ui->aclStack->setCurrentWidget(ui->aclNilPage);
    ui->cardStack->setCurrentWidget(ui->stubPage);

    MaterialUI("link", ui->actionConnect, MaterialPaint(QMaterialIcon::Success));
    MaterialUI("content_copy", ui->actionCopy_UID);
    MaterialUI("edit", ui->modUIDTool);
    MaterialUI("content_copy", ui->mfcKeyCopyTool);
    MaterialUI("content_paste", ui->mfcKeyPasteTool);
    MaterialUI("upload", ui->actionRead_Tag);
    MaterialUI("skip_next", ui->actionNext_Tag);
    MaterialUI("download", ui->actionWrite_Tag);
    MaterialUI("memory", ui->actionLF_Memory);
    ui->actionLF_Memory->setEnabled(false);

    progress_display = new QProgressDisplay(this);
    ui->menu_Emulator->setEnabled(false);
    ui->modUIDTool->setVisible(false);

#ifndef Q_OS_DARWIN
    ui->modUIDTool->setAutoRaise(true);
#endif

    ui->sectorTable->setModel(mfc_model);
    ui->sectorTable->horizontalHeader()->show();
    ui->sectorTable->verticalHeader()->show();
    ui->sectorTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    connect(ui->actionExit,SIGNAL(triggered()),this,SLOT(close()));

    connect(ui->actionAbout,SIGNAL(triggered()),this,SLOT(about()));
    connect(ui->actionAbout_Qt,SIGNAL(triggered()),qApp,SLOT(aboutQt()));

    ui->cardStack->setCurrentIndex(SI_NotSupported);

    ui->actionFWUpdate->setVisible(false);

    ui->menu_View->insertSeparator(ui->actionBinary)->setText(tr("Format"));
    fmtActGroup = new QActionGroup(this);
    fmtActGroup->addAction(ui->actionBinary);
    fmtActGroup->addAction(ui->actionOctal);
    fmtActGroup->addAction(ui->actionDecimal);
    fmtActGroup->addAction(ui->actionHexadecimal);
    fmtActGroup->addAction(ui->actionASCII);
    ui->actionBinary->setData(int(DR_Bin));
    ui->actionOctal->setData(int(DR_Oct));
    ui->actionDecimal->setData(int(DR_Dec));
    ui->actionHexadecimal->setData(int(DR_Hex));
    ui->actionASCII->setData(int(DR_ASCII));
    connect(fmtActGroup, &QActionGroup::triggered, this, &RFIDKitWindow::formatActionTriggered);

    hdrActGroup = new QActionGroup(this);
    hdrActGroup->addAction(ui->actionHdrDecimal);
    hdrActGroup->addAction(ui->actionHdrHexadecimal);
    hdrActGroup->addAction(ui->actionHdrABC);
    ui->actionHdrDecimal->setData(QVariant::fromValue(MifareBlockRawTableModel::Header_Dec));
    ui->actionHdrHexadecimal->setData(QVariant::fromValue(MifareBlockRawTableModel::Header_Hex));
    ui->actionHdrABC->setData(QVariant::fromValue(MifareBlockRawTableModel::Header_ABC));
    connect(hdrActGroup, &QActionGroup::triggered, this, &RFIDKitWindow::headerFormatActionTriggered);

    ui->cardUIDLabel->setMinimumWidth(QFontMetrics(font()).size(Qt::TextSingleLine,"FFFFFFFFFFFFFF").width());
    ledTimer.setSingleShot(false);
    ledTimer.setInterval(500);
    connect(&ledTimer,SIGNAL(timeout()),this,SLOT(led_timeout()));

    qDebug()<<"MFC default key "<<ui->mfcKeyEdit->text();

    ui->mfcKeyCombo->setCurrentIndex(MifareClassicKeyA);
    connect(ui->mfcKeyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RFIDKitWindow::mfcKeyTypeSwitched);

    ledTimer.start();
    ui->actionNext_Tag->setEnabled(false);

    connect(ui->mfcKeyCopyTool, &QToolButton::clicked, [=](){
        qApp->clipboard()->setText(this->ui->mfcKeyEdit->text());
    });
    connect(ui->mfcKeyPasteTool, &QToolButton::clicked, [=](){
        this->ui->mfcKeyEdit->setText(qApp->clipboard()->text());
    });

    connect(&m_udp,SIGNAL(readyRead()),this,SLOT(udp_ready_read()));
    m_tcp = new QTcpServer(this);
    connect(m_tcp, SIGNAL(newConnection()), this, SLOT(tcp_newConnection()));

    connectOperator();

    readSettings(true);
}

RFIDKitWindow::~RFIDKitWindow()
{
    writeSettings();
    closeUDP();
    tcpStop();
    _op->close();
    progress_display->stop();
    delete progress_display;
    delete ui;
}

void RFIDKitWindow::setDevMode(bool on)
{
    //ui->menuTools->menuAction()->setVisible(on);
    ui->actionAuto_Mode->setVisible(on);
}

void RFIDKitWindow::readSettings(bool init)
{
    if(init) {
        restoreGeometry(set.value(SETTINGS_WIN_GEOM).toByteArray());
        restoreState(set.value(SETTINGS_WIN_STATE).toByteArray());
    }

    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString themeFile = appDataPath.isEmpty() ? QString() : appDataPath + QLatin1String("/theme.ini");
    if(!themeFile.isEmpty() && QFile(themeFile).exists())
    {
        qDebug()<<"Loading custom colors from "<<themeFile;
        mfc_model->setColors(themeFile);
    }
    else
    {
        if(appDataPath.isEmpty())
        {
            qWarning()<<"Application data location is unavailable";
        }
        qDebug()<<"Loading build-in colors";
        mfc_model->setColors(ThemeDetector::isDarkThemeEnabled() ? ":/mifare/themes/default_dark.ini" : ":/mifare/themes/default.ini");
    }

    if(set.value(SETTINGS_UDP,false).toBool()) {
        m_udp.close();
        qDebug()<<"Binding to "<<set.value(SETTINGS_UDP_PORT, 12345).toInt();
        if(!m_udp.bind(set.value(SETTINGS_UDP_PORT, 12345).toInt(), QUdpSocket::ReuseAddressHint|QUdpSocket::DontShareAddress))
        {
            qDebug()<<"Bind failed: "<<m_udp.errorString();
            statusBar()->showMessage(tr("UDP server startup failed: %1").arg(m_udp.errorString()), 10000);
        }
        else
        {
            qDebug()<<"Bound to "<<set.value(SETTINGS_UDP_PORT, 12345).toInt();
        }
    } else {
        closeUDP();
    }

    tcpStop();
    if(set.value(SETTINGS_TCP, false).toBool()) {
        tcpStart();
    }

    {
        QAction* cellViewAct = nullptr;
        switch(set.value(SETTINGS_VIEW_CELLS, DR_Hex).toInt())
        {
            case DR_Bin: cellViewAct = ui->actionBinary;break;
            case DR_Oct: cellViewAct = ui->actionOctal;break;
            case DR_Dec: cellViewAct = ui->actionDecimal;break;
            default:
            case DR_Hex: cellViewAct = ui->actionHexadecimal;break;
            case DR_ASCII: cellViewAct = ui->actionASCII;break;
        }
        cellViewAct->setChecked(true);
        formatActionTriggered(cellViewAct);
    }

    {
        QAction* hdrViewAct = nullptr;
        switch(set.value(SETTINGS_VIEW_HEADERS, MifareBlockRawTableModel::Header_Hex).toInt())
        {
            case MifareBlockRawTableModel::Header_Dec: hdrViewAct = ui->actionHdrDecimal;break;
            default:
            case MifareBlockRawTableModel::Header_Hex: hdrViewAct = ui->actionHdrHexadecimal;break;
            case MifareBlockRawTableModel::Header_ABC: hdrViewAct = ui->actionHdrABC;break;
        }
        hdrViewAct->setChecked(true);
        headerFormatActionTriggered(hdrViewAct);
    }
}

void RFIDKitWindow::writeSettings()
{
    set.setValue(SETTINGS_VIEW_CELLS, fmtActGroup->checkedAction()->data().toInt());
    set.setValue(SETTINGS_VIEW_HEADERS, int(hdrActGroup->checkedAction()->data().value<MifareBlockRawTableModel::HeaderStyle>()));

    set.setValue(SETTINGS_WIN_GEOM, saveGeometry());
    set.setValue(SETTINGS_WIN_STATE, saveState());
}

BootMode RFIDKitWindow::exitMode(QString& dfuTransport, QStringList& dfuProducts) const
{
    dfuProducts.clear();
    if(exit_code == Boot_DFU)
    {
        switch(exit_reader_subtype & 0xFF)
        {
            case RFIDCARDUARTREADER_ID:
            case RFIDCARDRS485READER_ID:
            {
                dfuTransport = "rs485";
                dfuProducts.append("UART DFU");
                break;
            }
            default:
            {
                dfuTransport = "usb";
                dfuProducts.append({"WDG DFU", "DFU HID Interface"});
                break;
            }
        }
    }
    return exit_code;
}

void RFIDKitWindow::closeEvent(QCloseEvent* ev)
{
    if(_op->isValid() && mfc_sector && mfc_sector->modified())
    {
        if(QMessageBox::question(this, tr("Pending changes"),
                                 tr("Some changes have not been written to the currently edited tag and will be lost. Close the application?"),
                                 QMessageBox::Yes|QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        {
            ev->ignore();
            return;
        }
    }
    ev->accept();
}

void RFIDKitWindow::readerValid(bool on)
{
    ui->actionLF_Memory->setEnabled(on && _op->supportsLfMemory());
    if(!on)
    {
        ui->readerVersionLabel->setText(tr("not connected"));
        currentReaderInvalidated();
        _reader_version = QVersionNumber();
        qWarning()<<"Reader version invalidated";

        progress_display->stop();
        ui->rfidLed->off();
        ledTimer.stop();

        ui->menu_Emulator->setEnabled(false);
        ui->actionFWUpdate->setVisible(false);
        statusBar()->showMessage(tr("Connection closed"), 5000);

        ui->actionConnect->setText(tr("Connect"));
        MaterialUI("link", ui->actionConnect, MaterialPaint(QMaterialIcon::Success));
    }
    else
    {
        statusBar()->showMessage(tr("Reader connected"), 2000);
        ui->actionConnect->setText(tr("Disconnect"));
        MaterialUI("link_off", ui->actionConnect, MaterialPaint(QMaterialIcon::Warning));

        if(_op->type() == RFIDCardReaderInterface::RFIDReader_Hardware)
        {
            progress_display->setText(tr("Scanning…"));
            progress_display->start(ui->sectorTable);

            ledTimer.start();
            ui->actionFWUpdate->setVisible(true);
            ui->actionNext_Tag->setEnabled(true);
        }
        else
        {
            progress_display->setText(tr("Waiting..."));
            progress_display->start(ui->sectorTable);
            on_actionEmulator_Open_triggered();
        }
    }
}

void RFIDKitWindow::readerVersionChanged(const QString& v, const QVersionNumber& vn)
{
    ui->readerVersionLabel->setText(v);
    qWarning()<<"Reader version: "<<vn;
    _reader_version = vn;
}

void RFIDKitWindow::readerPasswordChanged(bool success, quint32 pwd)
{
    qWarning()<<"Reader pwd changed: success "<<success<<", pwd "<<pwd;
    if(success)
    {
        ui->ulevPwdEdit->setText(QString("%1").arg(pwd, 8, 16, QLatin1Char('0')).toUpper());
    }
}

void RFIDKitWindow::operatorError(RFIDCardReaderOperator::Level lev, const QString& title, const QString& msg)
{
    switch(lev)
    {
        case RFIDCardReaderOperator::Debug:
        {
            qWarning()<<title<<": "<<msg;
            break;
        }
        case RFIDCardReaderOperator::Info:
        {
            statusBar()->showMessage(msg,2000);
            break;
        }
        case RFIDCardReaderOperator::Warning:
        {
            QMessageBox::warning(this, title, msg);
            break;
        }
        case RFIDCardReaderOperator::Critical:
        {
            QMessageBox::critical(this, title, msg);
            break;
        }
    }
}

void RFIDKitWindow::cardDetected(const QByteArray& uid, int type)
{
    //current_reader is valid
    qDebug()<<"RFIDKitWindow: cardDetected - current reader is "<<_op->type()<<", uid "<<uid.toHex().toUpper();
    progress_display->stop();

    ui->cardUIDLabel->setText(QString::fromLatin1(uid.toHex().toUpper()));

    /* Changeable UID*/
    {
        ui->cardTypeLabel->setText(mifareCardName(type));
        if(isHidProxCard(type) && uid.size() == 6)
        {
            const quint32 facilityCode = msbBits(uid, 19, 8);
            const quint32 cardNumber = msbBits(uid, 27, 16);
            ui->cardTypeLabel->setToolTip(
                        tr("Wiegand 26: %1.%2")
                        .arg(facilityCode)
                        .arg(cardNumber, 5, 10, QLatin1Char('0')));
        }
        else if(uid.size()>=7)
        {
            //byte - manufacturer ID
            ui->cardTypeLabel->setToolTip(icManufacturerDB.value(int(static_cast<uint8_t>(uid.at(0))), tr("Unknown Manufacturer")));
        }
        else if(uid.size() == 4)
        {
            uint8_t uid0 = static_cast<uint8_t>(uid.at(0));
            if(uid0 == 0x08)
            {
                ui->cardTypeLabel->setToolTip(tr("Random UID"));
            }
            else if((uid0 & 0x0F) == 0xF)
            {
                ui->cardTypeLabel->setToolTip(tr("Non-unique fixed UID"));
            }
        }
        else
        {
            ui->cardTypeLabel->setToolTip(QString());
        }
    }

    if((_op->type() != RFIDCardReaderInterface::RFIDReader_Simulator) /*&& (last_uid != current_reader->getUID())*/)
    {
        if(set.value(SETTINGS_UDP,false).toBool())
        {
            udpBroadcast(QByteArray("card> type: ")+
                         mifareCardName(type).toUtf8()+
                         QByteArray("; uid: ")+uid.toHex().toUpper()
                         +QByteArray("\n"));
        }
        if(set.value(SETTINGS_TCP,false).toBool())
        {
            tcpBroadcast(QByteArray("card> type: ")+
                         mifareCardName(type).toUtf8()+
                         QByteArray("; uid: ")+uid.toHex().toUpper()
                         +QByteArray("\n"));
        }
    }

    ui->aclStack->setCurrentWidget(ui->aclNilPage);
    ui->cardStack->setCurrentIndex(SI_NotSupported);
    setTagToolsEnabled(false);

    if(isClassicCard(type) || (type==MF_PLUS_X_SL3)||(type==MF_PLUS_S_SL3))
    {
        bool is_plus_sl3 = ((type==MF_PLUS_X_SL3) || (type==MF_PLUS_S_SL3));
        if(is_plus_sl3 &&
           (
               (_reader_version <= QVersionNumber(1,5, RFIDCardReaderInterface::REV_FirstGen)) ||
               ((_reader_version >= QVersionNumber(1,6, RFIDCardReaderInterface::REV_FirstGen)) &&
                (_reader_version.microVersion() == RFIDCardReaderInterface::REV_SecondGen_HighLow))
           )
        ) {
            /* Plus is not supported */
            qWarning()<<"Reader "<<_reader_version<<" does not support PlusX";
            return;
        }

        setTagToolsEnabled(true);
        ui->mfcKeyEdit->setClass(is_plus_sl3 ? MifareClassicKeyEdit::KC_Plus : MifareClassicKeyEdit::KC_Classic);
        ui->cardStack->setCurrentIndex(SI_Classic);

        qWarning()<<"Mifare Classic sectors: [0 - "<<(classicSectorCount(type)-1)<<"]";
        if(classicSectorCount(type) != ui->sectorList->count())
        {
            ui->sectorList->blockSignals(true);
            ui->sectorList->clear();
            for(int si=0;si<classicSectorCount(type);++si) {
                ui->sectorList->addItem(tr("Sector %1").arg(si));
            }
            ui->sectorList->blockSignals(false);
            ui->sectorList->setMaximumWidth(ui->sectorList->sizeHintForColumn(0) + 2 * (16 + ui->sectorList->frameWidth()));
            ui->sectorGroup->setVisible(true);
        }
        ui->aclStack->setCurrentWidget(ui->aclClPlusPage);

        if(last_uid != uid)
        {
            ui->sectorList->setCurrentRow(-1);
            last_uid = uid;
            ui->sectorList->setCurrentRow(0);
        }
        else if((_op->type()==RFIDCardReaderInterface::RFIDReader_Simulator) ||
                ((ui->sectorList->currentRow() >= 0) && key_db->tryRead(MifareClassicKeyID(uid, ui->sectorList->currentRow()))))
        {
            qDebug()<<"UID "<<MifareKeyDatabase::uidText(uid)<<" is not flagged - try read";
            on_actionRead_Tag_triggered();
        }
    }
    else if(isUltralightCard(type))
    {
        ui->sectorList->blockSignals(true);
        ui->sectorList->clear();
        ui->sectorList->blockSignals(false);
        ui->sectorGroup->setVisible(false);

        last_uid = uid;
        if(type==MF_ULTRALIGHT_C)
        {
            return;
        }

        setTagToolsEnabled(true);
        ui->cardStack->setCurrentIndex(SI_Ultralight);
        if(isUltralightEV1Card(type))
        {
            ui->aclStack->setCurrentWidget(ui->aclUltralightPage);
            if(set.value(SETTINGS_KEYS, false).toBool())
            {
                bool has_key = false;
                MifareUltralightEV1Security sec = key_db->ultralight(uid,&has_key);
                if(has_key)
                {
                    qWarning()<<"Found password for ultralight card "<<uid;
                }
                ui->ulevPwdEdit->setText(QString("%1").arg(sec.password,8,16,QLatin1Char('0')).toUpper());
                _op->setPassword(sec.password);
            }
        }
        else
        {
            ui->aclStack->setCurrentWidget(ui->aclNilPage);
        }

        ui->modUIDTool->setVisible(_op->type() == RFIDCardReaderInterface::RFIDReader_Simulator);
        on_actionRead_Tag_triggered();
    }
    else if(isLfCard(type))
    {
        ui->sectorList->blockSignals(true);
        ui->sectorList->clear();
        ui->sectorList->blockSignals(false);
        ui->sectorGroup->setVisible(false);

        last_uid = uid;
        setTagToolsEnabled(true);
        ui->cardStack->setCurrentIndex(SI_EmMarine);
        ui->aclStack->setCurrentWidget(ui->aclNilPage);

        clearModel();
        mfc_sector = new EM4100Data(uid, static_cast<MifareCards>(type));
        cardStateChanged(RFIDCardReaderOperator::CS_ReadOK);

        ui->emCloneButton->setVisible(isEmMarineCard(type));
        ui->actionRead_Tag->setEnabled(_op->supportsLfMemory());
        ui->actionWrite_Tag->setEnabled(false);
    }
    else
    {
        last_uid = uid;
        clearModel();
        ui->modUIDTool->setVisible(false);
    }
}

void RFIDKitWindow::cardStateChanged(RFIDCardReaderOperator::CardState state)
{
    qWarning()<<"Card state changed to "<<state;
    switch(state) {
        case RFIDCardReaderOperator::CS_NoCard: {
            currentReaderInvalidated();
            progress_display->setText(tr("Scanning…"));
            progress_display->start(ui->sectorTable);
            break;
        }
        case RFIDCardReaderOperator::CS_Idle:
        case RFIDCardReaderOperator::CS_ReadFailed: {
            ui->actionNext_Tag->setEnabled(true);
            ui->actionRead_Tag->setEnabled(!isLfCard(_op->card()) ||
                                           _op->supportsLfMemory());
            setTagToolsEnabled(true);
            break;
        }
        case RFIDCardReaderOperator::CS_ReadPend:
        case RFIDCardReaderOperator::CS_Writing: {
            setTagToolsEnabled(false);
            ui->actionRead_Tag->setEnabled(false);
            ui->actionWrite_Tag->setEnabled(false);
            ui->actionNext_Tag->setEnabled(false);
            break;
        }
        case RFIDCardReaderOperator::CS_ReadOK: {
            progress_display->stop();
            card_read_done();
            ui->actionWrite_Tag->setEnabled(!isLfCard(_op->card()));
            break;
        }
        case RFIDCardReaderOperator::CS_Written: {
            card_write_done(true);
            ui->actionWrite_Tag->setEnabled(true);
            break;
        }
        case RFIDCardReaderOperator::CS_WriteFailed: {
            card_write_done(false);
            ui->actionWrite_Tag->setEnabled(true);
            break;
        }
    }
}

void RFIDKitWindow::on_actionNext_Tag_triggered()
{
    if(_op->type() != RFIDCardReaderInterface::RFIDReader_Hardware) {
        return;
    }

    if(mfc_sector && (mfc_sector->modified()))
    {
        if(QMessageBox::question(this, tr("Rescan Tag"), tr("All the current modifications will be lost. Proceed?"),
                                 QMessageBox::Yes|QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }

    /*Next Card*/
    _op->openCard(QString());
}

void RFIDKitWindow::on_modUIDTool_clicked()
{
    if(!_op->isValid()) {
        return;
    }

    UIDChangeDialog dlg(this);
    dlg.setup(_op->uid());
    if(dlg.exec() == QDialog::Accepted)
    {
        _op->setUID(dlg.uid());
    }
}

void RFIDKitWindow::on_actionCopy_UID_triggered()
{
    if(!ui->cardUIDLabel->text().isEmpty())
    {
        qApp->clipboard()->setText(ui->cardUIDLabel->text());
        statusBar()->showMessage(tr("UID copied to clipboard"), 2000);
    }
}

void RFIDKitWindow::on_actionRead_Tag_triggered()
{
    if(!_op->isValid() || _op->uid().isEmpty()) {
        //BUG - should not have happened
        qWarning("Called 'read_tag' in invalid tag1");
        cardStateChanged(RFIDCardReaderOperator::CS_NoCard);
    } else if(isClassicCard(_op->card())) {
        mfc_classic_read();
    } else if((_op->card() == MF_PLUS_S_SL3) || (_op->card() == MF_PLUS_X_SL3)) {
        mfc_plus_read();
    } else if(isUltralightCard(_op->card())) {
        ul_read();
    } else if(isLfCard(_op->card())) {
        on_actionLF_Memory_triggered();
    } else {
        //BUG - should not have happened
        qWarning("Called 'read_tag' in invalid tag2");
        cardStateChanged(RFIDCardReaderOperator::CS_NoCard);
    }
}

void RFIDKitWindow::on_actionLF_Memory_triggered()
{
    if(!_op->supportsLfMemory())
    {
        statusBar()->showMessage(
                    tr("The connected interface does not support 125 kHz memory reading"),
                    5000);
        return;
    }

    LFMemoryDialog dialog(this);
    dialog.setOperator(_op);
    dialog.startRead();
    dialog.exec();
}

void RFIDKitWindow::on_actionWrite_Tag_triggered()
{
    if(!_op->isValid() || _op->uid().isEmpty()) {
        //BUG - should not have happened
        qWarning("Called 'write_tag' in invalid tag");
        cardStateChanged(RFIDCardReaderOperator::CS_NoCard);
    } else if(isClassicCard(_op->card()) || (_op->card() == MF_PLUS_S_SL3) || (_op->card() == MF_PLUS_X_SL3)) {
        mfc_write();
    } else if(isUltralightCard(_op->card())) {
        ul_write();
    } else {
        statusBar()->showMessage(tr("This tag is not writable"), 5000);
    }
}

void RFIDKitWindow::on_emCloneButton_clicked()
{
    if(!_op->isValid())
    {
        return;
    }

    /* By the time we are ready to clone, the card will be long gone! */
    QByteArray current_uid = _op->uid();

    if(!current_uid.size())
    {
        return;
    }

    EMCloneDialog dlg(this);
    dlg.readSettings();
    qWarning()<<"Cloning "<<current_uid;
    dlg.setUID(current_uid);
    if(dlg.exec() == QDialog::Accepted)
    {
        dlg.writeSettings();
        _op->writeUID(current_uid,
                      QByteArray::fromHex(dlg.currentPassword().toLatin1()),
                      QByteArray::fromHex(dlg.newPassword().toLatin1()),
                      dlg.coding(), dlg.speed());
    }
}

void RFIDKitWindow::mfcKeyTypeSwitched(int key)
{
    Q_UNUSED(key)
    if(_op->type() == RFIDCardReaderInterface::RFIDReader_Hardware)
    {
        MifareClassicKeyType kt = (MifareClassicKeyType)key;
        bool ok;
        if(set.value(SETTINGS_KEYS, false).toBool() && (ui->sectorList->currentRow() >= 0))
        {
            if((_op->card() == MF_PLUS_S_SL3)||(_op->card() == MF_PLUS_X_SL3))
            {
                MifarePlusKey key = key_db->plus(_op->uid(), RFIDCardReaderInterface::plusKeyBNr(ui->sectorList->currentRow(), kt), &ok);
                if(ok)
                {
                    ui->mfcKeyEdit->setText(key.toString());
                }
            }
            else
            {
                MifareClassicKey key = key_db->classic(_op->uid(), ui->sectorList->currentRow(), kt, &ok);
                if(ok)
                {
                    ui->mfcKeyEdit->setText(key.toString());
                }
            }
        }
    }
}

//void RFIDKitWindow::on_mfcSectorCombo_currentIndexChanged(int i)
void RFIDKitWindow::on_sectorList_currentRowChanged(int i)
{
#if 0
    if(_op->uid().isEmpty())
    {
        qWarning()<<"mfc sector change request came too late - drop";
        return;
    }
#endif

    auto cs = _op->cardState();
    if((cs != RFIDCardReaderOperator::CS_Idle) && (cs != RFIDCardReaderOperator::CS_ReadFailed) && (cs != RFIDCardReaderOperator::CS_WriteFailed)) {
        qWarning()<<"Sector change request out of order - not idle: " << _op->cardState();
        ui->sectorList->blockSignals(true);
        ui->sectorList->setCurrentRow(-1);
        ui->sectorList->blockSignals(false);
        return;
    }

    if(i < 0) {
        qWarning("Sector - no selection");
        return;
    }

    if(isClassicCard(_op->card()))
    {
        ui->modUIDTool->setVisible((i==0) && (_op->uid().size() == 4));
        mfc_changeSector_classic(i);
    }
    else if((_op->card() == MF_PLUS_S_SL3)||(_op->card() == MF_PLUS_X_SL3))
    {
        mfc_changeSector_plus(i);
    }
}

void RFIDKitWindow::classic_block_written(int number, bool written)
{
    MifareClassicSectorInterface* iclassic = nullptr;

    if(!mfc_sector)
    {
        //should not happen
        return;
    }

    switch(mfc_sector->type())
    {
        case MifareSectorInterface::ST_MiClassic:
        {
            iclassic = dynamic_cast<MifareClassicSectorInterface*>(mfc_sector);
            break;
        }
        case MifareSectorInterface::ST_MiClassicJumbo:
        {
            iclassic = dynamic_cast<MifareClassicSectorInterface*>(mfc_sector);
            break;
        }
        default:return;
    }

    number -= mfc_sector->startAddress();
    if(written && (number==(mfc_sector->blockCount()-1)))
    {
        /*Current or Saved does not matter here - the block is 'written' at this point*/
        mifare_classic_mod_trailer = true;
        if(_op->type() == RFIDCardReaderInterface::RFIDReader_Hardware)
        {
            if(iclassic->canWriteKey(MifareClassicKeyA))
            {
                qWarning()<<"Written block "<<number<<" - can write A key - save "<<iclassic->key(MifareClassicKeyA, BD_Current);
                key_db->save(_op->uid(),
                             MifareClassicFunctions::sectorNumber(mfc_sector->startAddress()),
                             MifareClassicKeyA,iclassic->key(MifareClassicKeyA, BD_Current));
            }
            if(iclassic->canWriteKey(MifareClassicKeyB))
            {
                key_db->save(_op->uid(),
                             MifareClassicFunctions::sectorNumber(mfc_sector->startAddress()),
                             MifareClassicKeyB,iclassic->key(MifareClassicKeyB, BD_Current));
            }
        }
    }
}

/*number == startAddress() + blockCount()*/
void RFIDKitWindow::classic_write_finished(bool success)
{
    if(!success)
    {
        statusBar()->showMessage(tr("Sector could not be written"));
    }
    else
    {
        if(!mifare_classic_skippedBlocks.isEmpty())
        {
            mifare_classic_skippedBlocks.chop(1);
            QMessageBox::warning(this, tr("Write Sector"),
                                 tr("Blocks %1 could not be written due to access permissions.%2")
                                 .arg(mifare_classic_skippedBlocks).arg(mifare_classic_mod_trailer?
                                                                            tr("\nAccess bits have been modified. Please try again."):
                                                                            QString()));
        }
        statusBar()->showMessage(tr("Sector %1 written").arg(ui->sectorList->currentRow()));
    }
}

void RFIDKitWindow::ultralight_write_finished(bool success)
{
    Q_UNUSED(success);
    statusBar()->showMessage(tr("Tag data written"));
}

void RFIDKitWindow::reader_misc(int code, const QVariant& data)
{
    switch(code) {
        case RFIDCardReaderInterface::MC_DFUResult: {
            switch(data.toInt()) {
                case RFIDCardHardwareReader::DFailure: {
                    QAutoCloseMessageBox::warning(10, this, tr("Firmware Update"), tr("Cannot put the device into the firmware upgrade mode"));
                    break;
                }
                case RFIDCardHardwareReader::DBuiltin: {
                    statusBar()->showMessage(
                                tr("The device is switching to firmware upgrade mode"),
                                5000);
                    break;
                }
                case RFIDCardHardwareReader::DExternal: {
                    QAutoCloseMessageBox::information(10, this, tr("Firmware Update"),
                                                      tr("This device has a bootloader with mass-storage support. "
                                                         "Please locate the \".. DFU\" flash drive, copy the new firmware to it, "
                                                         "then wait for it to reboot or safely eject the drive after the copying is complete"));
                    break;
                }
                default: break;
            }
        }
        default: break;
    }
}

void RFIDKitWindow::mfc_write()
{
    if(!mfc_sector)
    {
        return;
    }

    MifareClassicKeyType key_type = (MifareClassicKeyType)ui->mfcKeyCombo->currentIndex();
    qDebug()<<"Authenticating with key "<<ui->mfcKeyEdit->text();
    bool is_plus = ((_op->card()==MF_PLUS_S_SL3) || (_op->card()==MF_PLUS_X_SL3));
    if(is_plus)
    {
        MifarePlusKey key(ui->mfcKeyEdit->text());
        _op->setPlusKey(key_type, key);
        /*Reauth after +i checks*/
        _op->plusAuth(mfc_sector->startAddress());
    }
    else
    {
        MifareClassicKey key(ui->mfcKeyEdit->text());
        _op->setKey(key_type, key);
    }

    MifareClassicSectorInterface* iclassic = nullptr;
    switch(mfc_sector->type())
    {
        case MifareSectorInterface::ST_MiClassic:
        {
            iclassic = dynamic_cast<MifareClassicSectorInterface*>(mfc_sector);
            break;
        }
        case MifareSectorInterface::ST_MiClassicJumbo:
        {
            iclassic = dynamic_cast<MifareClassicSectorInterface*>(mfc_sector);
            break;
        }
        default:return;
    }

    //update authentication type - may have changed
    //MifareClassicKeyType orig_type = iclassic->keyType();
    iclassic->setKeyType(key_type);

    mifare_classic_skippedBlocks.clear();
    mifare_classic_mod_trailer = false;

    for(int i=0;i<(mfc_sector->blockCount()-1); ++i)
    {
        if(mfc_sector->modified(i))
        {
            if(!iclassic->canWriteBlock(i))
            {
                mifare_classic_skippedBlocks += QString::number(i)+",";
            }
            else
            {
                qDebug()<<"Block "<<i<<" is modified -> writing";
                RFIDCardReaderOperator::OpCode uchoice = warnDeadLock(i);
                if(uchoice == RFIDCardReaderOperator::OP_Cancel) {
                    continue;
                } else if(uchoice==RFIDCardReaderOperator::OP_Abort) {
                    return;
                }

                qDebug()<<"Writing block "<<i<<" from sector "
                       <<MifareClassicFunctions::sectorNumber(mfc_sector->startAddress())<<" - block "<<mfc_sector->startAddress();

                _op->write(mfc_sector->startAddress() + i, QByteArray(reinterpret_cast<const char*>(mfc_sector->raw(i)), 16));
            }
        }
    }

    if(mfc_sector->modified(mfc_sector->blockCount()-1))
    {
        if(!iclassic->canWriteTrailerBits())
        {
            mifare_classic_skippedBlocks += QString::number(mfc_sector->blockCount()-1)+",";
        }
        else
        {
            qDebug()<<"Trailer block is modified -> writing";
            RFIDCardReaderOperator::OpCode uchoice = warnDeadLock(mfc_sector->blockCount()-1);
            if(uchoice==RFIDCardReaderOperator::OP_Continue)
            {
                qDebug()<<"Writing trailer block from sector "
                       <<MifareClassicFunctions::sectorNumber(mfc_sector->startAddress())<<" - block "<<mfc_sector->startAddress();

                _op->write(mfc_sector->startAddress()+mfc_sector->blockCount()-1, QByteArray(reinterpret_cast<const char*>(mfc_sector->raw(mfc_sector->blockCount()-1)), 16));
            }
        }
    }

    /*finalize*/
    _op->write(-1, QByteArray());
}

RFIDCardReaderOperator::OpCode RFIDKitWindow::warnDeadLock(int block)
{
    if(!mfc_sector)
        return RFIDCardReaderOperator::OP_Abort;

    if(mfc_sector->isDeadLocked(block, BD_Current))
    {
        return warnDeadLockBlocks(QVector<int>()<<block);
    }
    return RFIDCardReaderOperator::OP_Continue;
}

RFIDCardReaderOperator::OpCode RFIDKitWindow::warnDeadLockBlocks(const QVector<int>& blocks)
{
    if(set.value(SETTINGS_WARNLOCK,true).toBool() || blocks.isEmpty())
    {
        QString blk_str;
        foreach(int blk, blocks)
        {
            blk_str.append(QString("%1, ").arg(blk));
        }
        blk_str.chop(2);
        QMessageBox qMsg(this);
        qMsg.setWindowTitle(tr("Dead Lock"));
        qMsg.setText(tr("ATTENTION! You are going to lock block(s) <b>%1</b> forever! Proceed?").arg(blk_str));
        qMsg.setInformativeText(tr("You will not be able to alter the contents of these blocks once written."));
        qMsg.setStandardButtons(QMessageBox::Abort|QMessageBox::Yes|QMessageBox::No);
        qMsg.setDefaultButton(QMessageBox::No);
        qMsg.setCheckBox(new QCheckBox(tr("Do not show this warning again (assume \"Yes\")")));
        int ret = qMsg.exec();
        if(qMsg.checkBox()->isChecked())
            set.setValue(SETTINGS_WARNLOCK,false);

        switch(ret)
        {
            case QMessageBox::Yes:return RFIDCardReaderOperator::OP_Continue;
            case QMessageBox::No: return RFIDCardReaderOperator::OP_Cancel;
            default: return RFIDCardReaderOperator::OP_Abort;
        }
    }
    return RFIDCardReaderOperator::OP_Continue;
}

void RFIDKitWindow::on_mfcValueButton_clicked()
{
    MifareClassicSectorInterface* iclassic = dynamic_cast<MifareClassicSectorInterface*>(mfc_sector);
    if(!iclassic)
        return;

    MifareClassicValueEditor ed(this);
    ed.setBlockAddress(mfc_sector->startAddress());
    if(ed.exec()==QDialog::Accepted)
    {
        /* sector might have disappeared or changed */
        iclassic = dynamic_cast<MifareClassicSectorInterface*>(mfc_sector);
        if(!iclassic)
            return;

        iclassic->setValueBlock(ed.block(), ed.value(), ed.address());
        mfc_model->touch(ed.block());
    }
}

void RFIDKitWindow::on_sectorTable_doubleClicked(const QModelIndex &index)
{
    if(!mfc_sector || !index.isValid())
    {
        //should never happen
        return;
    }

    int stype = mfc_sector->type();
    switch(stype)
    {
        case MifareClassicSector::ST_MiClassic:
        case MifareClassicSector::ST_MiClassicJumbo:
        {
            MifareClassicSectorInterface *iclassic = dynamic_cast<MifareClassicSectorInterface*>(mfc_sector);
            if(index.column()==0)
            {
                //block/trailer bits edit requested

                /*we are actually editing the last block (with access bits), so check it!*/
                if(mfc_sector->isEditable(mfc_sector->blockCount()-1 /*index.row()*/, 1, BD_Saved))
                {
                    MifareClassicTrailerEditor ed(this);
                    if(index.row()==(mfc_sector->blockCount()-1))
                    {
                        qDebug()<<"Requested mifare classic trailer edit: current access "
                               <<iclassic->trailerAccessBits(BD_Current)<<": "<<iclassic->trailerAccessPermissions(BD_Current);
                        ed.setUIMode(MifareClassicTrailerEditor::TrailerEditor);
                        ed.setMode(iclassic->trailerAccessBits(BD_Current),iclassic->canWriteTrailerBits());
                        ed.setKey(MifareClassicKeyA,iclassic->key(MifareClassicKeyA, BD_Current));
                        ed.setKeyAvailable(MifareClassicKeyA,iclassic->canWriteKey(MifareClassicKeyA));
                        ed.setKey(MifareClassicKeyB,iclassic->key(MifareClassicKeyB, BD_Current));
                        ed.setKeyAvailable(MifareClassicKeyB,iclassic->canWriteKey(MifareClassicKeyB));
                    }
                    else
                    {
                        ed.setUIMode(MifareClassicTrailerEditor::BlockEditor);
                        ed.setMode(iclassic->blockAccessBits(index.row(), BD_Current),iclassic->canWriteTrailerBits());
                    }

                    if(ed.exec()==QDialog::Accepted)
                    {
                        /*card could have been removed*/
                        iclassic = dynamic_cast<MifareClassicSectorInterface*>(mfc_sector);
                        if(!iclassic)
                        {
                            break;
                        }

                        bool touched = false;
                        if(ed.uiMode() == MifareClassicTrailerEditor::BlockEditor)
                        {
                            if(ed.getMode() != iclassic->blockAccessBits(index.row(), BD_Current))
                            {
                                iclassic->setBlockAccess(index.row(),ed.getMode());
                                touched = true;
                            }
                        }
                        else
                        {
                            if(ed.getMode() != iclassic->trailerAccessBits(BD_Current))
                            {
                                iclassic->setTrailerAccess(ed.getMode());
                                touched = true;
                            }
                            if(ed.keyModified(MifareClassicKeyA))
                            {
                                iclassic->writeKey(MifareClassicKeyA,ed.getKey(MifareClassicKeyA));
                                touched = true;
                            }
                            if(ed.keyModified(MifareClassicKeyB))
                            {
                                iclassic->writeKey(MifareClassicKeyB,ed.getKey(MifareClassicKeyB));
                                touched = true;
                            }
                        }

                        if(touched)
                        {
                            mfc_model->touch(index.row());
                        }
                    }
                }
                else
                {
                    QAutoCloseMessageBox::warning(10, this, tr("Edit Block Access"), tr("Block %1 is not editable: %2")
                                         .arg(index.row())
                                         .arg(mfc_sector->editHint(index.row(),index.column()-1)));
                }
            }
            break;
        }
        case MifareClassicSector::ST_MiUlEEPROM:
        case MifareClassicSector::ST_MiULExtEEPROM:
        case MifareClassicSector::ST_MiNTAGEEPROM:
        {
            switch(index.row())
            {
                case 2:
                {
                    if((stype != MifareClassicSector::ST_MiUlEEPROM) && !mfc_sector->isEditable(2,2, BD_Saved))
                    {
                        QAutoCloseMessageBox::warning(10, this, tr("Edit Lock Bits"),
                                             tr("Lock bits could not be edited (not authorized?)"));
                        return;
                    }

                    //edit lock bytes;
                    LockBitsDialog dlg(true, this);
                    dlg.editor.setBits16(dynamic_cast<MifareUltralightAbstractEEPROMInterface*>(mfc_sector)->lockBits(BD_Saved));
                    dlg.editor.setLabels(MifareUltralightAbstractEEPROMInterface::staticLockBits_0_15_Labels);
                    if(dlg.exec()==QDialog::Accepted)
                    {
                        /*card could have been removed or changed*/
                        if(!dynamic_cast<MifareUltralightAbstractEEPROMInterface*>(mfc_sector))
                        {
                            return;
                        }

                        dynamic_cast<MifareUltralightAbstractEEPROMInterface*>(mfc_sector)->setLockBits(dlg.editor.bits16());
                        mfc_model->touch(2);
                    }
                    return;
                }
                case 3:
                {
                    if((stype != MifareClassicSector::ST_MiUlEEPROM) && !mfc_sector->isEditable(3,0, BD_Saved))
                    {
                        QAutoCloseMessageBox::warning(10, this, tr("Edit OTP Area"),
                                             tr("OTP / Capability Container bits could not be edited (not authorized?)"));
                        return;
                    }

                    //edit OTP (CC for NTAG - edit as OTP, for now)
                    LockBitsDialog dlg(true, this);
                    qWarning()<<"Current sector "<<mfc_sector<<" - as EEPROMInterface "
                             <<dynamic_cast<MifareUltralightAbstractEEPROMInterface*>(mfc_sector)
                            <<", as EV1 Interface: "<<dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector)
                           <<", as EV1_80:"<<dynamic_cast<MifareUltralightEV1_80_EEPROM*>(mfc_sector);
                    //qWarning()<<"Card "<<current_reader->getUID()<<" OTP: "<<dynamic_cast<MifareUltralightAbstractEEPROMInterface*>(mfc_sector)->otp(BD_Saved);
                    dlg.editor.setBits32(dynamic_cast<MifareUltralightAbstractEEPROMInterface*>(mfc_sector)->otp(BD_Saved));
                    dlg.editor.setLabels();
                    if(dlg.exec()==QDialog::Accepted)
                    {
                        /*card could have been removed or changed*/
                        if(!dynamic_cast<MifareUltralightAbstractEEPROMInterface*>(mfc_sector))
                        {
                            return;
                        }

                        dynamic_cast<MifareUltralightAbstractEEPROMInterface*>(mfc_sector)->setOTP(dlg.editor.bits32());
                        mfc_model->touch(3);
                    }
                    return;
                }
                default:break;
            }

            if(stype == MifareClassicSector::ST_MiUlEEPROM)
                break;

            //EV1 & NTAG
            if(mfc_sector->customEdit(index.row()) && mfc_sector->isEditable(index.row(), 0, BD_Saved))
            {
                int total_blocks = mfc_sector->blockCount();
                MifareUltralightAbstractEEPROM_EV1Interface* iface = dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector);
                if(iface && (index.row() >= (total_blocks-2)))
                {
                    //password & pack
                    MifareUltralightPasswordEditor pwded(this);
                    pwded.setup(iface->password(BD_Current), iface->pack(BD_Current));
                    if(pwded.exec()==QDialog::Accepted)
                    {
                        /*card could have been removed or changed*/
                        iface = dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector);
                        if(!iface)
                        {
                            break;
                        }

                        pwded.apply(mfc_sector);
                        mfc_model->touch(total_blocks-2);
                        mfc_model->touch(total_blocks-1);
                    }
                }
                else if((index.row()>=(total_blocks-4))&&(index.row()<=(total_blocks-3)))
                {
                    //cfg0 & cfg1
                    if(stype==MifareClassicSector::ST_MiULExtEEPROM)
                    {
                        MifareUltralightEV1ConfigEditor ed(this);
                        ed.setup(mfc_sector->raw(total_blocks-4), mfc_sector->raw(total_blocks-3));
                        if(ed.exec() == QDialog::Accepted)
                        {
                            /*card could have been removed*/
                            iface = dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector);
                            if(!iface)
                            {
                             break;
                            }

                            ed.apply(mfc_sector, total_blocks-4, total_blocks-3);
                            mfc_model->touch(total_blocks-4);
                            mfc_model->touch(total_blocks-3);
                        }
                    }
                    else
                    {
                        MifareNTAGConfigEditor ed(this);
                        ed.setup(mfc_sector->raw(total_blocks-4), mfc_sector->raw(total_blocks-3));
                        if(ed.exec() == QDialog::Accepted)
                        {
                            /*card could have been removed*/
                            iface = dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector);
                            if(!iface)
                            {
                             break;
                            }

                            ed.apply(mfc_sector, total_blocks-4, total_blocks-3);
                            mfc_model->touch(total_blocks-4);
                            mfc_model->touch(total_blocks-3);
                        }
                    }
                }
                else if((total_blocks > 20) && (index.row() == (total_blocks-5)))
                {
                    /*
                     * dynamic lock bytes for NTAG, lock bytes for EV1 164
                     * (what does 'dynamic' mean? they ARE one-time-programmable)
                     */
                    LockBitsDialog dlg(false, this);
                    dlg.editor.setBits32(dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector)->auxLockBits(BD_Saved),
                                         dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector)->auxLockRFU());
                    if(dynamic_cast<MifareUltralightEV1_164_EEPROM*>(mfc_sector))
                    {
                        dlg.editor.setLabels(MifareUltralightEV1_164_EEPROM::auxLockBits_16_35_Labels);
                    }
                    else if(dynamic_cast<MifareNTAG213*>(mfc_sector))
                    {
                        dlg.editor.setLabels(MifareNTAG213::dynamicLockBits_Labels);
                    }
                    else if(dynamic_cast<MifareNTAG215*>(mfc_sector))
                    {
                        dlg.editor.setLabels(MifareNTAG215::dynamicLockBits_Labels);
                    }
                    else if(dynamic_cast<MifareNTAG216*>(mfc_sector))
                    {
                        dlg.editor.setLabels(MifareNTAG216::dynamicLockBits_Labels);
                    }
                    else
                    {
                        qWarning()<<"Cast of "<<mfc_sector<<" to 80/164/N213/5/6 failed - this is a BUG!";
                        dlg.editor.setLabels();
                    }
                    if(dlg.exec()==QDialog::Accepted)
                    {
                        /*card could have been removed*/
                        if(!dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector))
                        {
                            break;
                        }

                        dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector)->setAuxLockBits(dlg.editor.bits32());
                        mfc_model->touch(index.row());
                    }
                }
            }
            break;
        }
        default:break;
    }
}

void RFIDKitWindow::on_ulevPwdEdit_clicked()
{
    quint32 pwd = ui->ulevPwdEdit->text().toUInt(nullptr,16);
    quint16 pack = 0x0000;

    if(mfc_sector && dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector))
    {
        pack = dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector)->pack(BD_Saved);
    }

    MifareUltralightPasswordEditor pwded(this);
    pwded.setup(pwd, pack);
    if(pwded.exec()==QDialog::Accepted)
    {
        ui->ulevPwdEdit->setText(QString("%1").arg(pwded.password(),8,16,QLatin1Char('0')).toUpper());
    }
}

void RFIDKitWindow::ul_read()
{
    if(mfc_sector && mfc_sector->modified())
    {
        if(QMessageBox::question(this, tr("Save Changes"),
                                 tr("Tag contents have been modified.\n"
                                    "Write changes to the tag?"),
                                 QMessageBox::Yes|QMessageBox::No,
                                 QMessageBox::No) == QMessageBox::Yes)
        {
            on_actionWrite_Tag_triggered();
            return;
        }
    }

    /*The corresponding key is already set*/
    clearModel();
    switch(_op->card())
    {
        case MF_ULTRALIGHT:
            mfc_sector = new MifareUltralightEEPROM();
            break;
        case MF_ULTRALIGHT_NANO:
            mfc_sector = new MifareUltralightNanoEEPROM();
            break;
        default:
        case MF_ULTRALIGHT_C:
            return;
        case MF_ULTRALIGHT_EV1_80:
            mfc_sector = new MifareUltralightEV1_80_EEPROM();
            break;
        case MF_ULTRALIGHT_EV1_164:
            mfc_sector = new MifareUltralightEV1_164_EEPROM();
            break;
        case MF_NTAG213:
            mfc_sector = new MifareNTAG213();
            break;
        case MF_NTAG215:
            mfc_sector = new MifareNTAG215();
            break;
        case MF_NTAG216:
            mfc_sector = new MifareNTAG216();
            break;
    }

    ultralight_password_rejected = false;
    progress_display->setText(tr("Reading Tag"));
    progress_display->start(ui->sectorTable);

    if(isUltralightEV1Card(_op->card()))
    {
        qWarning()<<"Try UL authentication & get PACK";
        _op->setPassword(ui->ulevPwdEdit->text().toUInt(nullptr, 16));
        _op->readPack();
    }

    qDebug()<<"Reading "<<mfc_sector->blockCount()<<" ultralight blocks";
    _op->read(0, mfc_sector->blockCount());
}

void RFIDKitWindow::ul_write()
{
    if(!mfc_sector) {
        return;
    }

    /*There is no use checking the modified blocks, instead, must check the lock bits*/
    QVector<int> new_locked = dynamic_cast<MifareUltralightAbstractEEPROMInterface*>(mfc_sector)->newLockedBlocks();
    if(!new_locked.isEmpty())
    {
        switch(warnDeadLockBlocks(new_locked))
        {
            case RFIDCardReaderOperator::OP_Abort:
            case RFIDCardReaderOperator::OP_Cancel:
            {
                return;
            }
            case RFIDCardReaderOperator::OP_Continue:
            {
                break;
            }
        }
    }

    /*write the static lock bits at the end!*/
    bool writing = false;
    for(int i=3;i<mfc_sector->blockCount(); ++i)
    {
        if(mfc_sector->modified(i))
        {
            qDebug()<<"Mifare UL block "<<i<<" modified - writing EEPROM "<<(mfc_sector->startAddress()+i);

            if(!writing && ((mfc_sector->type()==MifareSectorInterface::ST_MiNTAGEEPROM)||
                            (mfc_sector->type()==MifareSectorInterface::ST_MiULExtEEPROM)))
            {
                _op->readPack();
            }
            writing = true;
            _op->write(i, QByteArray(reinterpret_cast<const char*>(mfc_sector->raw(i)), 4));

            if((i == (mfc_sector->blockCount()-2)) &&
               ((mfc_sector->type()==MifareSectorInterface::ST_MiNTAGEEPROM)||
                (mfc_sector->type()==MifareSectorInterface::ST_MiULExtEEPROM)))
            {
                //password changed - apply the new one immediately
                _op->setPassword(dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector)->password(BD_Current));
                _op->readPack();
            }
        }
    }

    //static lock bits
    if(mfc_sector->modified(2))
    {
        qDebug()<<"Mifare UL lock bits changed!";
        if(!writing && ((mfc_sector->type()==MifareSectorInterface::ST_MiNTAGEEPROM)||
                        (mfc_sector->type()==MifareSectorInterface::ST_MiULExtEEPROM)))
        {
            _op->readPack();
        }
        writing = true;
        _op->write(2, QByteArray(reinterpret_cast<const char*>(mfc_sector->raw(2)), 4));
    }

    /*finalize*/
    _op->write(-1, QByteArray());
}

void RFIDKitWindow::card_uidChanged()
{
    /*NOP - old card will be lost, new found*/
    if(_op->type() == RFIDCardReaderInterface::RFIDReader_Simulator)
    {
        on_actionEmulator_Open_triggered();
    }
}

void RFIDKitWindow::card_packReceived(bool success, quint16 pack)
{
    if(!success)
    {
        if(_op->cardState() == RFIDCardReaderOperator::CS_ReadFailed)
        {
            ultralight_password_rejected = true;
            if(QMessageBox::question(this, tr("Wrong Password"),
                                     tr("Password based authentication procedure failed. The application can try to read the rest of data, however the result may be incorrect. Proceed?"),
                                     QMessageBox::Yes|QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            {
                // may have gone stale!
                if(_op->cardState() == RFIDCardReaderOperator::CS_ReadFailed) {
                    clearModel();

                    progress_display->setText(tr("Incorrect password"));
                    progress_display->start(ui->sectorTable);
                } else {
                    return;
                }
            }
            else if(mfc_sector)
            {
                /*mfc_sector could have disappeared by now*/
                _op->read(0, mfc_sector->blockCount());
            }
            return;
        }
        else /* if(card_state == CS_Writing)*/
        {
            QMessageBox::warning(this,tr("Ultralight Authentication"),
                                 tr("The password block has been changed, but authentication procedure with the new password failed. The tag may be in an undefined state."));
        }
    }
    else if(isUltralightEV1Card(_op->card()))
    {
        ultralight_password_rejected = false;
        MifareUltralightEV1Security cred;
        cred.password = ui->ulevPwdEdit->text().toUInt(nullptr, 16);
        cred.pack = pack;
        qWarning()<<"Received UL credentials: "<<cred;
        if(_op->type() == RFIDCardReaderInterface::RFIDReader_Hardware)
        {
            key_db->save(_op->uid(), cred);
        }

        if(dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector))
        {
            dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector)->storeCredentials(cred.password, pack);
        }
        else
        {
            qWarning()<<"Cannot convert "<<mfc_sector<<" sector to EV1Interface! - its a BUG!";
        }
    }
    //continue reading the card
}

void RFIDKitWindow::card_blockRead(int number, QByteArray data, int result)
{
    if(!mfc_sector)
    {
        qWarning()<<"BUG: block read, but no sector -skipping";
        return;
    }

    int blockNumber = number - mfc_sector->startAddress();
    qDebug()<<"Read block "<<blockNumber<<" (block "<<number<<" in card), result "<<result;

    if((blockNumber < 0) || (blockNumber >= mfc_sector->blockCount()))
    {
        qWarning()<<"Ignoring a stale or out-of-range block reply for address "<<number;
        return;
    }
    if((result == RFIDCardReaderInterface::RW_OK) && (data.size() != mfc_sector->blockSize()))
    {
        qWarning()<<"Ignoring block "<<number<<" with invalid size "<<data.size()
                  <<" (expected "<<mfc_sector->blockSize()<<")";
        statusBar()->showMessage(tr("Block %1 returned invalid data").arg(blockNumber), 2000);
        return;
    }

    switch(result)
    {
        case RFIDCardReaderInterface::RW_OK: {
            if((mfc_sector->type() == MifareSectorInterface::ST_MiClassic) ||
               (mfc_sector->type() == MifareSectorInterface::ST_MiClassicJumbo)) {
                ui->tableGroup->setTitle(tr("Data") % QLatin1String(" (") %
                                         tr("sector %1, %2 blocks")
                                         .arg(MifareClassicFunctions::sectorNumber(mfc_sector->startAddress()))
                                         .arg(blockNumber + 1) %
                                         QLatin1String(")"));
            } else {
                ui->tableGroup->setTitle(tr("Data") % QLatin1String(" (") %
                                         tr("%1 blocks").arg(blockNumber + 1) %
                                         QLatin1String(")"));
            }

            break;
        }
        case RFIDCardReaderInterface::RW_Denied:
        case RFIDCardReaderInterface::RW_Fatal:
        {
            statusBar()->showMessage(tr("Block %1 read failed - access denied").arg(blockNumber),2000);
            switch(mfc_sector->type())
            {
                case MifareSectorInterface::ST_MiClassic:
                case MifareSectorInterface::ST_MiClassicJumbo:
                {
                    if(ui->sectorList->currentRow() >= 0) {
                        key_db->failed(MifareClassicKeyID(_op->uid(), ui->sectorList->currentRow()));
                    }
                    break;
                }
                case MifareSectorInterface::ST_MiULExtEEPROM:
                case MifareSectorInterface::ST_MiNTAGEEPROM:
                {
                    qWarning()<<"UL EV / NTAG block read failed - maybe password protected?";
                    //Read error is reported as timeout -> no way to tell Fatal from Denied
                    if(!blockNumber /*|| (result == RFIDCardReaderInterface::RW_Fatal)*/) {
                        break;
                    }

                    MifareUltralightAbstractEEPROM_EV1Interface* iface =
                            dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector);
                    if(!iface)
                    {
                        qWarning()<<"Cannot process protected block: incompatible sector model";
                        return;
                    }
                    iface->setLastValidBlock(blockNumber-1);
                    //from ReadOK
                    progress_display->stop();
                    card_read_done();
                    //make idle
                    _op->rectifyCard();
                    return;
                }
                default:
                    break;
            }

            const QString failureReason =
                    ultralight_password_rejected && isUltralightEV1Card(_op->card())
                    ? tr("Incorrect password")
                    : ((result == RFIDCardReaderInterface::RW_Denied)
                       ? tr("Access denied") : tr("Read error"));

            clearModel();
            setTagToolsEnabled(true);
            progress_display->setText(failureReason);
            progress_display->start(ui->sectorTable);
            return;
        }
        case RFIDCardReaderInterface::RW_Retry:
        {
            /*restart the read process*/
            on_actionRead_Tag_triggered();
            return;
        }
        default:
        {
            statusBar()->showMessage(tr("Block %1 read failed").arg(blockNumber),2000);
            qWarning()<<"Unhandled block-read result "<<result<<" for block "<<number;
            return;
        }
    }

    mfc_sector->write(blockNumber, reinterpret_cast<const quint8*>(data.constData()));
    mfc_sector->written(blockNumber);
}

void RFIDKitWindow::warnAccessBits()
{
    QMessageBox::warning(this,tr("Invalid Data"),
                         tr("Access Bits could not be read due to access restrictions.\n"
                            "Key A is invalid and some/all blocks may also be invalid.\n"
                            "Be careful when overwriting the data.\n"
                            "Write operation will most probably fail."));
}

void RFIDKitWindow::warnKey(const QString& key, const QString& bogusBlocks)
{
    QString body;
    if(!key.isEmpty())
    {
        body = tr("Key") + " " + key;
    }
    if(!bogusBlocks.isEmpty())
    {
        if(!body.isEmpty())
        {
            body.append(tr(" and blocks "));
        }
        else
        {
            body.append(tr("Blocks") + " ");
        }
        body.append(bogusBlocks);
    }
    QMessageBox::warning(this,tr("Invalid Data"),
                         tr("%1 could not be read due to access restrictions.\n"
                            "The displayed data could be partially invalid!\n"
                            "Be careful when overwriting the data").arg(body));
}

void RFIDKitWindow::mfc_changeSector_classic(int sector)
{
    if(mfc_sector && mfc_sector->modified())
    {
        if(QMessageBox::question(this,tr("Save Changes"),tr("Sector %1 has been modified.\n"
                                                            "Write changes to the tag?")
                                 .arg(MifareClassicFunctions::sectorNumber(mfc_sector->startAddress())),
                                 QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)
        {
            ui->sectorList->blockSignals(true);
            ui->sectorList->setCurrentRow(MifareClassicFunctions::sectorNumber(mfc_sector->startAddress()));
            ui->sectorList->blockSignals(false);
            on_actionWrite_Tag_triggered();
            return;
        }

        /*mfc_sector could have disappeared by now*/
        if(!mfc_sector) return;
    }

    if(_op->type() == RFIDCardReaderInterface::RFIDReader_Hardware)
    {
        bool ok;
        MifareClassicKey key = key_db->classic(_op->uid(), sector, MifareClassicKeyA, &ok);
        if(!ok)
        {
            key = key_db->classic(_op->uid(), sector, MifareClassicKeyB, &ok);
            if(ok)
            {
                qDebug()<<"Found key B for "<<_op->uidString();
                ui->mfcKeyCombo->setCurrentIndex(MifareClassicKeyB);
                ui->mfcKeyEdit->setText(key.toString());
            }
            else
            {
                qDebug()<<"Neither key A nor key B found for "<<_op->uidString();
                ui->mfcKeyCombo->setCurrentIndex(MifareClassicKeyA);
                ui->mfcKeyEdit->setText("FFFFFFFFFFFF");
            }
        }
        else
        {
            qDebug()<<"Found key A for "<<_op->uidString();
            ui->mfcKeyCombo->setCurrentIndex(MifareClassicKeyA);
            ui->mfcKeyEdit->setText(key.toString());
        }
    }

    if(_op->isValid())
    {
        on_actionRead_Tag_triggered();
    }
}

void RFIDKitWindow::mfc_changeSector_plus(int sector)
{
    if(mfc_sector && mfc_sector->modified())
    {
        if(QMessageBox::question(this, tr("Save Changes"), tr("Sector %1 has been modified.\n"
                                                              "Write changes to the tag?")
                                 .arg(MifareClassicFunctions::sectorNumber(mfc_sector->startAddress())),
                                 QMessageBox::Yes|QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
        {
            ui->sectorList->blockSignals(true);
            ui->sectorList->setCurrentRow(MifareClassicFunctions::sectorNumber(mfc_sector->startAddress()));
            ui->sectorList->blockSignals(false);
            on_actionWrite_Tag_triggered();
            return;
        }

        /*mfc_sector could have disappeared by now*/
        if(!mfc_sector) return;
    }

    if(_op->type() == RFIDCardReaderInterface::RFIDReader_Hardware)
    {
        bool ok;
        MifarePlusKey key = key_db->plus(_op->uid(), RFIDCardReaderInterface::plusKeyBNr(sector, MifareClassicKeyA), &ok);
        if(!ok)
        {
            key = key_db->plus(_op->uid(), RFIDCardReaderInterface::plusKeyBNr(sector, MifareClassicKeyB), &ok);
            if(ok)
            {
                qDebug()<<"Found key B for "<<_op->uidString();
                ui->mfcKeyCombo->setCurrentIndex(MifareClassicKeyB);
                ui->mfcKeyEdit->setText(key.toString());
            }
            else
            {
                qDebug()<<"Neither key A nor key B found for "<<_op->uidString();
                ui->mfcKeyCombo->setCurrentIndex(MifareClassicKeyA);
                ui->mfcKeyEdit->setText("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
            }
        }
        else
        {
            qDebug()<<"Found key A for "<<_op->uidString();
            ui->mfcKeyCombo->setCurrentIndex(MifareClassicKeyA);
            ui->mfcKeyEdit->setText(key.toString());
        }
    }

    if(_op->isValid())
    {
        on_actionRead_Tag_triggered();
    }
}

void RFIDKitWindow::mfc_classic_read()
{
    int sector = ui->sectorList->currentRow();
    if(sector < 0) {
        qWarning("Not reading classic tag - no sector");
        return;
    }

    MifareClassicKeyType key_type = (MifareClassicKeyType)ui->mfcKeyCombo->currentIndex();
    qDebug()<<"Authenticating with key "<<ui->mfcKeyEdit->text();
    MifareClassicKey key(ui->mfcKeyEdit->text());

    clearModel();
    _op->setKey(key_type, key);
    if(sector<MifareClassicFirstJumboSector)
    {
        mfc_sector = new MifareClassicSector();
    }
    else
    {
        mfc_sector = new MifareClassicJumboSector();
    }
    dynamic_cast<MifareClassicSectorInterface*>(mfc_sector)->setSecurityLevel(MifareClassicSectorInterface::SL1);

    mfc_sector->setStartAddress(MifareClassicFunctions::classicBlockAddress(sector));

    //progress_display->setText(tr("Reading sector"));
    //progress_display->start(ui->sectorTable);
    _op->read(mfc_sector->startAddress(), mfc_sector->blockCount());
}

void RFIDKitWindow::mfc_plus_read()
{
    int sector = ui->sectorList->currentRow();
    if(sector < 0) {
        qWarning("Not reading classic tag - no sector");
        return;
    }

    MifareClassicKeyType key_type = (MifareClassicKeyType)ui->mfcKeyCombo->currentIndex();
    qDebug()<<"Authenticating with key "<<ui->mfcKeyEdit->text();
    MifarePlusKey key(ui->mfcKeyEdit->text());

    clearModel();
    _op->setPlusKey(key_type, key);
    if(sector<MifareClassicFirstJumboSector)
    {
        mfc_sector = new MifareClassicSector();
    }
    else
    {
        mfc_sector = new MifareClassicJumboSector();
    }
    dynamic_cast<MifareClassicSectorInterface*>(mfc_sector)->setSecurityLevel(MifareClassicSectorInterface::SL3);

    mfc_sector->setStartAddress(MifareClassicFunctions::classicBlockAddress(sector));

    //progress_display->setText(tr("Reading sector"));
    //progress_display->start(ui->sectorTable);
    _op->plusAuth(MifareClassicFunctions::classicBlockAddress(sector));
    _op->read(mfc_sector->startAddress(), mfc_sector->blockCount());
}

void RFIDKitWindow::mifareclassic_readFinished()
{
    int sector = ui->sectorList->currentRow();
    if(sector < 0) {
        return;
    }

    MifareClassicKeyType key_type = (MifareClassicKeyType)ui->mfcKeyCombo->currentIndex();
    MifareClassicSectorInterface* iclassic = dynamic_cast<MifareClassicSectorInterface*>(mfc_sector);

    if(!iclassic)
    {
        //should not happen
        return;
    }

    MifareKeyInterface* key_iface = nullptr;
    iclassic->setKeyType(key_type);
    if((_op->card() == MF_PLUS_S_SL3) || (_op->card() == MF_PLUS_X_SL3))
    {
        key_iface = new MifarePlusKey(ui->mfcKeyEdit->text());
    }
    else
    {
        key_iface = new MifareClassicKey(ui->mfcKeyEdit->text());
        iclassic->writeKey(key_type, *dynamic_cast<MifareClassicKey*>(key_iface));
    }
    mfc_sector->written(mfc_sector->blockCount()-1);

    statusBar()->showMessage(tr("Sector %1 read. Key valid").arg(sector), 4000);

    if(_op->type() != RFIDCardReaderInterface::RFIDReader_Simulator)
    {
        key_db->success(MifareClassicKeyID(_op->uid(), sector));
        if((_op->card() == MF_PLUS_S_SL3) || (_op->card() == MF_PLUS_X_SL3))
        {
            key_db->save(_op->uid(), sector, *dynamic_cast<MifarePlusKey*>(key_iface));
        }
        else
        {
            key_db->save(_op->uid(), sector, key_type, *dynamic_cast<MifareClassicKey*>(key_iface));
        }
    }
    qDebug()<<"Classic sector read. Bits "<<(iclassic->bitsValid(BD_Saved)? "valid" : "invalid");

    {
        MifareClassicPermission current_perm = (key_type == MifareClassicKeyB) ? MC_Permissions_KeyB : MC_Permissions_KeyA;
        /*Current or Saved does not matter here - the block has just been read*/
        if(!iclassic->bitsValid(BD_Current))
        {
            QMetaObject::invokeMethod(this,"warnAccessBits", Qt::QueuedConnection);
        }
        else
        {
            QString bogusBlocks;
            for(int i=0;i<(mfc_sector->blockCount()-1);++i)
            {
                if(!(iclassic->blockAccessPermissions(i, BD_Current).block_read & current_perm))
                {
                    bogusBlocks.append(QString::number(i)+",");
                }
            }

            if(!(iclassic->trailerAccessPermissions(BD_Current).bits_read & current_perm))
            {
                bogusBlocks.append(QString::number(mfc_sector->blockCount()-1)+",");
            }

            if(!bogusBlocks.isEmpty())
            {
                bogusBlocks.chop(1);
                //bogusBlocks.prepend(tr(" and blocks "));
            }

            if(key_type == MifareClassicKeyB)
            {
                QMetaObject::invokeMethod(this,"warnKey", Qt::QueuedConnection, Q_ARG(QString, "A"), Q_ARG(QString,bogusBlocks));
            }
            else if(!(iclassic->trailerAccessPermissions(BD_Current).keyB_read & current_perm))
            {
                QMetaObject::invokeMethod(this, "warnKey", Qt::QueuedConnection, Q_ARG(QString, "B"), Q_ARG(QString,bogusBlocks));
            }
            else if(!bogusBlocks.isEmpty())
            {
                QMetaObject::invokeMethod(this, "warnKey", Qt::QueuedConnection, Q_ARG(QString, QString()), Q_ARG(QString,bogusBlocks));
            }
        }
    }
    delete key_iface;
}

void RFIDKitWindow::mifareul_readFinished()
{
    statusBar()->showMessage(tr("Ultralight tag successfuly read"), 2000);
}

void RFIDKitWindow::mifareul_ext_readFinished()
{
    MifareUltralightAbstractEEPROM_EV1Interface* iulev = dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector);
    if(!iulev)
        return;

    //overwrite PWD & PACK
    if(iulev->lastValidBlock() != -1) {
        qWarning("Not restoring ULEV1 credentials - read failed");
    } else {
        qWarning()<<"Restoring ULEV1 credentials "<<iulev->getCredentials();
        iulev->restoreCredentials();
    }
    //already saved in the database
}

void RFIDKitWindow::card_blockWritten(int address, int code)
{
    if(!mfc_sector)
    {
        qWarning()<<"Ignoring a stale block-written reply without a sector";
        return;
    }

    int block_number = address - mfc_sector->startAddress();
    if((block_number < 0) || (block_number >= mfc_sector->blockCount()))
    {
        qWarning()<<"Ignoring a stale or out-of-range block-written reply for address "<<address;
        return;
    }

    if(code != RFIDCardReaderInterface::RW_OK)
    {
        statusBar()->showMessage(tr("Block %1 write failed - access denied").arg(block_number), 2000);
        return;
    }

    //if ok
    mfc_model->blockWritten(block_number);
    qDebug()<<"Block "<<block_number<<" is written";

    switch(mfc_sector->type())
    {
        case MifareSectorInterface::ST_MiClassic:
        case MifareSectorInterface::ST_MiClassicJumbo:
        {
            //if((_op->card() == MF_PLUS_S_SL3) || (_op->card() == MF_PLUS_X_SL3)) <- Same fucntion, no need to differentiate
            classic_block_written(address, true);
            break;
        }
        case MifareSectorInterface::ST_MiNTAGEEPROM:
        case MifareSectorInterface::ST_MiULExtEEPROM:
        {
            if((!_op->uid().isEmpty()) && (_op->type()==RFIDCardReaderInterface::RFIDReader_Hardware) &&
               (block_number >= (mfc_sector->blockCount()-2)))
            {
                //password or pack changed
                MifareUltralightAbstractEEPROM_EV1Interface* iface = dynamic_cast<MifareUltralightAbstractEEPROM_EV1Interface*>(mfc_sector);
                if(iface)
                {
                    key_db->save(_op->uid(), MifareUltralightEV1Security(iface->password(BD_Saved), iface->pack(BD_Saved)));
                }
            }
            break;
        }
        default:break;
    }
}

void RFIDKitWindow::card_read_done()
{
    qDebug()<<"Sector/Card read finished";
    if(!mfc_sector)
    {
        /*should not happen - called only in case of success*/
        return;
    }

    //read finished!
    mfc_model->setSector(mfc_sector);

    switch(mfc_sector->type())
    {
        case MifareSectorInterface::ST_MiClassic:
        case MifareSectorInterface::ST_MiClassicJumbo:
        {
            mifareclassic_readFinished();
            break;
        }
        case MifareSectorInterface::ST_MiUlEEPROM:
        case MifareSectorInterface::ST_MiUlCEEPROM:
        {
            mifareul_readFinished();
            break;
        }
        case MifareSectorInterface::ST_MiULExtEEPROM:
        case MifareSectorInterface::ST_MiNTAGEEPROM:
        {
            mifareul_ext_readFinished();
            break;
        }
        case MifareSectorInterface::ST_EmMARINE:
        {
            break;
        }
        default:
        {
            //should not happen
            break;
        }
    }

    ui->sectorTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
}

void RFIDKitWindow::card_write_done(bool success)
{
    if(mfc_sector)
    {
        if(isClassicCard(_op->card()))
        {
            classic_write_finished(success);
        }
        else if(isUltralightCard(_op->card()))
        {
            ultralight_write_finished(success);
        }
    }
}

void RFIDKitWindow::on_ulSaveButton_clicked()
{
    if(mfc_sector && isUltralightCard(_op->card()))
    {
        QString ofileStr = QFileDialog::getSaveFileName(this,tr("Save As"),
                                                        set.value("LastDir",QDir::homePath()).toString() + QLatin1String("/UL_") +
                                                        _op->uidString()+QLatin1String(".mfc"),
                                                        tr("Mifare Tag File (*.mfc)"));
        if(ofileStr.isEmpty())
            return;

        RFIDCardSimulatorReader* sim_reader = new RFIDCardSimulatorReader(this);
        QString error;
        if(!sim_reader->newCard(_op->card(), ofileStr, 4, &error))
        {
            QAutoCloseMessageBox::warning(10, this, tr("Save Tag"),
                                          tr("Cannot save the tag data to %1: %2").arg(ofileStr).arg(error));
        }
        else
        {
            for(int i=0;i<mfc_sector->blockCount();++i)
            {
                sim_reader->writeBlock(i, QByteArray(reinterpret_cast<const char*>(mfc_sector->raw(i)), 4));
            }
        }
        sim_reader->close();
        delete sim_reader;
    }
    else
    {
        ui->statusBar->showMessage(tr("Not an ultralight tag. Cannot save"), 2500);
    }
}

void RFIDKitWindow::on_actionEmulator_Open_triggered()
{
    QString cardfile = QFileDialog::getOpenFileName(this,tr("Open Tag File"),
                                                    set.value("LastDir","").toString(),tr("Mifare Tag File (*.mfc)"));
    if(cardfile.isEmpty()) {
        QMetaObject::invokeMethod(this, "readerValid", Qt::QueuedConnection, Q_ARG(bool, false));
        return;
    }

    set.setValue("LastDir",QFileInfo(cardfile).absolutePath());

    if(_op->type() != RFIDCardReaderInterface::RFIDReader_Simulator) {
        _op->setReader(RFIDCardReaderInterface::RFIDReader_Simulator, 0, QString());
    }

    _op->openCard(cardfile);
}

void RFIDKitWindow::cardOpened(bool success, const QString& error)
{
    if(!success)
    {
        QAutoCloseMessageBox::warning(10, this, tr("Tag Emulator"), tr("The tag file cannot be opened: %1").arg(error));
        //disconnect
        on_actionConnect_triggered();
    }
}

void RFIDKitWindow::on_actionEmulator_New_triggered()
{
    QString card = QInputDialog::getItem(this, tr("Select Emulator Type"), tr("Tag Type:"), mifareCardsNames,
                                         0,false);
    if((card.isEmpty())||(mifareCardsNames.indexOf(card)<0))
        return;
    emulateCardCore(static_cast<MifareCards>(mifareCardsNames.indexOf(card)));
}

void RFIDKitWindow::connectOperator()
{
    connect(_op, &RFIDCardReaderOperator::cardStateChanged, this, &RFIDKitWindow::cardStateChanged);
    connect(_op, &RFIDCardReaderOperator::readerValid, this, &RFIDKitWindow::readerValid);
    connect(_op, &RFIDCardReaderOperator::readerVersion, this, &RFIDKitWindow::readerVersionChanged);
    connect(_op, &RFIDCardReaderOperator::passwordChanged, this, &RFIDKitWindow::readerPasswordChanged);
    connect(_op, &RFIDCardReaderOperator::packReceived, this, &RFIDKitWindow::card_packReceived);
    connect(_op, &RFIDCardReaderOperator::cardOpened, this, &RFIDKitWindow::cardOpened);
    connect(_op, &RFIDCardReaderOperator::cardDetected, this, &RFIDKitWindow::cardDetected);
    connect(_op, &RFIDCardReaderOperator::cardUidChanged, this, &RFIDKitWindow::card_uidChanged);
    connect(_op, &RFIDCardReaderOperator::blockRead, this, &RFIDKitWindow::card_blockRead);
    connect(_op, &RFIDCardReaderOperator::blockWritten, this, &RFIDKitWindow::card_blockWritten);
    connect(_op, &RFIDCardReaderOperator::error, this, &RFIDKitWindow::operatorError);
    connect(_op, &RFIDCardReaderOperator::misc, this, &RFIDKitWindow::reader_misc);
}

void RFIDKitWindow::emulateCardCore(MifareCards card)
{
    QString cardfile = QFileDialog::getSaveFileName(this,tr("Save As"),
                                                    set.value("LastDir","").toString(),tr("Mifare Tag File (*.mfc)"));
    if(cardfile.isEmpty())
        return;
    if(!cardfile.endsWith(".mfc",Qt::CaseInsensitive))
        cardfile+=".mfc";

    set.setValue("LastDir",QFileInfo(cardfile).absolutePath());

    if(_op->type() != RFIDCardReaderInterface::RFIDReader_Simulator) {
        _op->setReader(RFIDCardReaderInterface::RFIDReader_Simulator, 0, QString());
    }

    _op->newCard(card, cardfile);
}

void RFIDKitWindow::on_actionEmulator_Close_triggered()
{
    if(_op->type()==RFIDCardReaderInterface::RFIDReader_Simulator)
    {
        _op->removeCard();
    }
}

void RFIDKitWindow::on_actionAuto_Mode_triggered()
{
    if(_op->type() != RFIDCardReaderInterface::RFIDReader_Hardware)
    {
        QAutoCloseMessageBox::warning(10, this, tr("Auto Mode"), tr("RFID reader is not connected"));
        return;
    }

    _op->disconnect(this);
    RFIDAutoModeDialog adlg(this);
    adlg.setOperator(_op);
    adlg.exec();
    connectOperator();
}

void RFIDKitWindow::on_actionFWUpdate_triggered()
{
    if(_op) {
        _op->dfu();
    }
}

#if 0
void RFIDKitWindow::on_actionSwitch_Interface_triggered()
{
    if(_op->switchInterface()) {
        //disconnect
        on_actionConnect_triggered();
    }
}
#endif

void RFIDKitWindow::on_actionPreferences_triggered()
{
    QRFIDPreferences prefs(this);
    if(prefs.exec()==QDialog::Accepted) {
        readSettings(false);
    }
}

void RFIDKitWindow::formatActionTriggered(QAction* act)
{
    mfc_model->setFormat(act->data().toInt());
}

void RFIDKitWindow::headerFormatActionTriggered(QAction* act)
{
    mfc_model->setHeaderStyle(act->data().value<MifareBlockRawTableModel::HeaderStyle>());
}

void RFIDKitWindow::clearModel()
{
    mfc_model->setSector(nullptr);
    if(mfc_sector)
    {
        delete mfc_sector;
        mfc_sector = nullptr;
    }
}

void RFIDKitWindow::currentReaderInvalidated()
{
    ultralight_password_rejected = false;
    ui->cardUIDLabel->setText(""/*tr("No Card")*/);
    ui->cardTypeLabel->setText(tr("Scanning…"));
    ui->cardTypeLabel->setToolTip(QString());

    ui->modUIDTool->setVisible(false);

    ui->actionRead_Tag->setEnabled(false);
    setTagToolsEnabled(false);
    clearModel();
}

void RFIDKitWindow::about()
{
    QMessageBox::about(this,tr("ODRFIDKit"),
                       tr("OpenDev RFID Toolkit v%1").arg(ODRFIDKIT_VERSION) %
                       QStringLiteral("<br>") %
                       tr("Developed by: Open Development LLC") %
                       QStringLiteral(" ") % QString(QChar(0x00A9)) % QString::fromLatin1(__DATE__ + 7) %
                       QStringLiteral("<br><a href=\"https://help.unitx.pro\">help.unitx.pro</a><br><br>") %
                       tr("3rd party software:") %
                       QStringLiteral("<ul>"
                                      "<li><a href=\"https://github.com/signal11/hidapi\">signal11/hidapi</a> (BSD-style license)</li>"
                                      "<li><a href=\"https://github.com/google/material-design-icons\">Material design icons</a> (Apache License 2.0)</li>"
                                      "<li><a href=\"https://github.com/GNOME/adwaita-icon-theme\">Adwaita icon theme</a> (CC-BY-SA 3.0)</li>"
                                      "</ul>"));
}

void RFIDKitWindow::on_actionConnect_triggered()
{
    if(_op->isValid()) {
        //simulator or cdc -> disconnect
        qDebug()<<"Disconnecting device";
        _op->close();
        readerValid(false);
    }
    else
    {
        exit_reader_subtype = 0;

#if 0
        int type = RFIDCardReaderInterface::RFIDReader_Invalid;
        QSerialPortSelectDialog sdlg(this);
        connect(&sdlg, &QSerialPortSelectDialog::updateRequested, this, &RFIDKitWindow::enumerateDevices);
        emit sdlg.updateRequested();
        if(sdlg.exec() != QDialog::Accepted)
        {
            return;
        }
        QString dev = sdlg.item();

        if(sdlg.currentIndex() == (sdlg.count()-1))
        {
            //simulator
            ui->menu_Emulator->setEnabled(true);
            type = RFIDCardReaderInterface::RFIDReader_Simulator;
        }
        else if(dev.contains("(CDC-AT"))
        {
            type = RFIDCardReaderInterface::RFIDReader_Hardware;
            exit_reader_subtype = RFIDCARDCDCATREADER_ID;
        }
        else if(dev.contains("(UART"))
        {
            type = RFIDCardReaderInterface::RFIDReader_Hardware;
            exit_reader_subtype = RFIDCARDUARTREADER_ID;
        }
        else if(dev.contains("(RS485"))
        {
            RS485SetupDialog selector(this);
            selector.setScanning(false);
            selector.readSettings();
            if(selector.exec() != QDialog::Accepted)
                return;

            selector.writeSettings();
            type = RFIDCardReaderInterface::RFIDReader_Hardware;
        }
        else if(dev.contains("HID"))
        {
            type = RFIDCardReaderInterface::RFIDReader_Hardware;
            exit_reader_subtype = RFIDCARDHIDREADER_ID;
        }
        _op->setReader(type, exit_reader_subtype, dev);
#else
        QEventLoop evt;
        ConnectionPopup cpp;
        connect(&cpp, &ConnectionPopup::done, &evt, [&evt](){
            evt.exit(0);
        });
        cpp.readSettings();
        cpp.move(ui->mainToolBar->mapToGlobal(QPoint(0, ui->mainToolBar->height())));
        cpp.setWindowModality(Qt::ApplicationModal);
        cpp.show();
        evt.exec();

        if(!cpp.accepted() || cpp.text().isEmpty()) {
            return;
        }

        QVariant selection = cpp.userData();
        qWarning()<<"Connecting to "<<selection;

        if(selection.isNull()) {
            //simulator
            ui->menu_Emulator->setEnabled(true);
            _op->setReader(RFIDCardReaderInterface::RFIDReader_Simulator, exit_reader_subtype, "");
        } else {
            int type = RFIDCardReaderInterface::RFIDReader_Invalid;
            QString dev = selection.toString();
            if(dev.contains("CDC-AT")) {
                type = RFIDCardReaderInterface::RFIDReader_Hardware;
                exit_reader_subtype = RFIDCARDCDCATREADER_ID;
            } else if(dev.contains("UART")) {
                type = RFIDCardReaderInterface::RFIDReader_Hardware;
                exit_reader_subtype = RFIDCARDUARTREADER_ID;
            } else if(dev.contains("RS485")) {
                RS485SetupDialog selector(this);
                selector.setScanning(false);
                selector.readSettings();
                if(selector.exec() != QDialog::Accepted)
                    return;

                selector.writeSettings();
                type = RFIDCardReaderInterface::RFIDReader_Hardware;
            } else if(dev.contains("HID")) {
                type = RFIDCardReaderInterface::RFIDReader_Hardware;
                exit_reader_subtype = RFIDCARDHIDREADER_ID;
            } else {
                qWarning("Unsupported device");
                return;
            }
            _op->setReader(type, exit_reader_subtype, dev);
        }
#endif
    }
}

void RFIDKitWindow::led_timeout()
{
    if(!_op->isValid() || (_op->type() == RFIDCardReaderInterface::RFIDReader_Simulator))
    {
        ui->rfidLed->off();
        return;
    }

    if(ui->rfidLed->isOn())
        ui->rfidLed->off();
    else
    {
        switch(_op->cardState())
        {
            case RFIDCardReaderOperator::CS_NoCard:     ui->rfidLed->on("#878787");break;
            case RFIDCardReaderOperator::CS_WriteFailed:
            case RFIDCardReaderOperator::CS_ReadFailed: ui->rfidLed->on("#8a39ed");break;
            case RFIDCardReaderOperator::CS_ReadPend:   ui->rfidLed->on("#ffc12e");break;
            case RFIDCardReaderOperator::CS_Written:
            case RFIDCardReaderOperator::CS_ReadOK:
            case RFIDCardReaderOperator::CS_Idle:       ui->rfidLed->on("#7FC135");break;
            case RFIDCardReaderOperator::CS_Writing:    ui->rfidLed->on("#EE3232");break;
        }
    }
}

void RFIDKitWindow::on_actionImport_Theme_triggered()
{
    QString thFile = QFileDialog::getOpenFileName(this,tr("Select Theme File"),QDir::homePath(),
                                                  tr("Theme Files (*.ini)"));
    if(!thFile.isEmpty())
    {
        const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if(appDataPath.isEmpty() || !QDir().mkpath(appDataPath))
        {
            QAutoCloseMessageBox::warning(10, this, tr("Theme Loading Failed"),
                                          tr("Theme could not be imported."));
            return;
        }

        QFile source(thFile);
        QSaveFile destination(appDataPath + QLatin1String("/theme.ini"));
        bool imported = source.open(QIODevice::ReadOnly) && destination.open(QIODevice::WriteOnly);
        while(imported && !source.atEnd())
        {
            const QByteArray chunk = source.read(64 * 1024);
            imported = !chunk.isEmpty() && (destination.write(chunk) == chunk.size());
        }
        imported = imported && destination.commit();
        if(!imported)
        {
            destination.cancelWriting();
        }

        if(imported)
        {
            QMessageBox::information(this, tr("Theme Loaded"), tr("Theme has been imported. Restart the application to apply it."));
        }
        else
        {
            QAutoCloseMessageBox::warning(10, this, tr("Theme Loading Failed"),
                                     tr("Theme could not be imported."));
        }
    }
}

void RFIDKitWindow::setTagToolsEnabled(bool on)
{
    ui->cardStack->setEnabled(on);
    ui->sectorGroup->setEnabled(on);
}

void RFIDKitWindow::udp_ready_read()
{
    qDebug()<<"udp readyRead called!";
    QByteArray datagram;
    QHostAddress client_a;
    quint16 client_p;
    ClientID client;

    qint64 dt_size;
    while(m_udp.hasPendingDatagrams())
    {
        dt_size = m_udp.pendingDatagramSize();
        if((dt_size <= 0) || (dt_size > MaxNetworkCommandSize))
        {
            datagram.resize(dt_size > 0 ? int(dt_size) : 0);
            m_udp.readDatagram(datagram.data(), datagram.size(), &client_a, &client_p);
            qWarning()<<"Discarded UDP datagram with invalid size "<<dt_size;
            continue;
        }
        datagram.resize(dt_size);
        dt_size = m_udp.readDatagram(datagram.data(),dt_size,&client_a,&client_p);
        if(dt_size<=0)
            continue;
        datagram.resize(dt_size);
        qDebug()<<"Read datagram"<<QString::fromUtf8(datagram)<<" from "<<client_a.toString()<<"@"<<client_p;

        client = ClientID(client_a,client_p);
        if(!udp_clients.contains(client))
        {
            if(udp_clients.size() >= MaxNetworkClients)
            {
                qWarning()<<"Ignoring UDP client: client limit reached";
                continue;
            }
            qDebug()<<"New udp client "<<client.toString();
            udp_clients.insert(client,UdpClient(datagram));
        }
        else
        {
            udp_clients[client].buffer.append(datagram);
        }
        if(udp_clients[client].buffer.size() > MaxNetworkCommandSize)
        {
            qWarning()<<"Dropping UDP client with an oversized command buffer";
            udp_clients.remove(client);
            continue;
        }
        processUdpDatagram(client);
    }
}

void RFIDKitWindow::tcp_newConnection()
{
    QTcpSocket* clientSocket;
    TcpClient* tcpClient;
    while((clientSocket=m_tcp->nextPendingConnection()))
    {
        if(tcp_clients.size() >= MaxNetworkClients)
        {
            qWarning()<<"Rejecting TCP client: client limit reached";
            clientSocket->disconnectFromHost();
            clientSocket->deleteLater();
            continue;
        }
        qintptr id = clientSocket->socketDescriptor();
        qDebug()<<"Connected new TCP client with ID "<<id;
        tcp_clients[id] = tcpClient = new TcpClient(clientSocket,this);
        connect(tcpClient,SIGNAL(newDatagram(QByteArray)),this,SLOT(tcp_clientMessage(QByteArray)));
        connect(tcpClient,SIGNAL(closed()),this,SLOT(tcp_clientClosed()));
    }
}

void RFIDKitWindow::tcp_clientClosed()
{
    TcpClient* client = qobject_cast<TcpClient*>(sender());
    if(client)
    {
        client->disconnect(this);
        qintptr id = tcp_clients.key(client,-1);
        if(id>=0)
        {
            qDebug()<<"Client's fd was "<<id;
            tcp_clients.remove(id);
        }
        client->deleteLater();
    }
}

void RFIDKitWindow::tcp_clientMessage(const QByteArray& data)
{
    TcpClient* client = qobject_cast<TcpClient*>(sender());
    if(!client) return;

    if(data=="uid")
    {
        client->fd->write(QString("uid> %1\n")
                          .arg(_op->uidString().isEmpty() ? "no card" : _op->uidString()).toUtf8());
    }
    else if(data=="state")
    {
        client->fd->write(QString("state> reader: %1; card: %2\n")
                          .arg(_op->isValid() ? "connected" : "disconnected")
                          .arg(!_op->uid().isEmpty() ? "present" : "not present")
                          .toUtf8());
    }
    else
    {
        client->fd->write(QString("help> uid,state\n").toUtf8());
    }
    client->fd->flush();
}

void RFIDKitWindow::processUdpDatagram(const ClientID &client)
{
    if(!udp_clients.contains(client))
    {
        qWarning()<<"This is a BUG: hash must containt client "<<client.toString();
        return;
    }

    UdpClient& data = udp_clients[client];
    int ind;
    qDebug()<<"Requested buffer process: "<<data.buffer;
    while((ind=data.buffer.indexOf('\n'))>=0)
    {
        //packet in '[0;ind]'
        QByteArray packet = data.buffer.left(ind);
        data.buffer.remove(0,ind+1);
        if(packet.endsWith('\r'))
        {
            packet.chop(1);
        }
        qDebug()<<"Found packet "<<packet<<", leftover "<<data.buffer;

        if(packet.isEmpty())
        {
            continue;
        }

        if(!data.init)
        {
            if(packet=="hello")
            {
                data.init = true;
                m_udp.writeDatagram("ok\n",client.address,client.port);
            }
            else
            {
                udp_clients.remove(client);
                return;
            }
        }
        else if(packet=="goodbye")
        {
            m_udp.writeDatagram("ok\n",client.address,client.port);
            udp_clients.remove(client);
            return;
        }
        else if(packet=="state")
        {
            m_udp.writeDatagram(QString("state> reader: %1; card: %2\n")
                                .arg(_op->isValid() ? "connected" : "disconnected")
                                .arg(!_op->uid().isEmpty() ? "present" : "not present")
                                .toUtf8(),
                                client.address,client.port);
        }
        else if(packet=="uid")
        {
            m_udp.writeDatagram(QString("uid> %1\n")
                                .arg(_op->uid().isEmpty() ? "no card" : _op->uidString()).toUtf8(),
                                client.address,client.port);
        }
        else if(packet=="help")
        {
            m_udp.writeDatagram("help> hello,state,uid,goodbye\n",client.address,client.port);
        }
        else
        {
            m_udp.writeDatagram("nop\n",client.address,client.port);
        }
    }
}

void RFIDKitWindow::closeUDP()
{
    udpBroadcast("goodbye\n");
    udp_clients.clear();
    m_udp.close();
}

void RFIDKitWindow::udpBroadcast(const QByteArray& data)
{
    QHashIterator<ClientID,UdpClient> it(udp_clients);
    while(it.hasNext())
    {
        it.next();
        if(!it.value().init)
        {
            continue;
        }
        qWarning()<<"Send UDP datagram to "<<it.key().address.toString()<<":"<<it.key().port;
        m_udp.writeDatagram(data,it.key().address,it.key().port);
    }
    m_udp.flush();
}

void RFIDKitWindow::tcpStart()
{
    tcpStop();

    QSettings set;
    if (!m_tcp->listen(QHostAddress::Any, set.value(SETTINGS_TCP_PORT,SETTINGS_TCP_PORT_DEFAULT).toUInt()))
    {
        statusBar()->showMessage(tr("Unable to start the server at %1: %2")
                                 .arg(set.value(SETTINGS_TCP_PORT,SETTINGS_TCP_PORT_DEFAULT).toUInt())
                                 .arg(m_tcp->errorString()),5000);
        return;
    }

    qDebug() << m_tcp->isListening() << " - tcp listeninig on port "
             <<set.value(SETTINGS_TCP_PORT,SETTINGS_TCP_PORT_DEFAULT).toUInt();
}

void RFIDKitWindow::tcpStop()
{
    QList<qintptr> cids = tcp_clients.keys();
    foreach(qintptr id, cids)
    {
        /*
         * MUST close NOW - not 'later'
        */
        delete tcp_clients.take(id);
    }
    m_tcp->close();
}

void RFIDKitWindow::tcpBroadcast(const QByteArray& data)
{
    QMapIterator<qintptr,TcpClient*> it(tcp_clients);
    while(it.hasNext())
    {
        it.next();
        it.value()->fd->write(data);
        it.value()->fd->flush();
    }
}
