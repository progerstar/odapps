#include "hidproxy.h"

#include <QEventLoop>
#include <QDateTime>
#include <QDebug>
#include <QThread>

#if !HIDPROXY_NO_LIBUSB
#include <libusb.h>
static libusb_context* kHidProxyCTX;
#endif

#if defined(Q_OS_ANDROID)
#include <QtAndroid>
static QAndroidJniObject hidManager;
#endif

HIDProxy::HIDProxy(QObject* parent): QObject(parent),
    hid_report_id(-1), hid_report_size(63),
    hid_in_buffer(nullptr), hid_out_buffer(nullptr),
    queryInterval(-1), queryTimerID(-1)
{
#if defined(Q_OS_ANDROID)
    if(!hidManager.isValid())
    {
        qWarning()<<"(Re)creating HIDIOManager";
        hidManager = QAndroidJniObject("ru.opendev.qhidapi.HIDIOManager", "(Landroid/content/Context;)V", QtAndroid::androidContext().object());
    }
#endif
    initBuffers();

    reply_timer.setInterval(0);
    reply_timer.setSingleShot(true);
    connect(&reply_timer,SIGNAL(timeout()),this,SLOT(nack()));
}

HIDProxy::~HIDProxy()
{
    if(hid_in_buffer) {
        delete[] hid_in_buffer;
        hid_in_buffer = nullptr;
    }
    if(hid_out_buffer) {
        delete[] hid_out_buffer;
        hid_out_buffer = nullptr;
    }
}

void HIDProxy::qSleep(int ms)
{
    QTimer tim;
    tim.setSingleShot(true);
    tim.setInterval(ms);
    QEventLoop evt;
    QObject::connect(&tim, SIGNAL(timeout()), &evt, SLOT(quit()));
    tim.start();
    evt.exec();
}

void HIDProxy::deinit()
{
#if defined(Q_OS_ANDROID)
    if(hidManager.isValid()) {
        hidManager.callMethod<void>("stopReadingThread");
    }
#else
    hid_exit();
# if !HIDPROXY_NO_LIBUSB
    if(kHidProxyCTX) {
        qWarning()<<"Destroying libusb context in "<<QThread::currentThread();
        libusb_exit(kHidProxyCTX);
    }
# endif
#endif
}

#if defined(Q_OS_ANDROID)
QAndroidJniObject* HIDProxy::manager() const
{
    if(!hidManager.isValid())
    {
        qWarning()<<"(Re)creating HIDIOManager";
        hidManager = QAndroidJniObject("ru.opendev.qhidapi.HIDIOManager", "(Landroid/content/Context;)V", QtAndroid::androidContext().object());
    }
    //qWarning()<<"HIDManager requested - return "<<hidManager.object();
    return &hidManager;
}
#endif

#if !defined (Q_OS_ANDROID)
void HIDProxy::initContext()
{
    static int __init = 0;
    if(!__init)
    {
        hid_init();
#if !HIDPROXY_NO_LIBUSB
        qWarning()<<"Creating libusb context in "<<QThread::currentThread();
        libusb_init(&kHidProxyCTX);
#endif
        __init = 1;
    }
}
#endif

#if !HIDPROXY_NO_LIBUSB
libusb_context* HIDProxy::context() const
{
    return kHidProxyCTX;
}
#endif

void HIDProxy::setReplyTimeout(int msec)
{
    reply_timer.setInterval(msec);
}

void HIDProxy::setPollInterval(int msec)
{
    if(queryTimerID>=0) {
        killTimer(queryTimerID);
        queryTimerID = -1;
    }

    if(msec >= 0) {
#if HIDPROXY_ALLOW_FLOOD
        queryInterval = msec;
#else
        queryInterval = qMax<int>(5, msec);
#endif
    } else {
        queryInterval = -1;
    }

    if((queryInterval >= 0) && isOpen())
    {
        queryTimerID = startTimer(queryInterval);
    }
}

bool HIDProxy::sendPacket(const QByteArray& data, int rID)
{
#if HIDPROXY_DUMP_OUT
    qWarning()<<device()<<" HID report (OUT,@"<<rID<<"): "<<data.toHex().toUpper();
#endif
    return sendPacket(data.constData(), data.size(), rID);
}

bool HIDProxy::sendPacket(const void* data, size_t length, int rID)
{
    if(!hid_out_buffer || hid_report_size <= 0
            || length > size_t(hid_report_size) || (length && !data))
    {
        return false;
    }

    if(reply_timer.interval())
    {
        reply_timer.stop();
        reply_timer.start();
    }

    memset(hid_out_buffer, 0, size_t(hid_report_size + 1));
    int offset = 0;
    if(rID >= 0)
    {
        ++offset;
        hid_out_buffer[0] = rID;
    }
    else if((rID != ReportId_None) && (hid_report_id>=0))
    {
        ++offset;
        hid_out_buffer[0] = hid_report_id;
    }

    if(length)
    {
        memcpy(hid_out_buffer + offset, data, length);
    }

    return writeReport(hid_report_size+offset);
}

bool HIDProxy::setFeature(const QByteArray& data, int rID)
{
    if(data.size() > hid_report_size) {
        return false;
    }

    QByteArray featureReport(hid_report_size + 1, char(0));
    uint8_t* feature_report = reinterpret_cast<uint8_t*>(featureReport.data());
    /* ReportID is mandatory (0 for no report ids) */
    if(rID>=0)
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
    memcpy(feature_report+1, data.constData(), data.size());

    return writeFeature(feature_report, data.size() + 1);
}

int HIDProxy::waitForReadyRead(int timeout)
{
    int res = -1;
    QEventLoop waiter;
    QTimer abortTimer;
    QTimer pollTimer;
    pollTimer.setInterval(0);
    pollTimer.setSingleShot(true);
    connect(&pollTimer, &QTimer::timeout, this, [&pollTimer, this, &res, &waiter]() {
        res = this->periodic();
        if(res < 0)
        {
            close();
            emit disconnected();
            waiter.exit(1);
        }
        else if(res)
        {
            waiter.exit(1);
        }
        else
        {
            pollTimer.start();
        }
    });
    abortTimer.setInterval(timeout);
    abortTimer.setSingleShot(true);
    QObject::connect(&abortTimer, SIGNAL(timeout()), &waiter, SLOT(quit()));
    abortTimer.start();
    waiter.exec();
    pollTimer.stop();
    abortTimer.stop();
    return ((res>0) && (hid_report_id>=0)) ? (res-1) : res;
}

QByteArray HIDProxy::getData(int length)
{
#if 0
#error "WARNING: do not use fromRawData - does not make a deep copy => undefined behavior"
    return hid_in_buffer ?
                QByteArray::fromRawData((const char*)hid_in_buffer + ((hid_report_id<0)?0:1), length) :
                QByteArray();
#else
    return hid_in_buffer ? QByteArray((const char*)hid_in_buffer + ((hid_report_id<0)?0:1), length) : QByteArray();
#endif
}

void HIDProxy::queueReport(const QByteArray& report, int rID)
{
    if(!reply_timer.isActive() && pending_op.isEmpty())
    {
        (void)sendPacket(report, rID);
    }
    else if(report.size())
    {
        if(rID >= 0)
        {
            quint8 rrid(rID);
            pending_op.append(QByteArray((const char*)&rrid, 1) + report);
        }
        else if((rID != ReportId_None) && (hid_report_id>=0))
        {
            quint8 rrid(hid_report_id);
            pending_op.append(QByteArray((const char*)&rrid, 1) + report);
        }
        else
        {
            pending_op.append(report);
        }
    }
}

void HIDProxy::timerEvent(QTimerEvent *event)
{
    Q_UNUSED(event)

    if(!isOpen())
    {
        pending_op.clear();
        qWarning()<<"Device is closed - stop hidQuery";
        killTimer(queryTimerID);
        queryTimerID = -1;
        return;
    }

    processPeriodic(periodic());
}

void HIDProxy::processPeriodic(int ret)
{
    //qWarning()<<"read HID report of size "<<ret<<" - starts with "<<int(hid_in_buffer[0])<<", "<<int(hid_in_buffer[1]);
    if((ret > 0) && ((hid_report_id == -1) || (hid_in_buffer[0] == hid_report_id)))
    {
#if HIDPROXY_DUMP_IN
        qWarning()<<device()<<" HID report (IN): "<<QByteArray::fromRawData((const char*)hid_in_buffer, ret).toHex().toUpper();
#endif
        if(reply_timer.interval())
        {
            reply_timer.stop();
        }
#if 0
#error "Do NOT use fromRawData -> does not make a deep copy => undefined behavior"
        emit processInRequest(QByteArray::fromRawData((const char*)hid_in_buffer, ret));
#else
        emit processInRequest(QByteArray((const char*)hid_in_buffer, ret));
#endif
    }

    if(ret<0)
    {
        qWarning()<<"Device read error "<<ret;
        close();
        emit disconnected();
    }

    if(!reply_timer.isActive() && !pending_op.isEmpty())
    {
        QByteArray next_report = pending_op.dequeue();
        qWarning()<<"Send sequence report";
        sendPacket(next_report);
    }
}

void HIDProxy::nack()
{
    qWarning()<<"Device is not responding";
    close();
    emit disconnected();
    emit deviceChanged(QString());
}

void HIDProxy::initBuffers()
{
    if(hid_in_buffer) {
        delete[] hid_in_buffer;
        hid_in_buffer = nullptr;
    }
    if(hid_out_buffer) {
        delete[] hid_out_buffer;
        hid_out_buffer = nullptr;
    }

    hid_in_buffer = new unsigned char[hid_report_size + 1]();
    hid_out_buffer = new unsigned char[hid_report_size + 1]();
}
