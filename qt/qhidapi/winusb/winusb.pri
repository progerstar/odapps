isEmpty(LIBUSB_WIN32_PRI_INCLUDED) {

LIBUSB_WIN32_PRI_INCLUDED = 1

!isEmpty(DISTRO_WIN){
   !isEmpty(DISTRO_WIN32):LIBUSB_ARCHIVE = $$PWD/lib/libusb-1.0.a
   !isEmpty(DISTRO_WIN64):LIBUSB_ARCHIVE = $$PWD/lib64/libusb-1.0.a

   exists($$LIBUSB_ARCHIVE) {
      INCLUDEPATH += $$quote($$PWD/include) $$quote($$PWD/include/libusb-1.0)
      LIBS += $$quote($$LIBUSB_ARCHIVE)
   } else {
      CONFIG += link_pkgconfig
      packagesExist(libusb-1.0) {
         PKGCONFIG += libusb-1.0
      } else {
         error("libusb-1.0 was not found through pkg-config")
      }
   }
}

}
