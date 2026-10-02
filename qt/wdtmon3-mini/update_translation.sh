#!/bin/sh

/opt/Qt/5.9/gcc_64/bin/lupdate wdtmon3_lite.pro
/opt/Qt/5.9/gcc_64/bin/linguist wdtmon3_lite_ru.ts
/opt/Qt/5.9/gcc_64/bin/lrelease wdtmon3_lite_ru.ts
