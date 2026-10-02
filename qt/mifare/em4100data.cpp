#include <em4100data.h>

QString EM4100Data::editHint(int block, int byte)
{
    if(!block) {
        if(_cardType == MF_HID_PROX) {
            Q_UNUSED(byte)
            return QT_TRANSLATE_NOOP("EM4100Data", "HID Prox raw 44-bit frame");
        }
        if(!byte) {
            return QT_TRANSLATE_NOOP("EM4100Data", "Version number or customer ID");
        } else {
            return QT_TRANSLATE_NOOP("EM4100Data", "Data bits");
        }
    }
    return QString();
}
