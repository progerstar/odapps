#include <QtTest>

#include <lfmemoryprotocol.h>
#include <mifare_global.h>

class TestLFMemoryProtocol : public QObject
{
        Q_OBJECT

    private slots:
        void parsesStructuredClassification();
        void parsesAirOnlyClassification();
        void rejectsMalformedClassification();
        void parsesLegacyClassification();
        void parsesExactBlockReply();
        void rejectsMismatchedBlockReply();
        void parsesTraceabilityReply();
        void parsesStandardReadStream();
        void rejectsStreamCountMismatch();
        void createsMemoryLayouts();
        void recognizesHidProxAsLfCard();
};

void TestLFMemoryProtocol::parsesStructuredClassification()
{
    LFMemoryClassification result;
    QVERIFY(LFMemoryProtocol::parseClassification(
                QList<QByteArray>()
                << QByteArrayLiteral("noise")
                << QByteArrayLiteral(
                       "+LFCLASS=OK,HID_PROX,T5577,T5577_TRACE,E0151234"),
                result));
    QCOMPARE(result.status, QStringLiteral("OK"));
    QCOMPARE(result.airProtocol, QStringLiteral("HID_PROX"));
    QCOMPARE(result.chipType, QStringLiteral("T5577"));
    QCOMPARE(result.evidence, QStringLiteral("T5577_TRACE"));
    QCOMPARE(result.chipInfo, QByteArray::fromHex("E0151234"));
    QVERIFY(result.hasMemoryChip());
    QCOMPARE(LFMemoryProtocol::seedBlock(result), 9);
}

void TestLFMemoryProtocol::parsesAirOnlyClassification()
{
    LFMemoryClassification result;
    QVERIFY(LFMemoryProtocol::parseClassification(
                QList<QByteArray>()
                << QByteArrayLiteral(
                       "+lfclass=air_only,em4100_compat,none,none,00000000"),
                result));
    QCOMPARE(result.status, QStringLiteral("AIR_ONLY"));
    QCOMPARE(result.airProtocol, QStringLiteral("EM4100_COMPAT"));
    QVERIFY(!result.hasMemoryChip());
}

void TestLFMemoryProtocol::rejectsMalformedClassification()
{
    LFMemoryClassification result;
    QVERIFY(!LFMemoryProtocol::parseClassification(
                QList<QByteArray>()
                << QByteArrayLiteral(
                       "+LFCLASS=OK,HID_PROX,T5577,T5577_TRACE,1234567"),
                result));
    QVERIFY(!LFMemoryProtocol::parseClassification(
                QList<QByteArray>()
                << QByteArrayLiteral(
                       "+LFCLASS=OK,HID_PROX,UNSUPPORTED,NONE,00000000"),
                result));
}

void TestLFMemoryProtocol::parsesLegacyClassification()
{
    LFMemoryClassification result;
    QVERIFY(LFMemoryProtocol::parseLegacyInfo(
                QList<QByteArray>()
                << QByteArrayLiteral("+LFINFO=T5577,E03900D0"),
                QStringLiteral("EM4100_COMPAT"), result));
    QCOMPARE(result.chipType, QStringLiteral("T5577"));
    QCOMPARE(result.evidence, QStringLiteral("T5577_TRACE"));
    QCOMPARE(result.airProtocol, QStringLiteral("EM4100_COMPAT"));
    QCOMPARE(LFMemoryProtocol::seedBlock(result), 9);
}

void TestLFMemoryProtocol::parsesExactBlockReply()
{
    QByteArray data;
    QVERIFY(LFMemoryProtocol::parseBlock(
                QList<QByteArray>()
                << QByteArrayLiteral("+LFREAD=T,1,7,01234567"),
                'T', 1, 7, data));
    QCOMPARE(data, QByteArray::fromHex("01234567"));

    QVERIFY(LFMemoryProtocol::parseBlock(
                QList<QByteArray>()
                << QByteArrayLiteral("+LFREAD=E,0,15,AABBCCDD"),
                'E', 0, 15, data));
    QCOMPARE(data, QByteArray::fromHex("AABBCCDD"));
}

void TestLFMemoryProtocol::rejectsMismatchedBlockReply()
{
    QByteArray data;
    QVERIFY(!LFMemoryProtocol::parseBlock(
                QList<QByteArray>()
                << QByteArrayLiteral("+LFREAD=T,0,3,DEADBEEF"),
                'T', 0, 4, data));
    QVERIFY(!LFMemoryProtocol::parseBlock(
                QList<QByteArray>()
                << QByteArrayLiteral("+LFREAD=E,0,3,DEADBEEF"),
                'T', 0, 3, data));
}

void TestLFMemoryProtocol::parsesTraceabilityReply()
{
    QByteArray block1;
    QByteArray block2;
    QVERIFY(LFMemoryProtocol::parseTrace(
                QList<QByteArray>()
                << QByteArrayLiteral("+LFTRACE=E0151234,89ABCDEF"),
                block1, block2));
    QCOMPARE(block1, QByteArray::fromHex("E0151234"));
    QCOMPARE(block2, QByteArray::fromHex("89ABCDEF"));
}

void TestLFMemoryProtocol::parsesStandardReadStream()
{
    QVector<QByteArray> words;
    QVERIFY(LFMemoryProtocol::parseStream(
                QList<QByteArray>()
                << QByteArrayLiteral(
                       "+LFSTREAM=3,01234567,89ABCDEF,00112233"),
                words));
    QCOMPARE(words.size(), 3);
    QCOMPARE(words.at(0), QByteArray::fromHex("01234567"));
    QCOMPARE(words.at(2), QByteArray::fromHex("00112233"));
}

void TestLFMemoryProtocol::rejectsStreamCountMismatch()
{
    QVector<QByteArray> words;
    QVERIFY(!LFMemoryProtocol::parseStream(
                QList<QByteArray>()
                << QByteArrayLiteral("+LFSTREAM=2,01234567"),
                words));
    QVERIFY(words.isEmpty());
}

void TestLFMemoryProtocol::createsMemoryLayouts()
{
    const QStringList t55xx = LFMemoryProtocol::blockLabels(
                QStringLiteral("T5577"));
    QCOMPARE(t55xx.size(), 16);
    QCOMPARE(t55xx.first(), QStringLiteral("P0 / B0"));
    QCOMPARE(t55xx.at(9), QStringLiteral("P1 / B1"));
    QCOMPARE(t55xx.last(), QStringLiteral("P1 / B7"));

    const QStringList em4x05 = LFMemoryProtocol::blockLabels(
                QStringLiteral("EM4305"));
    QCOMPARE(em4x05.size(), 16);
    QCOMPARE(em4x05.last(), QStringLiteral("W15"));

    const QStringList em4x50 = LFMemoryProtocol::blockLabels(
                QStringLiteral("EM4X50"));
    QCOMPARE(em4x50.size(), 6);
    QCOMPARE(em4x50.last(), QStringLiteral("W5"));
}

void TestLFMemoryProtocol::recognizesHidProxAsLfCard()
{
    QCOMPARE(int(MF_HID_PROX), 33);
    QVERIFY(isHidProxCard(MF_HID_PROX));
    QVERIFY(isLfCard(MF_HID_PROX));
    QVERIFY(isLfCard(MF_EM_4100));
    QVERIFY(!isLfCard(MF_CLASSIC_1K));
}

QTEST_APPLESS_MAIN(TestLFMemoryProtocol)

#include "tst_lfmemoryprotocol.moc"
