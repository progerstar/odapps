#!/bin/sh

#/opt/Qt/5.9/gcc_64/bin/lupdate wdtmon3.pro
/opt/Qt/5.9/gcc_64/bin/lupdate *.{cpp,h} lib/*.{cpp,hpp} *.qml ../singleapp/*.{cpp,h} ../uptime/*.{cpp,h} ../qjournald/*.{cpp,h} ../qmlsystrayicon/*.{cpp,h} ../systemprocess/*.{h,cpp,qml}  -ts wdtmon_ru.ts
/opt/Qt/5.9/gcc_64/bin/linguist wdtmon_ru.ts
/opt/Qt/5.9/gcc_64/bin/lrelease wdtmon_ru.ts
