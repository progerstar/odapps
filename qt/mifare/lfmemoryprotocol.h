#ifndef LFMEMORYPROTOCOL_H
#define LFMEMORYPROTOCOL_H

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

struct LFMemoryClassification
{
    QString status;
    QString airProtocol;
    QString chipType;
    QString evidence;
    QByteArray chipInfo;

    bool hasMemoryChip() const;
};

struct LFMemoryReadResult
{
    LFMemoryReadResult();

    bool success;
    QString error;
    LFMemoryClassification classification;
    QVector<QByteArray> blocks;
    QStringList blockErrors;
    QStringList blockLabels;

    int readBlockCount() const;
};

Q_DECLARE_METATYPE(LFMemoryReadResult)

class LFMemoryProtocol
{
    public:
        static bool parseClassification(const QList<QByteArray>& lines,
                                        LFMemoryClassification& result);
        static bool parseLegacyInfo(const QList<QByteArray>& lines,
                                    const QString& airProtocol,
                                    LFMemoryClassification& result);
        static bool parseBlock(const QList<QByteArray>& lines,
                               char expectedFamily, int expectedPage,
                               int expectedAddress, QByteArray& data);
        static bool parseTrace(const QList<QByteArray>& lines,
                               QByteArray& block1, QByteArray& block2);
        static bool parseStream(const QList<QByteArray>& lines,
                                QVector<QByteArray>& words);

        static bool isT55xx(const QString& chipType);
        static bool isEm4x05(const QString& chipType);
        static QStringList blockLabels(const QString& chipType);
        static int seedBlock(const LFMemoryClassification& classification);

    private:
        static QByteArray findLine(const QList<QByteArray>& lines,
                                   const QByteArray& prefix);
};

#endif // LFMEMORYPROTOCOL_H
