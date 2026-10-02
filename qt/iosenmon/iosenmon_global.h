#ifndef IOSENMON_GLOBAL_H
#define IOSENMON_GLOBAL_H

#define IOSENMON_VERSION "1.1.0"

#define SETTINGS_DB_LOC    "DB/Location"
#define SETTINGS_DB_AGE    "DB/Age"
#define SETTINGS_DB_AGE_DEFAULT (15*24*3600)
#define SETTINGS_DB_ICOUNT "DB/ItemCount"
#define SETTINGS_DB_ICOUNT_DEFAULT (16)
#define SETTINGS_DB_SAVEPATH "DB/ExportPath"

#define SETTINGS_UI_VISIBLE "UI/Show"
#define SETTINGS_SOFTGL     "UI/SoftGL"
#define SETTINGS_ALIASES    "UI/AliasHash"
#define SETTINGS_RESCAN_TIMEOUT "UI/Rescan"

#define SETTINGS_UNITS_TEMP   "US/Temp"
#define SETTINGS_UNITS_LENGTH "US/Length"

#define SETTINGS_LOG_SIZE   "Logging/Size"
#define SETTINGS_LOG_COUNT  "Logging/Count"
#define SETTINGS_LOG_ENABLE "Logging/Enable"

#define SETTINGS_HTTP_ENABLED "HTTP/Enable"

#define DB_CONN_NAME "IOSenMonDB"

#define STRINGIFY(X) #X

#if defined(__has_cpp_attribute) && __has_cpp_attribute(clang::fallthrough)
#define FALLTHROUGH [[clang::fallthrough]]
#elif defined(__GNUC__) && (__GNUC__ >= 7)
#define FALLTHROUGH __attribute__((fallthrough))
#else
#define FALLTHROUGH
#endif

#endif // IOSENMON_GLOBAL_H
