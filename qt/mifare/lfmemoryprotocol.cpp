#include "lfmemoryprotocol.h"

#include <QRegExp>

namespace {

QByteArray decodeWord(const QString& text)
{
    if(text.size() != 8)
    {
        return QByteArray();
    }

    const QByteArray encoded = text.toLatin1();
    const QByteArray decoded = QByteArray::fromHex(encoded);
    return decoded.size() == 4 ? decoded : QByteArray();
}

}

bool LFMemoryClassification::hasMemoryChip() const
{
    return status == QLatin1String("OK") &&
           chipType != QLatin1String("NONE") && !chipType.isEmpty();
}

LFMemoryReadResult::LFMemoryReadResult() : success(false)
{
}

int LFMemoryReadResult::readBlockCount() const
{
    int count = 0;
    for(const QByteArray& block : blocks)
    {
        if(block.size() == 4)
        {
            ++count;
        }
    }
    return count;
}

QByteArray LFMemoryProtocol::findLine(const QList<QByteArray>& lines,
                                      const QByteArray& prefix)
{
    for(const QByteArray& rawLine : lines)
    {
        const QByteArray line = rawLine.trimmed();
        if(line.size() >= prefix.size() &&
           line.left(prefix.size()).compare(prefix, Qt::CaseInsensitive) == 0)
        {
            return line;
        }
    }
    return QByteArray();
}

bool LFMemoryProtocol::parseClassification(const QList<QByteArray>& lines,
                                           LFMemoryClassification& result)
{
    const QByteArray line = findLine(lines, QByteArrayLiteral("+LFCLASS="));
    QRegExp expression(
        QStringLiteral("^\\+LFCLASS=(OK|AIR_ONLY|NO_SIGNAL|BUSY),"
                       "(EM4100_COMPAT|HID_PROX|UNKNOWN|NONE),"
                       "(T55XX|T5577|T5555|EM4205|EM4305|EM4369|EM4469|EM4X05|EM4X50|NONE),"
                       "(T55XX_CONFIG|T5577_TRACE|T5555_TRACE|EM4X05_WORD0|EM4X50_STREAM|NONE),"
                       "([A-F0-9]{8})$"),
        Qt::CaseInsensitive);

    if(!expression.exactMatch(QString::fromLatin1(line)))
    {
        return false;
    }

    const QByteArray chipInfo = decodeWord(expression.cap(5).toUpper());
    if(chipInfo.size() != 4)
    {
        return false;
    }

    result.status = expression.cap(1).toUpper();
    result.airProtocol = expression.cap(2).toUpper();
    result.chipType = expression.cap(3).toUpper();
    result.evidence = expression.cap(4).toUpper();
    result.chipInfo = chipInfo;
    return true;
}

bool LFMemoryProtocol::parseLegacyInfo(const QList<QByteArray>& lines,
                                       const QString& airProtocol,
                                       LFMemoryClassification& result)
{
    const QByteArray line = findLine(lines, QByteArrayLiteral("+LFINFO="));
    QRegExp expression(
        QStringLiteral("^\\+LFINFO=(T5577|T5555|EM4205|EM4305|EM4369|EM4469|EM4X05|EM4X50),"
                       "([A-F0-9]{8})$"),
        Qt::CaseInsensitive);

    if(!expression.exactMatch(QString::fromLatin1(line)))
    {
        return false;
    }

    const QString chipType = expression.cap(1).toUpper();
    const QString chipInfoText = expression.cap(2).toUpper();
    const QByteArray chipInfo = decodeWord(chipInfoText);
    if(chipInfo.size() != 4)
    {
        return false;
    }

    QString evidence;
    if(chipType == QLatin1String("T5555"))
    {
        evidence = QStringLiteral("T5555_TRACE");
    }
    else if(chipType == QLatin1String("T5577"))
    {
        evidence = (chipInfoText.startsWith(QLatin1String("E015")) ||
                    chipInfoText.startsWith(QLatin1String("E039")))
                ? QStringLiteral("T5577_TRACE")
                : QStringLiteral("T55XX_CONFIG");
    }
    else if(chipType == QLatin1String("EM4X50"))
    {
        evidence = QStringLiteral("EM4X50_STREAM");
    }
    else
    {
        evidence = QStringLiteral("EM4X05_WORD0");
    }

    result.status = QStringLiteral("OK");
    result.airProtocol = airProtocol.toUpper();
    result.chipType = chipType;
    result.evidence = evidence;
    result.chipInfo = chipInfo;
    return true;
}

bool LFMemoryProtocol::parseBlock(const QList<QByteArray>& lines,
                                  char expectedFamily, int expectedPage,
                                  int expectedAddress, QByteArray& data)
{
    const QByteArray line = findLine(lines, QByteArrayLiteral("+LFREAD="));
    QRegExp expression(
        QStringLiteral("^\\+LFREAD=([TE]),([01]),(\\d{1,2}),([A-F0-9]{8})$"),
        Qt::CaseInsensitive);

    if(!expression.exactMatch(QString::fromLatin1(line)))
    {
        return false;
    }

    bool addressOk = false;
    const int address = expression.cap(3).toInt(&addressOk);
    const QByteArray decoded = decodeWord(expression.cap(4).toUpper());
    if(!addressOk || decoded.size() != 4 ||
       expression.cap(1).at(0).toUpper().toLatin1() !=
           QChar::fromLatin1(expectedFamily).toUpper().toLatin1() ||
       expression.cap(2).toInt() != expectedPage || address != expectedAddress)
    {
        return false;
    }

    data = decoded;
    return true;
}

bool LFMemoryProtocol::parseTrace(const QList<QByteArray>& lines,
                                  QByteArray& block1, QByteArray& block2)
{
    const QByteArray line = findLine(lines, QByteArrayLiteral("+LFTRACE="));
    QRegExp expression(QStringLiteral("^\\+LFTRACE=([A-F0-9]{8}),([A-F0-9]{8})$"),
                       Qt::CaseInsensitive);
    if(!expression.exactMatch(QString::fromLatin1(line)))
    {
        return false;
    }

    const QByteArray first = decodeWord(expression.cap(1).toUpper());
    const QByteArray second = decodeWord(expression.cap(2).toUpper());
    if(first.size() != 4 || second.size() != 4)
    {
        return false;
    }

    block1 = first;
    block2 = second;
    return true;
}

bool LFMemoryProtocol::parseStream(const QList<QByteArray>& lines,
                                   QVector<QByteArray>& words)
{
    const QByteArray line = findLine(lines, QByteArrayLiteral("+LFSTREAM="));
    QRegExp expression(QStringLiteral("^\\+LFSTREAM=(\\d+)((?:,[A-F0-9]{8})*)$"),
                       Qt::CaseInsensitive);
    if(!expression.exactMatch(QString::fromLatin1(line)))
    {
        return false;
    }

    bool countOk = false;
    const int count = expression.cap(1).toInt(&countOk);
    if(!countOk || count < 0 || count > 16)
    {
        return false;
    }

    const QString values = expression.cap(2);
    const QStringList encodedWords = values.isEmpty()
            ? QStringList()
            : values.mid(1).split(QLatin1Char(','));
    if(encodedWords.size() != count)
    {
        return false;
    }

    QVector<QByteArray> decodedWords;
    decodedWords.reserve(count);
    for(const QString& encodedWord : encodedWords)
    {
        const QByteArray decoded = decodeWord(encodedWord.toUpper());
        if(decoded.size() != 4)
        {
            return false;
        }
        decodedWords.append(decoded);
    }

    words = decodedWords;
    return true;
}

bool LFMemoryProtocol::isT55xx(const QString& chipType)
{
    return chipType == QLatin1String("T55XX") ||
           chipType == QLatin1String("T5577") ||
           chipType == QLatin1String("T5555");
}

bool LFMemoryProtocol::isEm4x05(const QString& chipType)
{
    return chipType == QLatin1String("EM4205") ||
           chipType == QLatin1String("EM4305") ||
           chipType == QLatin1String("EM4369") ||
           chipType == QLatin1String("EM4469") ||
           chipType == QLatin1String("EM4X05");
}

QStringList LFMemoryProtocol::blockLabels(const QString& chipType)
{
    QStringList labels;
    if(isT55xx(chipType))
    {
        for(int page = 0; page < 2; ++page)
        {
            for(int block = 0; block < 8; ++block)
            {
                labels.append(QStringLiteral("P%1 / B%2").arg(page).arg(block));
            }
        }
    }
    else
    {
        const int count = chipType == QLatin1String("EM4X50") ? 6 : 16;
        for(int word = 0; word < count; ++word)
        {
            labels.append(QStringLiteral("W%1").arg(word));
        }
    }
    return labels;
}

int LFMemoryProtocol::seedBlock(const LFMemoryClassification& classification)
{
    if(classification.evidence == QLatin1String("T5577_TRACE") ||
       classification.evidence == QLatin1String("T5555_TRACE"))
    {
        return 9;
    }
    return 0;
}
