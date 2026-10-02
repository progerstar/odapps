#include "mifare_global.h"

static int _mfClassicKeyTypeID = qRegisterMetaType<MifareClassicKeyType>();

const QHash<int,QString> icManufacturerDB = {
    {0x01, "Motorola (UK)"},
    {0x02, "STMicroelectronics SA (FR)"},
    {0x03, "Hitachi Ltd (JP)"},
    {0x04, "NXP Semiconductors (DE)"},
    {0x05, "Infineon Technologies AG (DE)"},
    {0x06, "Cylink (US)"},
    {0x07, "Texas Instruments (FR)"},
    {0x08, "Fujitsu Limited (JP)"},
    {0x09, "Matsushita Electronics Corporation, Semiconductor Company (JP)"},
    {0x0A, "NEC (JP)"},
    {0x0B, "Oki Electric Industry Co Ltd (JP)"},
    {0x0C, "Toshiba Corp (JP)"},
    {0x0D, "Mitsubishi Electric Corp (JP)"},
    {0x0E, "Samsung Electronics Co Ltd (KR)"},
    {0x0F, "Hynix (KR)"},
    {0x10, "LG-Semiconductors Co Ltd (KR)"},
    {0x11, "Emosyn-EM Microelectronics (US)"},
    {0x12, "INSIDE Technology (FR)"},
    {0x13, "ORGA Kartensysteme GmbH (DE)"},
    {0x14, "Sharp Corporation (JP)"},
    {0x15, "ATMEL (FR)"},
    {0x16, "EM Microelectronic-Marin (CH)"},
    {0x17, "SMARTRAC TECHNOLOGY GmbH (DE)"},
    {0x18, "ZMD AG (DE)"},
    {0x19, "XICOR Inc (US)"},
    {0x1A, "Sony Corporation (JP)"},
    {0x1B, "Malaysia Microelectronic Solutions Sdn Bhd (MY)"},
    {0x1C, "Emosyn (US)"},
    {0x1D, "Shanghai Fudan Microelectronics Co Ltd (CN)"},
    {0x1E, "Magellan Technology Pty Limited (AU)"},
    {0x1F, "Melexis NV BO (CH)"},
    {0x20, "Renesas Technology Corp (JP)"},
    {0x21, "TAGSYS (FR)"},
    {0x22, "Transcore (US)"},
    {0x23, "Shanghai Belling Corp Ltd (CN)"},
    {0x24, "Masktech Germany GmbH (DE)"},
    {0x25, "Innovision Research and Technology Plc (UK)"},
    {0x26, "Hitachi ULSI Systems Co Ltd (JP)"},
    {0x27, "Yubico AB (SE)"},
    {0x28, "Ricoh (JP)"},
    {0x29, "ASK (FR)"},
    {0x2A, "Unicore Microsystems LLC (RU)"},
    {0x2B, "Dallas semiconductor/Maxim (US)"},
    {0x2C, "Impinj Inc (US)"},
    {0x2D, "RightPlug Alliance (US)"},
    {0x2E, "Broadcom Corporation (US)"},
    {0x2F, "MStar Semiconductor Inc (TW)"},
    {0x30, "BeeDar Technology Inc (US)"},
    {0x31, "RFIDsec (DK)"},
    {0x32, "Schweizer Electronic AG (DE)"},
    {0x33, "AMIC Technology Corp (TW)"},
    {0x34, "Mikron JSC (RU)"},
    {0x35, "Fraunhofer Institute for Photonic Microsystems (DE)"},
    {0x36, "IDS Microship AG (CH)"},
    {0x37, "Kovio (US)"},
    {0x38, "HMT Microelectronic Ltd (CH)"},
    {0x39, "Silicon Craft Technology (TH)"},
    {0x3A, "Advanced Film Device Inc. (JP)"},
    {0x3B, "Nitecrest Ltd (UK)"},
    {0x3C, "Verayo Inc. (US)"},
    {0x3D, "HID Global (US)"},
    {0x3E, "Productivity Engineering Gmbh (DE)"},
    {0x3F, "Austriamicrosystems AG (reserved) (AT)"},
    {0x40, "Gemalto SA (FR)"},
    {0x41, "Renesas Electronics Corporation (JP)"},
    {0x42, "3Alogics Inc (KR)"},
    {0x43, "Top TroniQ Asia Limited (Hong Kong)"},
    {0x44, "Gentag Inc (USA)"},
    {0x45, "Invengo Information Technology Co.Ltd (CN)"},
    {0x46, "Guangzhou Sysur Microelectronics, Inc (CN)"},
    {0x47, "CEITEC S.A. (BR)"},
    {0x48, "Shanghai Quanray Electronics Co. Ltd. (CN)"},
    {0x49, "MediaTek Inc (TW)"},
    {0x4A, "Angstrem PJSC (RU)"},
    {0x4B, "Celisic Semiconductor (Hong Kong) Limited (CN)"},
    {0x4C, "LEGIC Identsystems AG (CH)"},
    {0x4D, "Balluff GmbH (DE)"},
    {0x4E, "Oberthur Technologies (FR)"},
    {0x4F, "Silterra Malaysia Sdn. Bhd. (MY)"},
    {0x50, "DELTA Danish Electronics, Light & Acoustics (DK)"},
    {0x51, "Giesecke & Devrient GmbH (DE)"},
    {0x52, "Shenzhen China Vision Microelectronics Co., Ltd. (CN)"},
    {0x53, "Shanghai Feiju Microelectronics Co. Ltd. (CN)"},
    {0x54, "Intel Corporation (US)"},
    {0x55, "Microsensys GmbH (DE)"},
    {0x56, "Sonix Technology Co., Ltd. (TW)"},
    {0x57, "Qualcomm Technologies Inc (US)"},
    {0x58, "Realtek Semiconductor Corp (TW)"},
    {0x59, "Freevision Technologies Co. Ltd (CN)"},
    {0x5A, "Giantec Semiconductor Inc. (CN)"},
    {0x5B, "JSC Angstrem-T (RU)"},
    {0x5C, "STARCHIP France"},
    {0x5D, "SPIRTECH (FR)"},
    {0x5E, "GANTNER Electronic GmbH (AT)"},
    {0x5F, "Nordic Semiconductor (NO)"},
    {0x60, "Verisiti Inc (US)"},
    {0x61, "Wearlinks Technology Inc. (CN)"},
    {0x62, "Userstar Information Systems Co., Ltd (TW)"},
    {0x63, "Pragmatic Printing Ltd. (UK)"},
    {0x64, "Associação do Laboratório de Sistemas Integráveis Tecnológico – LSI-TEC (BR)"},
    {0x65, "Tendyron Corporation (CN)"},
    {0x66, "MUTO Smart Co., Ltd.(KR)"},
    {0x67, "ON Semiconductor (US)"},
    {0x68, "TÜBİTAK BİLGEM (TR)"},
    {0x69, "Huada Semiconductor Co., Ltd (CN)"},
    {0x6A, "SEVENEY (FR)"},
    {0x6B, "ISSM (FR)"},
    {0x6C, "Wisesec Ltd (IL)"},
    {0x7E, "Holtek (TW)"},
};

const QStringList mifareCardsNames = QStringList()
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Classic 1K")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Classic 4K")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Classic Mini")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Ultralight")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Ultralight-C")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Ultralight EV1 (80 bytes)")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Ultralight EV1 (164 bytes)")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Plus S 2K (SL1)")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Plus S 4K (SL1)")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare DESFire")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare DESFire")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare DESFire")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Classic 2K")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare PlusX 2K (SL1)")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare PlusX 4K (SL1)")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Plus X (SL0)")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Plus X 2K (SL2)")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Plus X 4K (SL2)")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Plus X (SL3)")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Plus S (SL0)")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Plus S 2K (SL2)")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Plus S 4K (SL2)")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Plus S (SL3)")
                                     <<QT_TRANSLATE_NOOP("MifareCards","NTAG 213")
                                     <<QT_TRANSLATE_NOOP("MifareCards","NTAG 215")
                                     <<QT_TRANSLATE_NOOP("MifareCards","NTAG 216")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Ultralight Nano")
                                     <<QT_TRANSLATE_NOOP("MifareCards","EM4100-compatible")
                                     <<QT_TRANSLATE_NOOP("MifareCards","NTAG 413")
                                     <<QT_TRANSLATE_NOOP("MifareCards","NTAG 424")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Plus X 4K (SL3)")
                                     <<QT_TRANSLATE_NOOP("MifareCards","Mifare Plus S 4K (SL3)")
                                     <<QT_TRANSLATE_NOOP("MifareCards","ISO 14443-4 / ISO 7816-4")
                                     <<QT_TRANSLATE_NOOP("MifareCards","HID Prox");
