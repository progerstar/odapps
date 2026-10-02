#ifndef ODHID_GLOBAL_H
#define ODHID_GLOBAL_H

#define OD_VID     0x0483
#define OD_PID_DFU 0xA26E
#define OD_PID_WDG 0xA26D

#define HIDDFU_VER_MAJ 0
#define HIDDFU_VER_MIN 3
#define HIDDFU_VER_PAT 2
#define HIDDFU_VERSION "0.4.0"

#define SETTINGS_DFU_STORAGE ("DFU/StorageDir")

#include <QStringList>
#include <QCommandLineParser>

class QApplication;

enum BootMode {
    Boot_Normal,
    Boot_Restart,
    Boot_DFU,
    Boot_Fault
};

BootMode libhid_main(QApplication* dapp, const QString& dfuTransport, const QStringList& dfuProducts);
void libhid_deinit(void);

#endif // ODHID_GLOBAL_H
