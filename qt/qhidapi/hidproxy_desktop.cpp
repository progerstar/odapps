#include "hidproxy_desktop.h"
#include <endianrw.h>

#include <QThread>
#include <QDebug>
#include <QDateTime>
#include <QVector>

namespace {
QString fromWideString(const wchar_t* value)
{
    return value ? QString::fromWCharArray(value) : QString();
}
}

HIDProxyDesktop::HIDProxyDesktop(QObject* parent): HIDProxy(parent),
    handle(nullptr), _quirks(0)
#if !HIDPROXY_NO_LIBUSB
    , raw_handle(nullptr), raw_cfg(nullptr)
#endif
{
    HIDProxy::initContext();
}

HIDProxyDesktop::~HIDProxyDesktop()
{
    closeRaw();

    if(handle)
    {
        hid_close(handle);
        handle = nullptr;
    }
}

static QStringList _hid_enumerate(int vid, const QString& manufacturer, int pid, const QString& product)
{
    QRegExp manufacturerRX(manufacturer.isEmpty() ? ".*" : manufacturer);
    QRegExp productRX(product.isEmpty() ? ".*" : product);

    QStringList ret;
    struct hid_device_info *devs, *cur_dev;
    devs = hid_enumerate((vid<=0) ? 0 : vid, (pid<=0) ? 0 : pid);
    cur_dev = devs;
    while (cur_dev)
    {
        QString cvendor  = fromWideString(cur_dev->manufacturer_string);
        QString cproduct = fromWideString(cur_dev->product_string);
        qWarning()<<"HID "<<cvendor<<" / "<<cproduct<<" @ "<<fromWideString(cur_dev->serial_number);
        if( ((vid<0)||(cur_dev->vendor_id==vid)) && manufacturerRX.exactMatch(cvendor) /*(manufacturer.isEmpty() || (cvendor == manufacturer))*/ &&
            ((pid<0)||(cur_dev->product_id==pid)) && productRX.exactMatch(cproduct) /* (product.isEmpty() || (cproduct==product))*/ )
        {
            QString sn = fromWideString(cur_dev->serial_number);
            ret.append(QString("%1:%2:").arg(cur_dev->vendor_id, 4, 16, QLatin1Char('0')).arg(cur_dev->product_id, 4, 16, QLatin1Char('0')) +
                       (!sn.isEmpty() ? sn : QString("S/N")));
        }
        cur_dev = cur_dev->next;
    }
    hid_free_enumeration(devs);
    return ret;
}

QStringList HIDProxyDesktop::availableHidDevices(int vid, int pid, const QString& product) const
{
    return _hid_enumerate(vid, QString(), pid, product);
}

QStringList HIDProxyDesktop::availableHidDevices(const QString& manufacturer, const QString& product) const
{
    return _hid_enumerate(-1, manufacturer, -1, product);
}

QList<HIDProxy::Descriptor> HIDProxyDesktop::availableHidDevicesList(int vid, int pid, const QString& product)
{
    QRegExp productRX(product.isEmpty() ? ".*" : product);

    QList<HIDProxy::Descriptor> ret;
    struct hid_device_info *devs, *cur_dev;
    devs = hid_enumerate((vid<=0) ? 0 : vid, (pid<=0) ? 0 : pid);
    cur_dev = devs;
    while (cur_dev)
    {
        QString cvendor  = fromWideString(cur_dev->manufacturer_string);
        QString cproduct = fromWideString(cur_dev->product_string);
        qWarning()<<"HID "<<cvendor<<" / "<<cproduct<<" @ "<<fromWideString(cur_dev->serial_number);
        if( ((vid<0)||(cur_dev->vendor_id==vid)) &&
            ((pid<0)||(cur_dev->product_id==pid)) &&
            productRX.exactMatch(cproduct) )
        {
            HIDProxy::Descriptor d;
            d.vid = cur_dev->vendor_id;
            d.pid = cur_dev->product_id;
            d.manufacturer = cvendor;
            d.product = cproduct;
            d.serial = fromWideString(cur_dev->serial_number);
            d.usagePage = cur_dev->usage_page;
            d.usage = cur_dev->usage;
            d.interfaceNumber = cur_dev->interface_number;
            ret.append(d);
        }
        cur_dev = cur_dev->next;
    }
    hid_free_enumeration(devs);
    return ret;
}

QString HIDProxyDesktop::device() const
{
    if(handle)
    {
        wchar_t serial_str[128] = {};
        const QString serial = (hid_get_serial_number_string(handle,serial_str,128) >= 0)
                ? fromWideString(serial_str) : QString();
        return QString("%1:%2:").arg(d_vid, 4, 16, QLatin1Char('0')).arg(d_pid, 4, 16, QLatin1Char('0')) +
                (serial.isEmpty() ? QStringLiteral("S/N") : serial);
    }
    return QString();
}

bool HIDProxyDesktop::setDevice(const QString& name)
{
    QRegExp edescr("([0-9A-F]{4,4})\\:([0-9A-F]{4,4})\\:(.+)", Qt::CaseInsensitive);
    if(isOpen())
    {
        close();
    }

    if(name.isEmpty() || !edescr.exactMatch(name))
    {
        qWarning()<<"Invalid device description "<<name;
        return false;
    }

    const quint16 vid = edescr.cap(1).toUInt(nullptr, 16);
    const quint16 pid = edescr.cap(2).toUInt(nullptr, 16);
    QString serial = edescr.cap(3);
    int selectedInterface = -1;
    int selectedUsagePage = -1;

    QRegExp interfaceSelector("(.+)#IF=([0-9]+)", Qt::CaseInsensitive);
    QRegExp usagePageSelector("(.+)#UP=([0-9A-F]+)", Qt::CaseInsensitive);
    if(interfaceSelector.exactMatch(serial)) {
        serial = interfaceSelector.cap(1);
        selectedInterface = interfaceSelector.cap(2).toInt();
    } else if(usagePageSelector.exactMatch(serial)) {
        serial = usagePageSelector.cap(1);
        selectedUsagePage = usagePageSelector.cap(2).toInt(nullptr, 16);
    }

    QVector<wchar_t> wsn(serial.size() + 1, L'\0');
    wsn[serial.toWCharArray(wsn.data())]=L'\0';

    if((selectedInterface >= 0) || (selectedUsagePage >= 0)) {
        struct hid_device_info* devices = hid_enumerate(vid, pid);
        for(struct hid_device_info* dev = devices; dev; dev = dev->next) {
            const QString candidateSerial = fromWideString(dev->serial_number);
            const bool serialMatches = (serial == QLatin1String("S/N")) ||
                                       (candidateSerial == serial);
            const bool selectorMatches = (selectedInterface >= 0)
                    ? (dev->interface_number == selectedInterface)
                    : (dev->usage_page == selectedUsagePage);
            if(serialMatches && selectorMatches && dev->path) {
                handle = hid_open_path(dev->path);
                break;
            }
        }
        hid_free_enumeration(devices);
    } else {
        // A USB serial descriptor is optional. "S/N" is the enumeration
        // placeholder used by the UI; a null serial asks hidapi to open the
        // first matching device instead of looking for the literal placeholder.
        handle = hid_open(vid, pid,
                          (serial == QLatin1String("S/N")) ? nullptr : wsn.constData());
    }
    if(!handle)
    {
        qWarning()<<"Cannot open HID device "<<name;
        return false;
    }

    d_vid = vid;
    d_pid = pid;
    hid_set_nonblocking(handle, 1);
    if(queryInterval >= 0)
    {
        queryTimerID = startTimer(queryInterval);
    }
    emit deviceChanged(name);
    return true;
}

HIDProxy::ClassDescriptor HIDProxyDesktop::deviceClass()
{
#if !HIDPROXY_NO_LIBUSB
    if(isOpen() && (raw_handle || openRaw()))
    {
        libusb_device_descriptor ldd;
        libusb_device* udev = libusb_get_device(raw_handle);
        int ret = libusb_get_device_descriptor(udev, &ldd);
        if(ret != 0)
        {
            qWarning()<<"Cannot get device descriptor: "<<ret<<": "<<libusb_error_name(ret);
            return {0,0,0};
        }
        return {ldd.bDeviceClass, ldd.bDeviceSubClass, ldd.bDeviceProtocol};
    }
#endif
    return {0, 0, 0};
}

int HIDProxyDesktop::interfaceCount()
{
#if !HIDPROXY_NO_LIBUSB
    if(isOpen() && (raw_handle || openRaw()))
    {
        return raw_cfg->bNumInterfaces;
    }
#endif
    return 0;
}

HIDProxy::ClassDescriptor HIDProxyDesktop::interfaceClass(int iface)
{
#if !HIDPROXY_NO_LIBUSB
    if(isOpen() && (raw_handle || openRaw()))
    {
        if(iface < raw_cfg->bNumInterfaces)
        {
            const libusb_interface_descriptor* idsc = &raw_cfg->interface[iface].altsetting[0];
            qWarning()<<"Interface class "<<idsc->bInterfaceClass<<":"<<idsc->bInterfaceSubClass<<":"<<idsc->bInterfaceProtocol;
            return {idsc->bInterfaceClass, idsc->bInterfaceSubClass, idsc->bInterfaceProtocol};
        }
    }
#endif
    return {0,0,0};
}

int HIDProxyDesktop::endpointCount(int iface)
{
#if !HIDPROXY_NO_LIBUSB
    if(isOpen() && (raw_handle || openRaw()))
    {
        if(iface < raw_cfg->bNumInterfaces)
        {
            return  raw_cfg->interface[iface].altsetting[0].bNumEndpoints;
        }
    }
#endif
    return 0;
}

bool HIDProxyDesktop::isOpen() const
{
    return handle ? true : false;
}

void HIDProxyDesktop::close()
{
    if(reply_timer.interval())
    {
        reply_timer.stop();
    }
    pending_op.clear();
    if(queryTimerID>=0) {
        killTimer(queryTimerID);
        queryTimerID=-1;
    }

    closeRaw();
    if(handle)
    {
        hid_close(handle);
        handle = nullptr;
        emit deviceChanged(QString());
    }
}

bool HIDProxyDesktop::getFeature(int sz, QByteArray& output, int rID)
{
    output.clear();
    if(!isOpen() || (sz < 0) || (sz > hid_report_size)) {
        return false;
    }

    /* ReportID is mandatory */
    QByteArray featureReport(hid_report_size + 1, char(0));
    uint8_t* feature_report = reinterpret_cast<uint8_t*>(featureReport.data());
    if(rID >= 0)
    {
        feature_report[0] = rID;
    }
    else if(hid_report_id>=0)
    {
        feature_report[0] = hid_report_id;
    }
    else
    {
        feature_report[0] = 0;
    }

    int res = -1;
    if(0) {
#ifdef Q_OS_WIN
    } else if(_quirks & QUIRK_INPUT_VS_FEATURE) {
        res = hid_get_input_report(handle, feature_report, (sz + 1));
#endif
    } else {
        res = hid_get_feature_report(handle, feature_report, (sz + 1));
    }

    if(res>=0)
    {
        if(res)
        {
            output.resize(res-1);
            memcpy(output.data(), feature_report + 1, res - 1);
        }
        return true;
    }
    qWarning()<<"getFeature returned "<<res;
    return false;
}

bool HIDProxyDesktop::rawSend(int iface, const QByteArray& data, int timeout)
{
#if HIDPROXY_NO_LIBUSB
    qWarning("BULK IO is not supported on this platform");
    Q_UNUSED(iface);Q_UNUSED(data);Q_UNUSED(timeout);
    return false;
#else
    if((!raw_handle && !openRaw()) || (iface >= raw_cfg->bNumInterfaces))
    {
        qWarning()<<"BULK IO failure - cannot create raw connection / no such interface ("<<iface<<" of "<<int(raw_cfg->bNumInterfaces)<<")";
        return false;
    }

    int maxsize = 0;
    int ep_out = -1;
    const libusb_interface_descriptor* idesc = &raw_cfg->interface[iface].altsetting[0];
    for(int i=0; i< idesc->bNumEndpoints; ++i)
    {
        /* BULK endpoint (0x02) */
        if(((idesc->endpoint[i].bmAttributes & 0x03) == 0x02) && !(idesc->endpoint[i].bEndpointAddress & 0x80))
        {
            ep_out = idesc->endpoint[i].bEndpointAddress;
            maxsize = idesc->endpoint[i].wMaxPacketSize;
            break;
        }
    }
    if(ep_out < 0)
    {
        qWarning()<<"Interface "<<iface<<" does not have a BULK OUT endpoint";
        return false;
    }

    int claim_res = libusb_claim_interface(raw_handle, iface);
    if(claim_res != 0)
    {
        qWarning()<<"Cannot claim interface "<<iface<<": "<<claim_res<<" ("<<libusb_error_name(claim_res)<<") - will try anyway";
    }

    int bytes_sent = 0;
    int res = libusb_bulk_transfer(raw_handle, (unsigned char)ep_out, (unsigned char*)(data.constData()), data.size(), &bytes_sent, timeout);

    if((res == 0 /*success*/) && ((maxsize && (bytes_sent == maxsize)) || !(bytes_sent & 0x3F)))
    {
        //ZLP
        int zlp = 0;
        libusb_bulk_transfer(raw_handle, (unsigned char)ep_out, 0, 0, &zlp, timeout);
        qWarning()<<"BULK IO: zlp issued";
    }

    if(claim_res == 0)
    {
        libusb_release_interface(raw_handle, iface);
    }

    if((res<0) || (data.size() != bytes_sent))
    {
        qWarning()<<"BULK IO failure: sent "<<data.size()<<", written "<<bytes_sent;
        qWarning()<<"    error "<<res<<" ("<<libusb_error_name(res)<<")";
        return false;
    }
    return true;
#endif
}

QByteArray HIDProxyDesktop::rawReceive(int iface, int maxlen, int timeout, bool* ok)
{
#if HIDPROXY_NO_LIBUSB
    Q_UNUSED(iface);Q_UNUSED(maxlen);Q_UNUSED(timeout);Q_UNUSED(ok);
    qWarning()<<"BULK IO is not supported on this platform";
    return QByteArray();
#else
    if((!raw_handle && !openRaw()) || (iface >= raw_cfg->bNumInterfaces))
    {
        qWarning()<<"BULK IO failure - cannot create raw connection / no such interface ("<<iface<<" of "<<int(raw_cfg->bNumInterfaces)<<")";
        if(ok) *ok=false;
        return QByteArray();
    }

    int ep_in = -1;
    const libusb_interface_descriptor* idesc = &raw_cfg->interface[iface].altsetting[0];
    for(int i=0; i< idesc->bNumEndpoints; ++i)
    {
        /* BULK endpoint (0x02) */
        if(((idesc->endpoint[i].bmAttributes & 0x03) == 0x02) && (idesc->endpoint[i].bEndpointAddress & 0x80))
        {
            ep_in = idesc->endpoint[i].bEndpointAddress;
            break;
        }
    }
    if(ep_in < 0)
    {
        qWarning()<<"Interface "<<iface<<" does not have a BULK OUT endpoint";
        if(ok) *ok=false;
        return QByteArray();
    }

    int claim_res = libusb_claim_interface(raw_handle, iface);
    if(claim_res != 0)
    {
        qWarning()<<"Cannot claim interface "<<iface<<": "<<claim_res<<" ("<<libusb_error_name(claim_res)<<") - will try anyway";
    }

    QByteArray storage(maxlen, char(0));
    int bytes_recved = 0;
    int res = libusb_bulk_transfer(raw_handle, (unsigned char)ep_in, reinterpret_cast<unsigned char*>(storage.data()), maxlen, &bytes_recved, timeout);

    if(claim_res == 0)
    {
        libusb_release_interface(raw_handle, iface);
    }

    if((res<0) || (bytes_recved<0))
    {
        qWarning()<<"BULK IO failure: "<<res<<" ("<<libusb_error_name(res)<<")";
        if(ok) *ok = false;
        return QByteArray();
    }

    storage.truncate(bytes_recved);
    if(ok) *ok = true;
    return storage;
#endif
}

int HIDProxyDesktop::periodic()
{
    int ret = hid_read(handle, hid_in_buffer, hid_report_size+1);
    if(ret<0)
    {
        qWarning()<<"HID read error("<<ret<<") "<<fromWideString(hid_error(handle));
        return -1;
    }

    return ret;
}

bool HIDProxyDesktop::writeReport(int size)
{
    /*on Windows - returns the size of the longest report*/
#ifndef OD_NO_DEVELOPER
    if(handle) {
        int ret = hid_write(handle, hid_out_buffer, size);
        if(ret < size) {
            qWarning()<<"hid_write of size "<<size<<" returned "<<ret;
            return false;
        }
        return true;
    } else {
        qWarning()<<"cannot hid_write - null handle";
        return false;
    }
#else
    return (handle && (hid_write(handle, hid_out_buffer, size)>=size));
#endif
}

bool HIDProxyDesktop::writeFeature(const uint8_t *data, int size)
{
    return isOpen() && (hid_send_feature_report(handle, data, size) == size);
}

#if !HIDPROXY_NO_LIBUSB

#include <QTextCodec>
static QString getDescriptorString(libusb_device_handle* hdev, int index, int* ok)
{
#ifdef Q_OS_WIN
    char buffer[255];
#else
    char buffer[256];
#endif

    int rval = libusb_get_descriptor(hdev, LIBUSB_DT_STRING, index, (unsigned char*)buffer, sizeof(buffer));

    if (rval<0) // error
    {
        qWarning()<<"getDescriptorString - cannot get descriptor at ind="<<index
                  <<" error "<<rval;
        if(ok) *ok = rval;
        return QString();
    }

    // rval should be bytes read, but buffer[0] contains the actual response size
    if ((unsigned char)buffer[0] < rval)
    {
        qWarning()<<"getDescriptorString: Bytes received ("<<rval
                  <<") > real size ("<<(unsigned char)buffer[0]<<")";
        rval = (unsigned char)buffer[0]; // string is shorter than bytes read
    }

    if(ok) *ok = 0;
    if(rval < 4)
    {
        //no data?
        return QString();
    }

    qDebug()<<"USB Descriptor String (hex): "<<QString::fromLatin1(QByteArray(buffer+2,rval-2).toHex()).toUpper();
    if(buffer[3] != 0)
    {
        qDebug()<<"Non Latin1 descriptor string found!";
        //not Latin1-String -> convert to Hex;
        QString ret;
        ret.reserve(rval-2);
        for(int i=2;i<rval;++i)
        {
            ret.append(QChar(buffer[i]));
        }
        return ret;
    }

    if (buffer[1] != LIBUSB_DT_STRING) // second byte is the data type
    {
        qCritical ()<<"getDescriptorString: invalid return type";
        if(ok) *ok = LIBUSB_ERROR_INVALID_PARAM;
        return QString();
    }

    // we're dealing with UTF-16LE here so actual chars is half of rval,
    // and index 0 doesn't count
    QTextCodec* codec = QTextCodec::codecForName ("UTF-16LE");
    QString output;
    if(!codec)
    {
        qCritical ()<<"Cannot get codec for UTF-16LE - using lossy conversion";
        rval /= 2;

        QByteArray choutput(rval, char(0));
        int i=1;
        for (; i < rval; i++)
        {
            if (buffer[2 * i + 1] == 0)
            {
                choutput[i - 1] = buffer[2 * i];
            }
            else
            {
                choutput[i - 1] = '?'; /* outside of ISO Latin1 range */
            }
        }

        choutput[i - 1] = 0;
        output = QString::fromLatin1(choutput.constData());
    }
    else
    {
        output = codec->toUnicode (buffer+2, rval-2);
    }

    return output;
}

bool HIDProxyDesktop::openRaw()
{
    qWarning()<<"Requested openRaw - handle is "<<raw_handle;
    //try interface 1
    wchar_t sn_number[128];
    hid_get_serial_number_string(handle, sn_number, 128);
    QString sn = fromWideString(sn_number);

    //VID - OD_VID, PID - OD_PID_DFU, SN - sn
    libusb_device **devs;
    int r;
    int cnt;

    cnt = libusb_get_device_list(HIDProxy::context(), &devs);
    if (cnt <= 0)
    {
        qWarning()<<"Cannot enumerate libusb devices - dev_list returned "<<cnt;
        return 0;
    }

    libusb_device *udev;
    int i = 0;

    int mok = false;
    while ((udev = devs[i++]) != nullptr)
    {
        struct libusb_device_descriptor desc;
        r = libusb_get_device_descriptor(udev, &desc);
        if (r < 0)
        {
            qWarning()<<"UsbOperator::open - failed to get device descriptor - skipping";
            continue;
        }
        if((d_vid == desc.idVendor) && (d_pid == desc.idProduct))
        {
            qWarning()<<"libusb: found raw device at "<<libusb_get_bus_number(udev)<<":"<<libusb_get_port_number(udev);
            if((r = libusb_open(udev, &raw_handle))<0)
            {
                qDebug()<<"Cannot open device: error "<<r;
                raw_handle = nullptr;
                continue;
            }
            QString tmp_sn = getDescriptorString(raw_handle, desc.iSerialNumber, &mok);
            if(mok)
            {
                qWarning()<<"libusb: cannot get serial number";
                closeRaw();
                continue;
            }
            if(tmp_sn != sn)
            {
                qWarning()<<"raw device found, but SN does not match ("<<sn<<" vs "<<tmp_sn<<")";
                closeRaw();
                continue;
            }

            if((r = libusb_get_config_descriptor(udev, 0, &raw_cfg))<0)
            {
                qWarning()<<"Error getting device config descriptor: "<<r;
                closeRaw();
                break;
            }
            break;
        }
    }
    libusb_free_device_list(devs, 1);

    if(!raw_handle)
    {
        qWarning()<<"Raw device init failed";
        return false;
    }

#if 0
    libusb_claim_interface(usb_handle,1);
#endif
    qWarning()<<"Raw handle opened";
    return true;
}

void HIDProxyDesktop::closeRaw()
{
    if(raw_handle)
    {
        if(raw_cfg)
        {
            libusb_free_config_descriptor(raw_cfg);
            raw_cfg = nullptr;
        }
        //libusb_lock_events(HIDProxy::context());
        qWarning()<<"Closing libusb handle in "<<QThread::currentThread();
        libusb_close(raw_handle);
        //libusb_unlock_events(HIDProxy::context());
        raw_handle = nullptr;
    }
}

#else /* no libusb */
bool HIDProxyDesktop::openRaw()
{
    return false;
}

void HIDProxyDesktop::closeRaw()
{

}
#endif
