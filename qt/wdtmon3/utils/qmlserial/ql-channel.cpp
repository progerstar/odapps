#include "ql-channel.hpp"

QlChannel::QlChannel(QObject* parent) : QObject(parent)
{
}

QStringList QlChannel::channels() { return QStringList(); }

bool QlChannel::open(const QString &) { return false; }
bool QlChannel::isOpen() { return false; }
void QlChannel::close() { }
QString QlChannel::name() { return QString(""); }

QStringList QlChannel::params() { return params_; }
QString QlChannel::param(const QString &) { return QString(""); }
bool QlChannel::paramSet(const QString &, const QString &) { return false; }

QString QlChannel::readString() { return QString(); }

bool QlChannel::writeString(const QString &) { return false; }
