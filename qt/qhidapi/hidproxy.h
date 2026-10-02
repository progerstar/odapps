#ifndef HIDPROXY_H
#define HIDPROXY_H

#include <QObject>
#include <QTimer>

#include <QTimerEvent>
#include <QQueue>

#if defined(Q_OS_ANDROID)

#include <QAndroidJniObject>

#else

#include <hidapi.h>
#if !HIDPROXY_NO_LIBUSB
#include <libusb.h>
#endif

#endif

class HIDProxy : public QObject
{
        Q_OBJECT
        Q_PROPERTY(QString device READ device WRITE setDevice NOTIFY deviceChanged)
        Q_PROPERTY(int reportID READ reportID WRITE setReportID)
        Q_PROPERTY(int reportSize READ reportSize WRITE setReportSize)
        Q_PROPERTY(int replyTimeout READ replyTimeout WRITE setReplyTimeout)
        Q_PROPERTY(int pollInterval READ pollInterval WRITE setPollInterval)
        Q_PROPERTY(int quirks READ getQuirks WRITE setQuirks)
    public:
        struct Descriptor {
            int vid = 0;
            int pid = 0;
            QString manufacturer;
            QString product;
            QString serial;
            quint16 usagePage = 0;
            quint16 usage = 0;
            int interfaceNumber = -1;

            QString toString() const {
                QString value = QString("%1:%2:").arg(vid, 4, 16, QLatin1Char('0'))
                        .arg(pid, 4, 16, QLatin1Char('0')) + (!serial.isEmpty() ? serial : QString("S/N"));
                if(interfaceNumber >= 0) {
                    value += QStringLiteral("#IF=") + QString::number(interfaceNumber);
                } else if(usagePage) {
                    value += QStringLiteral("#UP=") + QString::number(usagePage, 16);
                }
                return value;
            }
        };

        enum Quirk {
            QUIRK_NONE = 0x00,
            QUIRK_INPUT_VS_FEATURE = 0x01,
        };
        Q_ENUM(Quirk)
        Q_DECLARE_FLAGS(Quirks, Quirk)

        enum ReportIds {
            ReportId_Default = -1,
            ReportId_None = -2,
        };

        struct ClassDescriptor {
            quint8 bClass;
            quint8 bSubClass;
            quint8 bProtocol;
        };

        explicit HIDProxy(QObject* parent = nullptr);
        virtual ~HIDProxy();
        static void qSleep(int msec);
        static void deinit();
#if defined (Q_OS_ANDROID)
        QAndroidJniObject* manager() const;
#endif
#if !defined (Q_OS_ANDROID)
        static void initContext();
#endif
#if !HIDPROXY_NO_LIBUSB
        libusb_context* context() const;
#endif

        /* Enumeration */
        virtual QStringList availableHidDevices(int vid, int pid, const QString& product = QString()) const = 0;
        virtual QStringList availableHidDevices(const QString& manufacturer, const QString& product = QString()) const = 0;
        virtual QList<HIDProxy::Descriptor> enumerateHidDevices(int vid, int pid, const QString& product = QString()) const = 0;

        virtual QString device() const = 0;
        virtual bool setDevice(const QString& name) = 0;

        /*Device info*/
        virtual ClassDescriptor deviceClass() = 0;
        virtual int interfaceCount() = 0;
        virtual ClassDescriptor interfaceClass(int iface) = 0;
        virtual int endpointCount(int iface) = 0;

        virtual bool isOpen() const = 0;
        virtual bool hasPermission() const = 0;

        virtual void close() = 0;

        bool waitReply() const
        {
            return reply_timer.isActive();
        }

        inline void cancelReply()
        {
            reply_timer.stop();
        }

        inline int replyTimeout() const
        {
            return reply_timer.interval();
        }

        void setReplyTimeout(int msec);

        inline int pollInterval() const
        {
            return queryInterval;
        }

        void setPollInterval(int msec);

        virtual int getQuirks() const = 0;
        virtual void setQuirks(int v) = 0;

        virtual bool sendPacket(const QByteArray& data, int rID=-1);
        virtual bool sendPacket(const void* data, size_t length, int rID=-1);
        virtual bool setFeature(const QByteArray& data, int rID=-1);
        virtual bool getFeature(int sz, QByteArray& output, int rID=-1) = 0;

        virtual int waitForReadyRead(int timeout);
        virtual QByteArray getData(int length);

        /* BULK interfaces */
        virtual bool rawSend(int iface, const QByteArray& data, int timeout = 1000) = 0;
        virtual QByteArray rawReceive(int iface, int maxlen, int timeout = 1000, bool* ok = nullptr) = 0;

        virtual void queueReport(const QByteArray& report, int rID=-1);

        inline int reportID() const
        {
            return hid_report_id;
        }
        void setReportID(int id)
        {
            hid_report_id = id;
        }

        inline int reportSize() const
        {
            return hid_report_size;
        }

        inline void setReportSize(int sz)
        {
            hid_report_size = sz;
            initBuffers();
        }
    signals:
        void processInRequest(const QByteArray& data);
        void deviceChanged(const QString& name);
        void disconnected();
    protected:
        int hid_report_id;
        int hid_report_size;
        //unsigned char hid_report_in[64];
        //unsigned char hid_report_out[64];
        unsigned char* hid_in_buffer;
        unsigned char* hid_out_buffer;
        QTimer reply_timer;

        virtual void timerEvent(QTimerEvent *event) Q_DECL_OVERRIDE;
        virtual int periodic() = 0;
        virtual bool writeReport(int size) = 0;
        virtual bool writeFeature(const uint8_t* data, int size/*incl ReportID*/) = 0;
    protected slots:
        virtual void nack();
    protected:
        int queryInterval;
        int queryTimerID;
        QQueue<QByteArray> pending_op;
    private:
        void processPeriodic(int ret);
        void initBuffers();
};

#endif // HIDPROXY_H
