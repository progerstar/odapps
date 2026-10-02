!isEmpty(CD_PRI_INCLUDED):error("commondefines.pri already included")
CD_PRI_INCLUDED = 1

isEmpty(DD_PRI_INCLUDED):include ($$PWD/distrodefines.pri)

CONFIG += c++11
macx{
    QMAKE_LFLAGS_SONAME = -Wl,-install_name,@executable_path/../Frameworks/
}

!isEmpty(DISTRO_WIN){
    isEmpty( PREFIX ){
        PREFIX="C:/Program Files/Open-dev.ru/"
    }
    MOC_DIR     = moc_win
    OBJECTS_DIR = obj_win
    CONFIG(static):QTPLUGIN+=qwindows
}
!isEmpty(DISTRO_LINUX){
    isEmpty( PREFIX ) {
        PREFIX=/opt/opendev/
    }
    MOC_DIR     = .moc_unix
    OBJECTS_DIR = .obj_unix
    CONFIG(static):QTPLUGIN+=qlinuxfb qminimal
}

!isEmpty(DISTRO_MACX){
    isEmpty( PREFIX ) {
        PREFIX      = $$system(echo $HOME)
        BIN_DIR     ="$${PREFIX}/Applications"
        LIB_DIR     = "/usr/local/lib"
        INCLUDE_DIR = "/usr/local/include"
    }
    MOC_DIR     = .moc_macx
    OBJECTS_DIR = .obj_macx
    CONFIG(static):QTPLUGIN+=qcocoa
} else {
    DATA_DIR = "$${PREFIX}/share/odrfidcfg"
    BIN_DIR = "$${PREFIX}/bin"
    win32{
        LIB_DIR = "$${PREFIX}/bin"
    } else {
        LIB_DIR = "$${PREFIX}/lib"
    }
    INCLUDE_DIR = "$${PREFIX}/include"
}

!isEmpty ( CXX_FLAGS ){
        QMAKE_CXXFLAGS += $${CXX_FLAGS}
}

CONFIG += rtti
CONFIG -=silent

!isEmpty(OD_DEBUG) {
    warning("ATTENTION!!!!: Debug output will be shown")
} else {
    DEFINES += QT_NO_DEBUG_OUTPUT QT_NO_WARNING_OUTPUT
    message("Debug output will be suppressed")
}
