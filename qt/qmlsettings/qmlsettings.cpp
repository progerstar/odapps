/****************************************************************************
**
** Copyright (C) 2017 "Open Development" LLC.
** Contact: http://open-dev.ru/contacts
**
** This file is part of ODFarmControl
**
**  ODFarmControl is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  ODFarmControl is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with ODFarmControl.  If not, see <http://www.gnu.org/licenses/>.
****************************************************************************/

#include "qmlsettings.h"

#include <QDebug>
#include <QUrl>

QMLSettings::QMLSettings(QObject *parent) : QObject(parent), _set(this)
{

}

void QMLSettings::write(const QString& key, const QString& value, const QString& type)
{
    if(type=="int")
    {
        _set.setValue(key,value.toInt());
    }
    else if(type=="int_kb")
    {
        _set.setValue(key,quint64(1024)*value.toUInt());
    }
    else if(type=="int_mb")
    {
        _set.setValue(key,quint64(1024*1024)*value.toUInt());
    }
    else if(type=="bool")
    {
        _set.setValue(key,(value.toLower()=="true")?true:false);
    }
    else if(type=="color")
    {
        _set.setValue(key,QColor(value));
    }
    else if(type=="double")
    {
        _set.setValue(key,value.toDouble());
    }
    else if(type=="stringlist")
    {
        _set.setValue(key,value.split(";",QString::SkipEmptyParts));
    }
    else
    {
        _set.setValue(key,value);
    }

    _set.sync();
}

void QMLSettings::exportSettings(const QString& file)
{
    QString locFile = QUrl(file).toLocalFile();
    if(!locFile.endsWith(".ini",Qt::CaseInsensitive))
    {
        locFile+=".ini";
    }
    QSettings eset(locFile,QSettings::IniFormat);
    QStringList keys = _set.allKeys();
    foreach(const QString& key, keys)
    {
        eset.setValue(key,_set.value(key));
    }
    eset.sync();
}

bool QMLSettings::importSettings(const QString& file)
{
    QSettings iset(QUrl(file).toLocalFile(),QSettings::IniFormat);
    if(iset.status() != QSettings::NoError)
    {
        qWarning()<<"Cannot import settings from "<<file<<": "<<iset.status();
        return false;
    }
    QStringList keys = iset.allKeys();
    foreach(const QString& key, keys)
    {
        _set.setValue(key,iset.value(key));
    }
    _set.sync();
    return true;
}
