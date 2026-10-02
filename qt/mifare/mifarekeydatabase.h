#ifndef MIFAREKEYDATABASE_H_
#define MIFAREKEYDATABASE_H_

#include "mifareblock.h"

#include <QObject>
#include <QSqlDatabase>
#include <QSet>

#define SETTINGS_KEYS "Mifare/Keys"

class MifareClassicKeyID
{
    public:
        MifareClassicKeyID() : uid(), sector(0) {}
        MifareClassicKeyID(const QByteArray& u, int s) : uid(u),sector(s){}
        QByteArray uid;
        quint8 sector;
};

inline uint qHash(const MifareClassicKeyID& id, uint seed=0)
{
    return qHash(id.uid,seed+id.sector);
}

inline bool operator==(const MifareClassicKeyID& u1, const MifareClassicKeyID& u2)
{
    return (u1.uid==u2.uid)&&(u1.sector==u2.sector);
}

class MifareKeyDatabase : public QObject
{
        Q_OBJECT
    public:
        static MifareKeyDatabase* instance();

        inline static QString uidText(const QByteArray& uid)
        {
            return QString::fromLatin1(uid.toHex().toUpper());
        }

        ~MifareKeyDatabase();

        MifareUltralightEV1Security ultralight(const QByteArray& uid, bool* ok = nullptr);
        MifareClassicKey classic(const QByteArray& uid, quint8 block, MifareClassicKeyType type, bool* ok = nullptr);
        MifarePlusKey plus(const QByteArray& uid, quint16 block, bool* ok = nullptr);
        void save(const QByteArray& uid, quint8 block, MifareClassicKeyType type, const MifareClassicKey& key);
        void save(const QByteArray& uid, quint16 block, const MifarePlusKey& key);
        void save(const QByteArray& uid, const MifareUltralightEV1Security& sec);

        void cleanDatabase();

        inline bool tryRead(const MifareClassicKeyID& uid) const
        {
            qDebug()<<"UID "<<uidText(uid.uid)<<" flagged: "<<uid_db.contains(uid);
            return !uid_db.contains(uid);
        }

        inline void failed(const MifareClassicKeyID& uid)
        {
            qDebug()<<"UID "<<uidText(uid.uid)<<" is flagged";
            uid_db.insert(uid);
        }

        inline void success(const MifareClassicKeyID& uid)
        {
            qDebug()<<"UID "<<uidText(uid.uid)<<" is unflagged";
            uid_db.remove(uid);
        }

    private:
        MifareKeyDatabase(QObject* parent = 0);
        Q_DISABLE_COPY(MifareKeyDatabase)

        bool database_opened;
        QSet<MifareClassicKeyID> uid_db;
};

#endif  // MIFAREKEYDATABASE_H_
