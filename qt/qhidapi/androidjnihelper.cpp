#include "androidjnihelper.h"

bool AndroidJNIHelper::requestPermission(const QString& id)
{
    QtAndroid::PermissionResult req = QtAndroid::checkPermission(id);
    if(req == QtAndroid::PermissionResult::Denied) {
        QtAndroid::requestPermissionsSync(QStringList()<<id, 5000);
        req = QtAndroid::checkPermission(id);
    }
    return (req == QtAndroid::PermissionResult::Granted);
}
