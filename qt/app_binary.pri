unix:!macx:QMAKE_LFLAGS += '-Wl,-rpath,\'\$$ORIGIN/../lib\',-z,origin'
macx:QMAKE_LFLAGS  = -Wl,-install_name,@executable_path/../Frameworks/ -Wl,-rpath,@executable_path/../Frameworks/

isEmpty(NO_ICO){
    ICO_FILE = $$lower($${TARGET})
    message("ICO file $${ICO_FILE}")
    win32:                    RC_FILE = $${ICO_FILE}.rc
    macx:CONFIG(app_bundle):  ICON = $${ICO_FILE}.icns
}

macx:CONFIG(app_bundle){
    !isEmpty(MAC_BUNDLE_FRAMEWORKS){
        QMAKE_POST_LINK+=$$PWD/deploymacframework.sh "$$OUT_PWD/../lib" "$$OUT_PWD/$${TARGET}.app" $${MAC_BUNDLE_FRAMEWORKS} \
        && $$PWD/macbundleid.sh "$$OUT_PWD/$${TARGET}.app" $${TARGET} ru.open-dev
    }else{
        QMAKE_POST_LINK+= $$PWD/macbundleid.sh "$$OUT_PWD/$${TARGET}.app" $${TARGET} ru.open-dev
    }
}

isEmpty(OD_DEBUG):!win32 {
    isEmpty(QMAKE_POST_LINK) {
        QMAKE_POST_LINK = $(STRIP) $(TARGET)
    } else {
        QMAKE_POST_LINK += && $(STRIP) $(TARGET)
    }
    message("stripping with $(STRIP) utility")
}
