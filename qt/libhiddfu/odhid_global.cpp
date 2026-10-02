#include "odhid_global.h"
#include "hiddfu.h"
#include <qmaterialfont.h>
#include <themedetector.h>

#include <QMessageBox>
#include <QApplication>

BootMode libhid_main(QApplication *dapp, const QString& dfuTransport, const QStringList& dfuProducts)
{
    BootMode bmode = Boot_Normal;

    QMaterialIcon* materialIcons = QMaterialIcon::instance();
    if(!materialIcons->firstRun()) {
        materialIcons->reinit();
    }
    materialIcons->setDefaultColors(ThemeDetector::isDarkThemeEnabled());


    HIDDfu* dfu = new HIDDfu(nullptr, Qt::Window);
    dfu->setDevMode(false);
    dfu->setTransport(dfuTransport);
    dfu->setProducts(dfuProducts);
    if(!dfu->init()) {
        QMessageBox::critical(nullptr, "Firmware Updater",
                              QT_TRANSLATE_NOOP("DfuLoop", "A problem with the Updater application has been detected. "
                                                           "The application will be closed."));
        delete dfu;
        return Boot_Restart;
    }

    dfu->show();
    (void)dapp->exec();
    bmode = dfu->exitMode();
    delete dfu;

    // NOT here - can be called multiple times!
    //HIDProxy::deinit();
    return bmode;
}

void libhid_deinit(void)
{
    HIDProxy::deinit();
}
