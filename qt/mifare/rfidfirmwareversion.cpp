#include "rfidcardreaderinterface.h"

#include <QRegularExpression>

RFIDCardReaderInterface::Revision RFIDCardReaderInterface::revisionForCode(int maj, int min, const QString& code)
{
    const QString normalizedCode = code.toLower();
    const bool thirdGeneration = (maj > 2) || ((maj == 2) && (min >= 9));

    if(normalizedCode == QLatin1String("m")) {
        return thirdGeneration ? REV_ThirdGen_HighLow : REV_SecondGen_HighLow;
    }
    if(normalizedCode == QLatin1String("n")) {
        return thirdGeneration ? REV_ThirdGen_High : REV_SecondGen_High;
    }
    if(normalizedCode == QLatin1String("e")) {
        return thirdGeneration ? REV_ThirdGen_Low : REV_SecondGen_Low;
    }
    return REV_FirstGen;
}

QString RFIDCardReaderInterface::codeForRevision(RFIDCardReaderInterface::Revision rev)
{
    switch (rev)
    {
        case REV_SecondGen_Low:
        case REV_ThirdGen_Low: return QStringLiteral("e");
        case REV_SecondGen_High:
        case REV_ThirdGen_High: return QStringLiteral("n");
        case REV_SecondGen_HighLow:
        case REV_ThirdGen_HighLow: return QStringLiteral("m");
        case REV_FirstGen: return QStringLiteral("F");
    }
    return QStringLiteral("?");
}

bool RFIDCardReaderInterface::parseFirmwareVersion(const QString& input, QString& versionText,
                                                   QVersionNumber& versionNumber, int* position)
{
    static const QRegularExpression versionExpression(
        QStringLiteral("([0-9]+)\\.([0-9]+)([Fmne])\\b"),
        QRegularExpression::CaseInsensitiveOption);

    versionText.clear();
    versionNumber = QVersionNumber();
    if(position) {
        *position = -1;
    }

    const QRegularExpressionMatch match = versionExpression.match(input);
    if(!match.hasMatch()) {
        return false;
    }

    bool majorOk = false;
    bool minorOk = false;
    const int major = match.captured(1).toInt(&majorOk);
    const int minor = match.captured(2).toInt(&minorOk);
    if(!majorOk || !minorOk) {
        return false;
    }

    versionText = match.captured(0);
    versionNumber = QVersionNumber(major, minor, revisionForCode(major, minor, match.captured(3)));
    if(position) {
        *position = match.capturedStart(0);
    }
    return true;
}
