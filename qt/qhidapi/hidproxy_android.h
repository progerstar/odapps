#ifndef HIDPROXY_ANDROID_H
#define HIDPROXY_ANDROID_H

#include <hidproxy.h>

#include <QAndroidJniEnvironment>
#include <QAndroidJniObject>

class HIDProxyAndroid : public HIDProxy
{
        Q_OBJECT
    public:
        explicit HIDProxyAndroid(QObject* parent = nullptr);
        virtual ~HIDProxyAndroid();

        virtual QStringList availableHidDevices(int vid, int pid, const QString& product = QString()) const override;
        virtual QStringList availableHidDevices(const QString& manufacturer, const QString& product = QString()) const override;
        virtual QList<HIDProxy::Descriptor> enumerateHidDevices(int vid, int pid, const QString& product = QString()) const override;

        virtual QString device() const Q_DECL_OVERRIDE;
        virtual bool setDevice(const QString& name) Q_DECL_OVERRIDE;

        /*Device info*/
        virtual ClassDescriptor deviceClass() Q_DECL_OVERRIDE;
        virtual int interfaceCount() Q_DECL_OVERRIDE;
        virtual ClassDescriptor interfaceClass(int iface) Q_DECL_OVERRIDE;
        virtual int endpointCount(int iface) Q_DECL_OVERRIDE;

        virtual bool isOpen() const Q_DECL_OVERRIDE;
        virtual bool hasPermission() const Q_DECL_OVERRIDE;

        virtual void close() Q_DECL_OVERRIDE;

        virtual int getQuirks() const override {
            return 0;
        }
        virtual void setQuirks(int v) override {
            (void)v;
        }

        virtual bool getFeature(int sz, QByteArray& output, int rID=-1) Q_DECL_OVERRIDE;

        /* BULK interfaces */
        virtual bool rawSend(int iface, const QByteArray& data, int timeout = 1000) Q_DECL_OVERRIDE;
        virtual QByteArray rawReceive(int iface, int maxlen, int timeout = 1000, bool* ok = nullptr) Q_DECL_OVERRIDE;
    protected:
        virtual int periodic() Q_DECL_OVERRIDE;
        virtual bool writeReport(int size) Q_DECL_OVERRIDE;
        virtual bool writeFeature(const uint8_t *data, int size) Q_DECL_OVERRIDE;
    private:
        QString handle;
        int hid_iface;
        int ep_in, ep_out;

        QAndroidJniObject* getDeviceHandle() const;
};

#endif // HIDPROXY_ANDROID_H
