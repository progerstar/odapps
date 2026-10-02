!isEmpty(DD_PRI_INCLUDED){
    error("distrodefines.pri already included")
}
DD_PRI_INCLUDED = 1

win32 {
    ## Windows common build here
    DISTRO_WIN = 1
    win32-g++ {
        !isEmpty(BUILD_WIN64){
            DISTRO_WIN64 = 1
        } else {
            DISTRO_WIN32 = 1
        }
    } else:win64-g++ {
        DISTRO_WIN64 = 1
    } else:win32-msvc {
        CONFIG += embed_manifest_exe
        DISTRO_WIN64 = 1
    } else {
        error("Unknown windows mkspecs $$QMAKESPEC")
    }
} else:macx {
    DISTRO_MACX = 1
} else:android-g++|android-clang{
    DISTRO_ANDROID = 1
} else {
    ## Linux common build here
    DISTRO_LINUX = 1

    IS_UBUNTU=$$system(cat /proc/version | grep -o Ubuntu)
    !isEmpty(IS_UBUNTU):DISTRO_LINUX_UBUNTU = 1

    IS_FEDORA=$$system(cat /proc/version | grep -o Fedora)
    !isEmpty(IS_FEDORA):DISTRO_LINUX_FEDORA = 1

    IS_FEDORA=$$system(cat /proc/version | grep -o fedora)
    !isEmpty(IS_FEDORA):DISTRO_LINUX_FEDORA = 1

    IS_REDHAT=$$system(cat /proc/version | grep -o Hat)
    !isEmpty(IS_REDHAT):DISTRO_LINUX_REDHAT = 1

    isEmpty(DISTRO_LINUX_UBUNTU):isEmpty(DISTRO_LINUX_FEDORA):isEmpty(DISTRO_LINUX_REDHAT) {
        DISTRO_LINUX_UNKNOWN = 1
        warning("Unknown linux distribution")
    }

    !isEmpty(DISTRO_LINUX_FEDORA){
        #on Fedora 25 (and possibly other new distros claim usb fails)
        IS_FC25=$$system(cat /proc/version | grep -o "fc2[5-7]")
        !isEmpty(IS_FC25){
            warning("Detected Fedora 25 distro - applying libusb quirk")
            DEFINES+=LIBUSB_CLAIM_QUIRK

        }
    }

    linux-g++-64 {
        DISTRO_LINUX64 = 1
    } else:linux-g++-32 {
        DISTRO_LINUX32 = 1
    } else:linux-g++{
        IF_64HOST = $$find(QMAKE_HOST.arch, .*64.*)
        isEmpty(IF_64HOST) {
            DISTRO_LINUX32 = 1
        } else {
            DISTRO_LINUX64 = 1
        }
    } else{
        error("Unsupported unix mkspecs $$QMAKESPEC")
    }

    !isEmpty(DISTRO_LINUX32){
        DEFINES += Q_OS_LINUX_BUILD_X86
    }
    !isEmpty(DISTRO_LINUX64){
        DEFINES += Q_OS_LINUX_BUILD_X64
    }
}

!isEmpty(OD_DEBUG) {
    warning("ATTENTION!!!!: Debug output will be shown")
} else {
    DEFINES += QT_NO_DEBUG_OUTPUT QT_NO_WARNING_OUTPUT
    message("Debug output will be suppressed")
}
