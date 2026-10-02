#include "odhiddfumain.h"
#include <ui_odhiddfumain.h>

#include <QMainWindow>
#include <QMessageBox>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QCommandLinkButton>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QSerialPort>
#include <QUrl>

#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>

#include <QDebug>

#define DFU_TRANSPORT_ROLE "transport"
#define DFU_IDS_ROLE       "id"
#define DFU_SELECT_ROLE    "select"


ODHidDFUMain::ODHidDFUMain(const HidDFUParams& params) :
    QMainWindow(nullptr),
    ui(new Ui::ODHidDFUMain),
    d_instance(nullptr), cmd_opts(params)
{
    ui->setupUi(this);
    setWindowTitle(this->windowTitle() + QLatin1String(" " HIDDFU_VERSION));
    ui->actionDisconnect->setVisible(false);
    connect(ui->actionExit, &QAction::triggered, qApp, &QCoreApplication::quit);

    QMetaObject::invokeMethod(this, "setup", Qt::QueuedConnection);
}

ODHidDFUMain::~ODHidDFUMain()
{
    if(d_instance) {
        d_instance->disconnect(this);
        delete d_instance;
        d_instance = nullptr;
    }
    delete ui;
}

void ODHidDFUMain::execute(const QStringList& products)
{
    if(d_instance) {
        ui->stackedWidget->removeWidget(d_instance);
        d_instance->setParent(this);
        delete d_instance;
    }

    d_instance = new HIDDfu(this);
#ifndef OD_NO_DEVELOPER
    d_instance->setDevMode(cmd_opts.meijin);
    if(cmd_opts.stmsw)
    {
        d_instance->setAutoAction(HIDDfu::Action_STM);
    }
#else
    d_instance->setDevMode(false);
#endif

    if(cmd_opts.transport.isEmpty())
    {
        QMessageBox::warning(this, tr("DFU Device"), tr("Specify a transport for this product type"));
        d_instance->deleteLater();
        d_instance = nullptr;
        return;
    }

    d_instance->setTransport(cmd_opts.transport);
    if(!cmd_opts.product.isEmpty())
    {
        d_instance->setProduct(cmd_opts.product);
    }
    else
    {
        d_instance->setProducts(products);
    }

    d_instance->setMedium(cmd_opts.medium);

    if(cmd_opts.baudrate > 0)
    {
        d_instance->setBaudrate(cmd_opts.baudrate);
    }
    else
    {
        d_instance->setBaudrate(115200);
    }

    if(cmd_opts.address>=0)
    {
        d_instance->setAddress(cmd_opts.address);
    }
    else
    {
        d_instance->setAddress(-1);
    }

    if(!d_instance->init())
    {
        QMessageBox::critical(this, tr("DFU Device"),
                              tr("The device is not connected or is not in DFU mode. "));
        d_instance->deleteLater();
        d_instance = nullptr;
        return;
    }

    if(!cmd_opts.fw_file.isEmpty())
    {
        d_instance->setFile(cmd_opts.fw_file);
    }
#ifndef OD_NO_DEVELOPER
    if(cmd_opts.stmsw)
    {
        d_instance->setAutoAction(HIDDfu::Action_STM);
    }
#endif
    d_instance->setAutoAccept(cmd_opts.auto_mode);

    ui->stackedWidget->addWidget(d_instance);
    connect(d_instance, &HIDDfu::finished, this, &ODHidDFUMain::dfuFinished);
    ui->stackedWidget->setCurrentIndex(1);
    ui->actionDisconnect->setVisible(true);

    /* Clear one-time option */
    cmd_opts.transport.clear();
    cmd_opts.medium.clear();
    cmd_opts.type.clear();
    cmd_opts.product.clear();
    cmd_opts.address = -1;
    cmd_opts.baudrate = 0;
}

void ODHidDFUMain::setup()
{
    const QString setupSource =  ":/dfu/data/variables.json";

    QFile ifd(setupSource);
    if(ifd.open(QFile::ReadOnly)) {
        QJsonParseError jerror;
        QJsonDocument descrDoc = QJsonDocument::fromJson(ifd.readAll(), &jerror);
        if(descrDoc.isNull() || !descrDoc.isObject()) {
            qWarning()<<"File "<<ifd.fileName()<<" is invalid ("<<jerror.errorString()<<")";
            qCritical()<<"Built-in DFU devices data is invalid";
            qApp->exit(1);
            return;
        }

        QJsonObject descrRoot = descrDoc.object();
        const QString databaseVersion = descrRoot.value(QLatin1String("version")).toString();
        qWarning()<<"Parsing DFU devices data v "<<descrRoot.value("version").toString()<<" from "<<descrRoot.value("date").toString();

        QJsonObject dfuVariants = descrRoot.value("devices").toObject();
        if(databaseVersion.isEmpty() || dfuVariants.isEmpty()) {
            qWarning()<<"DFU devices data is empty or incomplete";
            qCritical()<<"Built-in DFU devices data is invalid";
            qApp->exit(1);
            return;
        }
        QStringList dfuKeys = dfuVariants.keys();
        std::sort(dfuKeys.begin(), dfuKeys.end());

        QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(ui->selectorPage->layout());
        if(!mainLayout) {
            mainLayout = new QVBoxLayout(ui->selectorPage);
        } else {
            while(QLayoutItem* item = mainLayout->takeAt(0)) {
                delete item->widget();
                delete item;
            }
        }
        foreach(const QString& key, dfuKeys) {
            qWarning()<<"Processing dfu variant "<<key;
            QJsonObject dfuVariant = dfuVariants.value(key).toObject();

            QCommandLinkButton* dfuButton = new QCommandLinkButton(this);
            dfuButton->setText(dfuVariant.value("name").toString());
            dfuButton->setDescription(dfuVariant.value("descr").toString());

            for(QJsonObject::const_iterator it = dfuVariant.constBegin(); it != dfuVariant.constEnd(); ++it) {
                if((it.key() != "name") && (it.key() != "descr")) {
                   dfuButton->setProperty(it.key().toLatin1().constData(), it.value().toVariant());
                }
            }

            connect(dfuButton, &QCommandLinkButton::clicked, this, &ODHidDFUMain::dfuSelected);
            mainLayout->addWidget(dfuButton);

            if(!cmd_opts.product.isEmpty())
            {
                if(cmd_opts.transport.isEmpty() && dfuVariant.value("id").toVariant().toList().contains(QVariant(cmd_opts.product))) {
                    cmd_opts.transport = dfuVariant.value(DFU_TRANSPORT_ROLE).toString();
                }
            }
            else if(cmd_opts.type == key)
            {
                qWarning()<<"Automatically selecting "<<cmd_opts.type<<" option by user choice";
                QTimer::singleShot(10, dfuButton, SIGNAL(clicked()));
            }
        }
    } else {
        qCritical()<<"Binary is corrupt - cannot parse dfu data";
        qApp->exit(1);
        return;
    }

    if(!cmd_opts.product.isEmpty()) {
        QMetaObject::invokeMethod(this, "execute", Qt::QueuedConnection);
    }
}

void ODHidDFUMain::on_actionAbout_triggered()
{
    QMessageBox::about(this,QStringLiteral("OD DFU Utility " HIDDFU_VERSION),
                       tr("Utility to flash Open Development DFU capable devices.")+
                       QStringLiteral("<br><br>")+
                       tr("Developed by: Open Development LLC")+
                       QStringLiteral(" ")+QString(QChar(0x00A9))+QStringLiteral("2020")+
                       QStringLiteral("<br><br>")+
                       QStringLiteral("<a href=\"https://help.unitx.pro\">help.unitx.pro</a><br><br>")+
                       tr("Uses:")+
                       QStringLiteral("<ul>"
                                      "<li><a href=\"https://github.com/signal11/hidapi\">signal11/hidapi</a> (BSD-style license)</li>"
                                      "<li><a href=\"https://google.github.io/material-design-icons/\">Material Icons</a> (Apache License Version 2.0)</li>"
                                      "</ul>"));
}

void ODHidDFUMain::on_actionDisconnect_triggered()
{
    if(d_instance) {
        d_instance->close();
        QMetaObject::invokeMethod(this, &ODHidDFUMain::dfuFinished, Qt::QueuedConnection);
    }
}

void ODHidDFUMain::dfuSelected()
{
    QCommandLinkButton* src = qobject_cast<QCommandLinkButton*>(sender());
    if(!src)
    {
        return;
    }

    QString transport = src->property(DFU_TRANSPORT_ROLE).toString();
    QVariantList ids = src->property(DFU_IDS_ROLE).toList();
    QVariantList selections = src->property(DFU_SELECT_ROLE).toList();
    qWarning()<<"Clicked: "<<transport<<" / "<<ids;

    if(ids.isEmpty())
    {
        QMessageBox::critical(this, tr("DFU Device"), tr("No known devices for this type"));
        return;
    }

    QSet<QString> productIds;
    for(const QVariant& pid : qAsConst(ids)) {
        productIds.insert(pid.toString());
    }

    if((transport != "usb") && (transport != "rs485"))
    {
        QMessageBox::warning(this, tr("DFU Device"),
                             tr("The %1 transport is not supported by this version. Please upgrade.").arg(transport));
        return;
    }

    foreach(const QVariant& selItem, selections)
    {
        if((selItem.toString() == "serial") && !selectSerialPort()) {
            cmd_opts.medium.clear();
            return;
        } else if((selItem.toString() == "address") && !selectDeviceAddress()) {
            cmd_opts.medium.clear();
            cmd_opts.address = -1;
            return;
        } else if((selItem.toString() == "speed") && !selectBaudRate()) {
            cmd_opts.medium.clear();
            cmd_opts.address = -1;
            cmd_opts.baudrate = 0;
            return;
        }
    }

    cmd_opts.transport = transport;
    execute(productIds.values());
}

bool ODHidDFUMain::selectSerialPort()
{
    if(cmd_opts.medium.isEmpty()) {
        QList<QSerialPortInfo> sys_ports = QSerialPortInfo::availablePorts();
        QHash<QString,QString> sys_ports_hash;
        foreach(const QSerialPortInfo& pinfo, sys_ports) {
#ifdef Q_OS_WIN
            if(pinfo.portName() != "COM1")
#endif
            {
                sys_ports_hash.insert(QString("%1 (%2:%3:%4)")
                                      .arg(pinfo.portName())
                                      .arg(pinfo.manufacturer())
                                      .arg(pinfo.vendorIdentifier())
                                      .arg(pinfo.serialNumber()), pinfo.portName());
            }
        }

        if(sys_ports_hash.isEmpty()) {
            QMessageBox::warning(this, tr("DFU Device"), tr("This dfu type requires a serial device, but none found"));
            return false;
        }

        bool ok;
        QString sel = QInputDialog::getItem(this, tr("Serial port"),
                                            tr("Select a serial port:"), sys_ports_hash.keys(), 0, false, &ok);
        if(!ok)
        {
            return false;
        }

        cmd_opts.medium = sys_ports_hash.value(sel);
    }

    return true;
}

void ODHidDFUMain::dfuFinished()
{
    if(cmd_opts.auto_mode) {
        QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    } else if(d_instance) { /* Protect against multiple calls */
        ui->stackedWidget->removeWidget(d_instance);
        d_instance->setParent(this);
        d_instance->disconnect(this);
        d_instance->deleteLater();
        d_instance = nullptr;
        ui->actionDisconnect->setVisible(false);
        ui->stackedWidget->setCurrentIndex(0);
    }
}

bool ODHidDFUMain::selectDeviceAddress()
{
    if(cmd_opts.address < 0) {
        bool ok;
        cmd_opts.address = QInputDialog::getInt(this, tr("Device address"),
                                                tr("Device address (-1 - scan all)") + ":", -1, -1, 254, 1, &ok);
        if(!ok)
            return false;
    }

    return true;
}

bool ODHidDFUMain::selectBaudRate()
{
    if(cmd_opts.baudrate == 0) {
        bool ok;
        cmd_opts.baudrate = QInputDialog::getInt(this, tr("Baudrate"), tr("Baudrate")+":",
                                                 115200, 1, 5000000, 1, &ok);
        if(!ok)
            return false;
    }

    return true;
}
