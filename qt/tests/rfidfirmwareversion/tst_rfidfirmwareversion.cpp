#include <QtTest>

#include <rfidcardreaderinterface.h>

class TestRFIDFirmwareVersion : public QObject
{
        Q_OBJECT

    private slots:
        void parsesSupportedVersions_data();
        void parsesSupportedVersions();
        void rejectsInvalidVersions_data();
        void rejectsInvalidVersions();
};

void TestRFIDFirmwareVersion::parsesSupportedVersions_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("position");
    QTest::addColumn<int>("major");
    QTest::addColumn<int>("minor");
    QTest::addColumn<int>("revision");

    QTest::newRow("legacy") << QStringLiteral("1.4F") << QStringLiteral("1.4F") << 0
                             << 1 << 4 << int(RFIDCardReaderInterface::REV_FirstGen);
    QTest::newRow("two-digit-minor") << QStringLiteral("ODRFID v1.10F 2026-08-23")
                                      << QStringLiteral("1.10F") << 8
                                      << 1 << 10 << int(RFIDCardReaderInterface::REV_FirstGen);
    QTest::newRow("third-gen-boundary") << QStringLiteral("reader 2.9m") << QStringLiteral("2.9m") << 7
                                        << 2 << 9 << int(RFIDCardReaderInterface::REV_ThirdGen_HighLow);
    QTest::newRow("third-gen-two-digit-minor") << QStringLiteral("reader 2.10m") << QStringLiteral("2.10m") << 7
                                                << 2 << 10 << int(RFIDCardReaderInterface::REV_ThirdGen_HighLow);
    QTest::newRow("two-digit-major-and-minor") << QStringLiteral("fw=12.34n; built today")
                                                << QStringLiteral("12.34n") << 3
                                                << 12 << 34 << int(RFIDCardReaderInterface::REV_ThirdGen_High);
    QTest::newRow("case-insensitive-code") << QStringLiteral("3.12E") << QStringLiteral("3.12E") << 0
                                            << 3 << 12 << int(RFIDCardReaderInterface::REV_ThirdGen_Low);
    QTest::newRow("odrfid3-m-device-reply")
        << QStringLiteral("Open Development RFID Reader 3.13m aa24c8cb8631-dirty")
        << QStringLiteral("3.13m") << 29
        << 3 << 13 << int(RFIDCardReaderInterface::REV_ThirdGen_HighLow);
}

void TestRFIDFirmwareVersion::parsesSupportedVersions()
{
    QFETCH(QString, input);
    QFETCH(QString, text);
    QFETCH(int, position);
    QFETCH(int, major);
    QFETCH(int, minor);
    QFETCH(int, revision);

    QString parsedText;
    QVersionNumber parsedNumber;
    int parsedPosition = -1;

    QVERIFY(RFIDCardReaderInterface::parseFirmwareVersion(input, parsedText, parsedNumber, &parsedPosition));
    QCOMPARE(parsedText, text);
    QCOMPARE(parsedPosition, position);
    QCOMPARE(parsedNumber.majorVersion(), major);
    QCOMPARE(parsedNumber.minorVersion(), minor);
    QCOMPARE(parsedNumber.microVersion(), revision);
}

void TestRFIDFirmwareVersion::rejectsInvalidVersions_data()
{
    QTest::addColumn<QString>("input");

    QTest::newRow("empty") << QString();
    QTest::newRow("no-revision") << QStringLiteral("1.10");
    QTest::newRow("missing-major") << QStringLiteral(".10F");
    QTest::newRow("missing-minor") << QStringLiteral("1.F");
    QTest::newRow("unknown-revision") << QStringLiteral("1.10x");
    QTest::newRow("trailing-identifier") << QStringLiteral("1.10Fextra");
    QTest::newRow("major-overflow") << QStringLiteral("999999999999.10F");
    QTest::newRow("minor-overflow") << QStringLiteral("1.999999999999F");
}

void TestRFIDFirmwareVersion::rejectsInvalidVersions()
{
    QFETCH(QString, input);

    QString parsedText = QStringLiteral("stale");
    QVersionNumber parsedNumber(9, 9, 9);
    int parsedPosition = 42;

    QVERIFY(!RFIDCardReaderInterface::parseFirmwareVersion(input, parsedText, parsedNumber, &parsedPosition));
    QVERIFY(parsedText.isEmpty());
    QCOMPARE(parsedNumber.segmentCount(), 0);
    QCOMPARE(parsedPosition, -1);
}

QTEST_APPLESS_MAIN(TestRFIDFirmwareVersion)

#include "tst_rfidfirmwareversion.moc"
