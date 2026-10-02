#include "qpathparser.h"

#include <QtGlobal>
#include <QDir>
#include <QFileInfo>
#include <QRegExp>

QString QPathParser::canonicalFilePath(const QString& input)
{
#if defined (Q_OS_LINUX) || defined(Q_OS_DARWIN)
    if(input.startsWith(QChar('~')))
    {
        return QFileInfo(QDir::homePath()+input.mid(1)).canonicalFilePath();
    }
#elif defined(Q_OS_WIN)
    if(input.startsWith("%USERPROFILE%", Qt::CaseSensitive) || input.startsWith("%HOMEPATH%", Qt::CaseSensitive))
    {
        return QFileInfo(QDir::homePath()+input.mid(input.indexOf('%', 1)+1)).canonicalFilePath();
    }
#endif
    return QFileInfo(input).canonicalFilePath();
}
