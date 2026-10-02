#ifndef MIFAREULTRALIGHTCONFIG_COMMON_H
#define MIFAREULTRALIGHTCONFIG_COMMON_H

#define CFG0_MOD_BYTE             (0)
#define CFG0_MOD_BITS_STRG_MOD_EN     (0x04)   /*Strong modulation : 0 - disabled, 1 - enabled*/

#define CFG0_AUTH0_BYTE           (3)          /*First password protected page (>=PAGE_COUNT = disabled)*/

#define CFG1_ACCESS_BYTE          (0)
#define CFG1_ACCESS_BITS_PROT         (0x80)   /*Memory protection: 0 - write access is password protected, 1 - read/write access is password protected*/
#define CFG1_ACCESS_BITS_CFGLCK       (0x40)   /*CFG0/1 protection: 0 - open to write access, 1 - permanently locked against write access*/
#define CFG1_ACCESS_BITS_AUTHLIM      (0x07)   /*Negative password verification attempts limit: 0 - disabled, 1-7 - maximum number of attempts*/

#endif // MIFAREULTRALIGHTCONFIG_COMMON_H
