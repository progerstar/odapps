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

#ifndef SETTINGS_H
#define SETTINGS_H

#include <QObject>
#include <QSettings>
#include <QColor>

class Settings : public QObject
{
        Q_OBJECT
    public:
        explicit Settings(QObject *parent = nullptr);

        Q_INVOKABLE QString read(const QString& key, const QString& defaultValue = QString()) {
            return  _set.contains(key) ? _set.value(key).toString() : defaultValue;
        }

        Q_INVOKABLE void write(const QString& key, const QString& value, const QString& type = "string");

        Q_INVOKABLE bool hasKey(const QString& key) const {
            return _set.contains(key);
        }

        Q_INVOKABLE void reset(const QString& key) {
            _set.remove(key);
        }

        Q_INVOKABLE void remove(const QString& key) {
            _set.remove(key);
        }

        Q_INVOKABLE void flush() {
            _set.sync();
        }

        Q_INVOKABLE bool exportSettings(const QString& file);
        Q_INVOKABLE bool importSettings(const QString& file);

    private:
        QSettings _set;
};

#endif // SETTINGS_H
