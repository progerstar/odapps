#ifndef RFIDKIT_GLOBAL_H
#define RFIDKIT_GLOBAL_H

#include <QtGlobal>
#include <QStringList>
#include <QMetaType>
#include <QFlags>
#include <QDebug>

#define ODRFIDKIT_VERSION    "2.5.0"

#define SETTINGS_WIN_GEOM    "UI/Geometry"
#define SETTINGS_WIN_STATE   "UI/State"

#define SETTINGS_LAST_PORT   "UI/LastReader"

#define SETTINGS_USB         "USB"
#define SETTINGS_WARNLOCK    "WarnDeadLock"
#define SETTINGS_LOGGING     "Debug/Logging"
#define SETTINGS_NET         "Network"
#define SETTINGS_UDP         (SETTINGS_NET "/UDP")
#define SETTINGS_UDP_PORT    (SETTINGS_NET "/UDP_Port")
#define SETTINGS_TCP         (SETTINGS_NET "/TCP")
#define SETTINGS_TCP_PORT    (SETTINGS_NET "/TCP_Port")
#define SETTINGS_KEEPPWD     "KeepPassword"

#define SETTINGS_EM_PASSWORD_OLD "EM/PasswordOld"
#define SETTINGS_EM_PASSWORD_NEW "EM/PasswordNew"
#define SETTINGS_EM_CODING   "EM/Coding"
#define SETTINSG_EM_SPEED    "EM/Speed"

#define MAX_PORT                  65535
#define SETTINGS_UDP_PORT_DEFAULT 12345
#define SETTINGS_TCP_PORT_DEFAULT 12346

/* UDP Protocol
 *
 * Protocol is text based. Accepted encoding: UTF-8 only. End of 'packet' - newline character '\n'
 * !! '\r' is not accepted, nor is '\r\n' combination !!
 *
 * Communication consists of
 * a) initiated by the remote client:
 *  1) 'hello' - client identification;
 *      client will not receive anything from the server, unless its first message is 'hello'
 *      server replies with 'ok'
 *  2) client requests. Currently supported:
 *      - 'help'
 *        server replies with 'help> "comma separated list of supported commands"
 *      - 'status'
 *        server replies with 'status> usb: connected/disconnected; card: present/not present'
 *      - 'goodbye'
 *        server replies with 'ok' and stops the communication with this client
 *      - 'any other command'
 *        server replies with 'nop' - i.e. command is not supported
 * b) initiated by the server
 *  1) card detection datagram
 *      'card > type: "CardTypeString"; uid: "CardUID_as_HEX"'
 *  2) server closure notification
 *      'goodbye'
 *
 * --------Communication example-------------
 *
 * 1. client:  'hello\n'
 * 2. server:  'ok\n'
 * 3. client:  'status\n'
 * 4. server:  'status> usb:connected; card:not present'
 * ...
 * 5. server:  'card> type:Classic 1K; uid: 00010203'
 * ...
 * 6. client:  'goodbye\n'
 * 7. server:   'ok\n'
 *
 * (to resume communication, client must send 'hello' first)
 */


/*******************************************************************************/

#endif // RFIDKIT_GLOBAL_H
