/****************************************************************************
**
** Copyright (C) 2017 "Open Development" LLC.
** Contact: mail@unitx.pro
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

#include "settings.h"

#include <QDebug>
#include <QFileInfo>
#include <QUrl>

namespace {
QString localSettingsPath(const QString& file)
{
    const QUrl url(file);
    return url.isLocalFile() ? url.toLocalFile() : file;
}
}

Settings::Settings(QObject *parent) : QObject(parent), _set(this)
{

}

void Settings::write(const QString& key, const QString& value, const QString& type)
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
        _set.setValue(key,value.split(";", Qt::SkipEmptyParts));
    }
    else
    {
        _set.setValue(key,value);
    }

    _set.sync();
}

bool Settings::exportSettings(const QString& file)
{
    QString locFile = localSettingsPath(file);
    if(locFile.isEmpty())
    {
        return false;
    }
    if(!locFile.endsWith(".ini",Qt::CaseInsensitive))
    {
        locFile+=".ini";
    }
    QSettings eset(locFile,QSettings::IniFormat);
    eset.clear();
    const QStringList keys = _set.allKeys();
    for(const QString& key : keys)
    {
        eset.setValue(key,_set.value(key));
    }
    eset.sync();
    if(eset.status() != QSettings::NoError)
    {
        qWarning()<<"Cannot export settings to "<<locFile<<": "<<eset.status();
        return false;
    }
    return true;
}

bool Settings::importSettings(const QString& file)
{
    const QString locFile = localSettingsPath(file);
    const QFileInfo fileInfo(locFile);
    if(locFile.isEmpty() || !fileInfo.exists() || !fileInfo.isFile() || !fileInfo.isReadable())
    {
        qWarning()<<"Cannot import settings from "<<locFile<<": file is not readable";
        return false;
    }

    QSettings iset(locFile,QSettings::IniFormat);
    const QStringList keys = iset.allKeys();
    if(iset.status() != QSettings::NoError)
    {
        qWarning()<<"Cannot import settings from "<<file<<": "<<iset.status();
        return false;
    }
    for(const QString& key : keys)
    {
        _set.setValue(key,iset.value(key));
    }
    _set.sync();
    return _set.status() == QSettings::NoError;
}
