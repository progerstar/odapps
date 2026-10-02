#ifndef HIDPROXY_DESKTOP_H
#define HIDPROXY_DESKTOP_H

#include <hidproxy.h>

class HIDProxyDesktop : public HIDProxy
{
        Q_OBJECT
    public:
        explicit HIDProxyDesktop(QObject* parent = nullptr);
        virtual ~HIDProxyDesktop();

        virtual QStringList availableHidDevices(int vid, int pid, const QString& product = QString()) const override;
        virtual QStringList availableHidDevices(const QString& manufacturer, const QString& product = QString()) const override;
        virtual QList<HIDProxy::Descriptor> enumerateHidDevices(int vid, int pid, const QString& product = QString()) const override {
            return HIDProxyDesktop::availableHidDevicesList(vid, pid, product);
        }

        static QList<HIDProxy::Descriptor> availableHidDevicesList(int vid, int pid, const QString& product = QString());

        virtual QString device() const Q_DECL_OVERRIDE;
        virtual bool setDevice(const QString& name) Q_DECL_OVERRIDE;

        /*Device info*/
        virtual ClassDescriptor deviceClass() Q_DECL_OVERRIDE;
        virtual int interfaceCount() Q_DECL_OVERRIDE;
        virtual ClassDescriptor interfaceClass(int iface) Q_DECL_OVERRIDE;
        virtual int endpointCount(int iface) Q_DECL_OVERRIDE;

        virtual bool isOpen() const Q_DECL_OVERRIDE;
        virtual bool hasPermission() const Q_DECL_OVERRIDE
        {
            /* stub */
            return true;
        }

        virtual void close() Q_DECL_OVERRIDE;

        virtual int getQuirks() const override {
            return _quirks;
        }

        virtual void setQuirks(int v) override {
            _quirks = v;
        }

        virtual bool getFeature(int sz, QByteArray& output, int rID=-1) Q_DECL_OVERRIDE;

        /* BULK interfaces */
        ///timeout: in MS
        virtual bool rawSend(int iface, const QByteArray& data, int timeout = 1000) Q_DECL_OVERRIDE;
        virtual QByteArray rawReceive(int iface, int maxlen, int timeout = 1000, bool* ok = nullptr) Q_DECL_OVERRIDE;
    protected:
        virtual int periodic() Q_DECL_OVERRIDE;
        virtual bool writeReport(int size) Q_DECL_OVERRIDE;
        virtual bool writeFeature(const uint8_t *data, int size) Q_DECL_OVERRIDE;
    private:
        hid_device* handle;
        int _quirks;
#if !HIDPROXY_NO_LIBUSB
        libusb_device_handle *raw_handle;
        libusb_config_descriptor* raw_cfg;
#endif
        bool openRaw();
        void closeRaw();
        quint16 d_vid, d_pid;
};

#endif // HIDPROXY_DESKTOP_H
