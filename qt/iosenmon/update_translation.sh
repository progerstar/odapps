#!/bin/sh

/opt/Qt/5.9/gcc_64/bin/lupdate *.{cpp,h,qml} ../singleapp/*.{cpp,h} ../uptime/*.{cpp,h} ../qjournald/*.{cpp,h} ../qautostarter/*.{cpp,h} ../qmuhttp/*.{cpp,h} ../qmlsettings/*.{cpp,h} ../qmlsystrayicon/*.{cpp,h}   -ts iosenmon_ru.ts
/opt/Qt/5.9/gcc_64/bin/linguist iosenmon_ru.ts
/opt/Qt/5.9/gcc_64/bin/lrelease iosenmon_ru.ts
