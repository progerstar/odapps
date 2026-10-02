#include "hidproxy_android.h"
#include "androidjnihelper.h"
#include <endianrw.h>

#include <QDebug>
#include <QDateTime>
#include <QEventLoop>

#include <QtAndroid>

HIDProxyAndroid::HIDProxyAndroid(QObject* parent): HIDProxy(parent),
    handle("")
{
    HIDProxy::manager()->callMethod<void>("startReadingThread");
}

HIDProxyAndroid::~HIDProxyAndroid()
{
    if(!handle.isEmpty())
    {
        QAndroidJniEnvironment env;
        QAndroidJniObject hndObj = QAndroidJniObject::fromString(handle);
        HIDProxy::manager()->callMethod<void>("close", "(Ljava/lang/String;)V", hndObj.object<jstring>());
        ANDROID_CLEAR_EXCEPTION(env, "Exception while closing android USB device");
        handle.clear();
    }
}

QStringList HIDProxyAndroid::availableHidDevices(const QString& manufacturer, const QString& product)
{
    qWarning()<<"Requested enumerate USB "<<manufacturer<<"/"<<product;
    QAndroidJniObject vndObj = QAndroidJniObject::fromString(manufacturer);
    QAndroidJniObject prodObj = QAndroidJniObject::fromString(product);

    QAndroidJniEnvironment env;
    bool success;
    QStringList devs;
    QAndroidJniObject* mgr = HIDProxy::manager();
    ANDROID_JNI_SAFE_GET_STRINGLIST(env, devs, success, *mgr, "deviceList",
                                    "(Ljava/lang/String;Ljava/lang/String;)[Ljava/lang/String;",
                                    vndObj.object<jstring>(), prodObj.object<jstring>());
    if(!success)
    {
        qWarning()<<"HID enumeration failed";
    }
    else
    {
        qWarning()<<"Found HID devices: "<<devs;
    }
    return devs;
}

QStringList HIDProxyAndroid::availableHidDevices(int vid, int pid, const QString& product)
{
    QAndroidJniEnvironment env;
    bool success;
    QStringList devs;
    QAndroidJniObject* mgr = HIDProxy::manager();
    QAndroidJniObject prodObj = QAndroidJniObject::fromString(product);
    ANDROID_JNI_SAFE_GET_STRINGLIST(env, devs, success, *mgr, "deviceList",
                                    "(IILjava/lang/String;)[Ljava/lang/String;", vid, pid,
                                    prodObj.object<jstring>());
    if(!success)
    {
        qWarning()<<"HID enumeration failed";
    }
    else
    {
        qWarning()<<"Found HID devices: "<<devs;
    }
    return devs;
}

QList<HIDProxy::Descriptor> HIDProxyAndroid::enumerateHidDevices(int vid, int pid, const QString& product) const
{
    QList<HIDProxy::Descriptor> devEnum;
    QAndroidJniEnvironment env;
    bool success;
    QStringList devs;
    QAndroidJniObject* mgr = HIDProxy::manager();
    QAndroidJniObject prodObj = QAndroidJniObject::fromString(product);
    ANDROID_JNI_SAFE_GET_STRINGLIST(env, devs, success, *mgr, "deviceInfoList",
                                    "(IILjava/lang/String;)[Ljava/lang/String;", vid, pid,
                                    prodObj.object<jstring>());
    if(!success) {
        qWarning()<<"HID enumeration failed";
    } else {
        qWarning()<<"Found HID devices: "<<devs;
        for(const QString& dev : qAsConst(devs)) {
            QStringList parts = dev.split("/");
            if(parts.size() == 5) {
                Descriptor desc;
                desc.vid = parts.at(0).toUInt(nullptr, 16);
                desc.pid = parts.at(1).toUInt(nullptr, 16);
                desc.manufacturer = parts.at(2);
                desc.product = parts.at(3);
                desc.serial = parts.at(4);
                devEnum.append(desc);
            }
        }
    }
    return devEnum;
}

QString HIDProxyAndroid::device() const
{
    return handle;
}

bool HIDProxyAndroid::setDevice(const QString& name)
{
    if(isOpen())
    {
        close();
    }

    QAndroidJniEnvironment env;
    QAndroidJniObject snJString = QAndroidJniObject::fromString(name);
    jboolean result = HIDProxy::manager()->callMethod<jboolean>("open", "(Ljava/lang/String;)Z", snJString.object<jstring>());
    if(env->ExceptionCheck())
    {
        qWarning()<<"Cannot open HID device - java exception occured";
        env->ExceptionClear();
        return false;
    }
    if(!result)
    {
        qWarning()<<"Cannot open HID device "<<name;
        return false;
    }

    qWarning()<<"Device "<<name<<" opened";
    handle = name;
    int ifaces = interfaceCount();
    if(!ifaces)
    {
        qWarning()<<"No interfaces or retreival error";
        close();
        return false;
    }
    qWarning()<<"has "<<ifaces<<" interfaces";

    hid_iface = -1;
    int fallbackHidInterface = -1;
    /* Prefer a vendor HID interface. Composite readers also expose a boot
     * keyboard HID interface, which must not receive binary protocol reports. */
    for(int i=0;i<ifaces;++i) {
        const ClassDescriptor descriptor = interfaceClass(i);
        if(descriptor.bClass == 0x03) {
            if(fallbackHidInterface < 0)
                fallbackHidInterface = i;
            if((descriptor.bSubClass == 0x00) && (descriptor.bProtocol == 0x00)) {
                hid_iface = i;
                break;
            }
        }
    }
    if(hid_iface < 0)
        hid_iface = fallbackHidInterface;

    if(hid_iface >= 0) {
        const int i = hid_iface;
        QAndroidJniObject* hnd = getDeviceHandle();
        if(!hnd)
        {
            close();
            return false;
        }

        qWarning()<<"Device hid interface number is "<<i;
        {
            bool success;
            QList<bool> epDirections;
            ANDROID_JNI_SAFE_GET_ARRAY(jboolean, Boolean, bool, env, epDirections, success, *hnd, "interfaceEpDirections", "(I)[Z", i);
            if(!success || epDirections.size() != 2)
            {
                delete hnd;
                qWarning()<<"EP enumeration failed or invalid interface (HID iface is expected to have exactly 2 enpoints - one OUT and one IN)";
                close();
                return false;
            }
            ep_in = (epDirections[0]==true) ? 1 : 0;
            ep_out = (epDirections[0]==true) ? 0 : 1;
        }

        bool claimed;
        ANDROID_JNI_SAFE_GET(jboolean, bool, false, claimed, env, *hnd, "claimInterface", "(I)Z", i);
        delete hnd;

        if(!claimed)
        {
            qWarning()<<"Cannot claim HID interface";
            close();
            return false;
        }
    }
    if(hid_iface<0)
    {
        qWarning()<<"Device does not have a HID interface or an enumeration error has occured";
        close();
        return false;
    }

    if(queryInterval >= 0)
    {
        queryTimerID = startTimer(queryInterval);
    }
    emit deviceChanged(name);
    return true;
}

HIDProxy::ClassDescriptor HIDProxyAndroid::deviceClass()
{
    HIDProxy::ClassDescriptor ret = {0, 0, 0};
    QAndroidJniObject* hnd = getDeviceHandle();
    if(hnd)
    {
        QAndroidJniEnvironment env;
        bool ok;
        QList<int> dcl;
        ANDROID_JNI_SAFE_GET_ARRAY(jint, Int, int, env, dcl, ok, *hnd, "deviceClass", "()[I");
        delete hnd;

        if(ok && dcl.size() == 3)
        {
            return {quint8(dcl.at(0)), quint8(dcl.at(1)), quint8(dcl.at(2))};
        }
        qWarning()<<"Error getting device class";
    }
    return ret;
}

int HIDProxyAndroid::interfaceCount()
{
    QAndroidJniObject* hnd = getDeviceHandle();
    if(hnd)
    {
        qWarning()<<"Querying interface count for "<<handle;
        QAndroidJniEnvironment env;
        int icnt;
        ANDROID_JNI_SAFE_GET(jint, int, 0, icnt, env, *hnd, "interfaceCount", "()I");
        delete hnd;
        return icnt;
    }
    return 0;
}

HIDProxy::ClassDescriptor HIDProxyAndroid::interfaceClass(int iface)
{
    HIDProxy::ClassDescriptor ret = {0, 0, 0};
    QAndroidJniObject* hnd = getDeviceHandle();
    if(hnd)
    {
        QAndroidJniEnvironment env;
        bool ok;
        QList<int> icl;
        qWarning()<<"Getting interface "<<iface<<" class";
        ANDROID_JNI_SAFE_GET_ARRAY(jint, Int, int, env, icl, ok, *hnd, "interfaceClass", "(I)[I", iface);
        delete hnd;

        if(ok && icl.size() == 3)
        {
            qWarning()<<" -> "<<icl;
            return {quint8(icl.at(0)), quint8(icl.at(1)), quint8(icl.at(2))};
        }
        qWarning()<<"Error getting interface "<<iface<<" class";
    }
    return ret;
}

int HIDProxyAndroid::endpointCount(int iface)
{
    QAndroidJniObject* hnd = getDeviceHandle();
    if(hnd)
    {
        QAndroidJniEnvironment env;
        int epcnt;
        ANDROID_JNI_SAFE_GET(jint, int, 0, epcnt, env, *hnd, "interfaceEpCount", "(I)I", iface);
        delete hnd;
        return epcnt;
    }
    return 0;
}

bool HIDProxyAndroid::isOpen() const
{
    QAndroidJniEnvironment env;
    QAndroidJniObject snJString = QAndroidJniObject::fromString(handle);
    bool ok;
    ANDROID_JNI_SAFE_GET(jboolean, bool, false, ok, env, *(HIDProxy::manager()), "isOpen", "(Ljava/lang/String;)Z", snJString.object<jstring>());
    return ok;
}

bool HIDProxyAndroid::hasPermission() const
{
    QAndroidJniEnvironment env;
    QAndroidJniObject snJString = QAndroidJniObject::fromString(handle);
    bool ok;
    ANDROID_JNI_SAFE_GET(jboolean, bool, false, ok, env, *(HIDProxy::manager()), "hasPermission", "(Ljava/lang/String;)Z", snJString.object<jstring>());
    return ok;
}

void HIDProxyAndroid::close()
{
    pending_op.clear();
    if(queryTimerID>=0) {
        killTimer(queryTimerID);
        queryTimerID=-1;
    }

    if(!handle.isEmpty()) {
        qWarning()<<"HIDProxy::close "<<handle;
        QAndroidJniEnvironment env;
        QAndroidJniObject snJString = QAndroidJniObject::fromString(handle);
        ANDROID_JNI_SAFE_EXECUTE(env, *(HIDProxy::manager()), "close", "(Ljava/lang/String;)V", snJString.object<jstring>());
        handle.clear();
        emit deviceChanged(QString());
    }
}

bool HIDProxyAndroid::getFeature(int sz, QByteArray& output, int rID)
{
    output.clear();
    QAndroidJniObject* hnd = getDeviceHandle();
    if(!hnd)
    {
        return false;
    }

    QAndroidJniEnvironment env;
    bool ok;
    ANDROID_JNI_SAFE_GET_BYTES(env, output, ok, *hnd, "hidGetFeature", "(IIII)[B",
                               hid_iface, ((rID>=0)?rID:((hid_report_id>=0?hid_report_id:0))), sz+1, 500);
    delete hnd;

    if(!ok)
    {
        return false;
    }
    if(output.size())
    {
        //the first byte is the report ID;
        output.remove(0,1);
    }
    return true;
}

bool HIDProxyAndroid::rawSend(int iface, const QByteArray& data, int timeout)
{
    QAndroidJniObject* hnd = getDeviceHandle();
    if(!hnd)
    {
        return false;
    }

    QAndroidJniEnvironment env;
    int bep_out;
    ANDROID_JNI_SAFE_GET(jint, int, -1, bep_out, env, *hnd, "findEndpoint", "(IIZ)I", iface, 0x02, true);
    if(bep_out < 0)
    {
        qWarning()<<"Interface "<<iface<<" does not have a BULK OUT endpoint";
        delete hnd;
        return false;
    }

    bool claimed;
    ANDROID_JNI_SAFE_GET(jboolean, bool, false, claimed, env, *hnd, "claimInterface", "(I)Z", iface);
    if(!claimed)
    {
        qWarning()<<"Interface "<<iface<<" could not be claimed";
        delete hnd;
        return false;
    }

    jbyteArray array = env->NewByteArray(data.size());
    env->SetByteArrayRegion(array, 0, data.size(), (const jbyte*)data.constData());
    bool res = (bool)hnd->callMethod<jboolean>("write", "(II[BI)Z", bep_out, iface, array, timeout);
    delete hnd;

    if(env->ExceptionCheck())
    {
        qWarning()<<"JNI exception writing to bulk endpoint";
        res = false;
        env->ExceptionClear();
    }
    env->DeleteLocalRef(array);
    return res;
}

static inline bool _hid_read(QAndroidJniObject* hnd, int ep, int iface, int maxlen, int timeout)
{
    QAndroidJniEnvironment env;
    bool ok;
    ANDROID_JNI_SAFE_GET(jboolean, bool, false, ok, env, *hnd, "read", "(IIII)Z",
                         ep, iface, maxlen, timeout);
    return ok;
}

static inline bool __hid_dataPending(QAndroidJniObject* hnd)
{
    QAndroidJniEnvironment env;
    bool ok;
    ANDROID_JNI_SAFE_GET(jboolean, bool, false, ok, env, *hnd, "dataPending", "(V)Z");
    return ok;
}

QByteArray HIDProxyAndroid::rawReceive(int iface, int maxlen, int timeout, bool* ok)
{
    if(ok) *ok=false;
    QAndroidJniObject* hnd = getDeviceHandle();
    if(!hnd)
    {
        return QByteArray();
    }

    QAndroidJniEnvironment env;
    int bep_in;
    ANDROID_JNI_SAFE_GET(jint, int, -1, bep_in, env, *hnd, "findEndpoint", "(IIZ)I", iface, 0x02, false);
    if(bep_in < 0)
    {
        qWarning()<<"Interface "<<iface<<" does not have a BULK IN endpoint";
        delete hnd;
        return QByteArray();
    }

    bool claimed;
    ANDROID_JNI_SAFE_GET(jboolean, bool, false, claimed, env, *hnd, "claimInterface", "(I)Z", iface);
    if(!claimed)
    {
        qWarning()<<"Interface "<<iface<<" could not be claimed";
        delete hnd;
        return QByteArray();
    }

    if(!_hid_read(hnd, bep_in, iface, maxlen, timeout))
    {
        delete hnd;
        return QByteArray();
    }

    if(ok) *ok = true;
    if(!__hid_dataPending(hnd))
    {
        delete hnd;
        return QByteArray();
    }

    bool read_success;
    QByteArray read_data;
    ANDROID_JNI_SAFE_GET_BYTES(env, read_data, read_success, *hnd, "getQueuedData", "()[B");
    delete hnd;

    if(read_success)
    {
        if(ok) *ok=true;
        read_data.truncate(maxlen);
        return read_data;
    }
    if(ok) *ok = false;
    return QByteArray();
}

int HIDProxyAndroid::periodic()
{
    int ret = 0;

    QAndroidJniObject* hnd = getDeviceHandle();
    if(!hnd)
    {
        return -1;
    }

    if(!__hid_dataPending(hnd))
    {
        if(!_hid_read(hnd, ep_in, hid_iface, reportMaxSize, 10))
        {
            qWarning()<<"Cannot read from device";
            delete hnd;
            return -1;
        }
        if(!__hid_dataPending(hnd))
        {
            delete  hnd;
            return 0;
        }
    }

    QAndroidJniEnvironment env;
    QByteArray in_report;
    bool read_success;
    ANDROID_JNI_SAFE_GET_BYTES(env, in_report, read_success, *hnd, "getQueuedData", "()[B");
    delete hnd;

    if(!read_success)
    {
        qWarning()<<"HID poll error";
        return -1;
    }
    if(in_report.size())
    {
        memcpy(hid_in_buffer, in_report.constData(), in_report.size());
    }
    return in_report.size();
}

bool HIDProxyAndroid::writeReport(int size)
{
    QAndroidJniObject* hnd = getDeviceHandle();
    if(!hnd)
    {
        return false;
    }

    QAndroidJniEnvironment env;
    jbyteArray array = env->NewByteArray(size);
    env->SetByteArrayRegion(array, 0, size, reinterpret_cast<jbyte*>(hid_out_buffer));

    /* Investigate this - android may be pretty slow! */
    bool res = hnd->callMethod<jboolean>("write", "(II[BI)Z", ep_out, hid_iface, array, 200);
    delete hnd;

    if(env->ExceptionCheck())
    {
        qWarning()<<"JNI exception during writeReport";
        res = false;
        env->ExceptionClear();
    }
    env->DeleteLocalRef(array);
    return res;
}

bool HIDProxyAndroid::writeFeature(const uint8_t *data, int size)
{
    QAndroidJniObject* hnd = getDeviceHandle();
    if(!hnd)
    {
        return false;
    }

    QAndroidJniEnvironment env;
    jbyteArray array = env->NewByteArray(size-1);
    env->SetByteArrayRegion(array, 0, size-1, (const jbyte*)(data+1));
    bool res = hnd->callMethod<jboolean>("hidSetFeature", "(II[BII)Z", hid_iface, int(data[0]), array, 500);
    delete hnd;

    if(env->ExceptionCheck())
    {
        qWarning()<<"JNI exception during writeFeature";
        res = false;
        env->ExceptionClear();
    }
    env->DeleteLocalRef(array);
    return res;
}

QAndroidJniObject* HIDProxyAndroid::getDeviceHandle() const
{
    if(isOpen())
    {
        int backoff = 100;
        while(backoff-- && !hasPermission())
        {
            qWarning()<<"Waiting for permission";
            qSleep(100);
        }
        if(backoff>0)
        {
            QAndroidJniEnvironment env;
            QAndroidJniObject descrObj = QAndroidJniObject::fromString(handle);

            QAndroidJniObject obj = HIDProxy::manager()->callObjectMethod("handle", "(Ljava/lang/String;)Lru/opendev/qhidapi/HIDIODevice;", descrObj.object<jstring>());
            if(env->ExceptionCheck())
            {
                qWarning()<<"Cannot get USB handle - exception";
                env->ExceptionClear();
                return nullptr;
            }
            if(obj.isValid())
            {
                qWarning()<<"Obtained valid usb device handle";
                return new QAndroidJniObject(obj.object());
            }
        }
    }
    qWarning()<<"Cannot get USB handle - failed / not opened";
    return nullptr;
}
