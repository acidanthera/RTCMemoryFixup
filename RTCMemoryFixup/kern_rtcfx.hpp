//
//  kern_rtcfx.hpp
//  RTCMemoryFixup
//
//  Copyright © 2020 lvs1974. All rights reserved.
//

#ifndef kern_rtcfx_hpp
#define kern_rtcfx_hpp
#include <Headers/kern_efi.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_nvram.hpp>

#define CMOS_ADDREG1     0x70
#define CMOS_DATAREG1    0x71
#define CMOS_ADDREG2     0x72
#define CMOS_DATAREG2    0x73

#define RTC_ADDRESS_SECONDS            0x00  // R/W  Range 0..59
#define RTC_ADDRESS_SECONDS_ALARM      0x01  // R/W  Range 0..59
#define RTC_ADDRESS_MINUTES            0x02  // R/W  Range 0..59
#define RTC_ADDRESS_MINUTES_ALARM      0x03  // R/W  Range 0..59
#define RTC_ADDRESS_HOURS              0x04  // R/W  Range 1..12 or 0..23 Bit 7 is AM/PM
#define RTC_ADDRESS_HOURS_ALARM        0x05  // R/W  Range 1..12 or 0..23 Bit 7 is AM/PM
#define RTC_ADDRESS_DAY_OF_THE_WEEK    0x06  // R/W  Range 1..7
#define RTC_ADDRESS_DAY_OF_THE_MONTH   0x07  // R/W  Range 1..31
#define RTC_ADDRESS_MONTH              0x08  // R/W  Range 1..12
#define RTC_ADDRESS_YEAR               0x09  // R/W  Range 0..99
#define RTC_ADDRESS_REGISTER_A         0x0A  // R/W[0..6]  R0[7]
#define RTC_ADDRESS_REGISTER_B         0x0B  // R/W
#define RTC_ADDRESS_REGISTER_C         0x0C  // RO
#define RTC_ADDRESS_REGISTER_D         0x0D  // RO

// May be set to 01 FE /usr/libexec/efiupdater, which is DefaultBackgroundColor
#define APPLERTC_BG_COLOUR_ADDR1       0x30
#define APPLERTC_BG_COLOUR_ADDR2       0x31

#define APPLERTC_HASHED_ADDR           0x0E  // Checksum is calculated starting from this address.
#define APPLERTC_TOTAL_SIZE            0x100 // All Apple hardware cares about 256 bytes of RTC memory
#define APPLERTC_CHECKSUM_ADDR1        0x58
#define APPLERTC_CHECKSUM_ADDR2        0x59

#define APPLERTC_BOOT_STATUS_ADDR      0x5C // 0x5 - Exit Boot Services, 0x4 - recovery? (AppleBds.efi)

#define APPLERTC_HIBERNATION_KEY_ADDR  0x80 // Contents of IOHibernateRTCVariables
#define APPLERTC_HIBERNATION_KEY_LEN   0x2C // sizeof (AppleRTCHibernateVars)

// 0x00 - after memory check if AppleSmcIo and AppleRtcRam are present (AppleBds.efi)
// 0x02 - when blessing to recovery for EFI firmware update (closed source bless)
#define APPLERTC_BLESS_BOOT_TARGET     0xAC // used for boot overrides at Apple
#define APPLERTC_RECOVERYCHECK_STATUS  0xAF // 0x0 - at booting into recovery? connected to 0x5C

#define APPLERTC_POWER_BYTES_ADDR      0xB0 // used by /usr/bin/pmset
// Valid values could be found in stringForPMCode (https://opensource.apple.com/source/PowerManagement/PowerManagement-494.30.1/pmconfigd/PrivateLib.c.auto.html)
#define APPLERTC_POWER_BYTE_PM_ADDR    0xB4 // special PM byte describing Power State
#define APPLERTC_POWER_BYTES_LEN       0x08

class RTCFX
{
public:
    bool init();
    void deinit();
    
private:
    /**
     *  Patch kext if needed and prepare other patches
     *
     *  @param patcher KernelPatcher instance
     *  @param index   kinfo handle
     *  @param address kinfo load address
     *  @param size    kinfo memory size
     */
    void processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
    
    
    using t_rtc_read = uint8_t (*)(void *, uint32_t Address);
    using t_rtc_write = void (*)(void *, uint32_t Address, uint8_t ByteToWrite);

    
    /**
     *  Hooked methods / callbacks
     */
    static uint8_t rtcRead(void *AppleRTC, uint32_t Address);
    static void rtcWrite(void *AppleRTC, uint32_t Address, uint8_t ByteToWrite);
    
    /**
     *  Trampolines for original method invocations
     */
    t_rtc_read  orgRtcRead {nullptr};
    t_rtc_write orgRtcWrite {nullptr};
    
    enum { RTC_SIZE = 256 };
    static UInt8                        emulated_rtc_mem[RTC_SIZE];
    static bool                         emulated_flag[RTC_SIZE];

	bool excludeOffsetsInstalled {false};
    bool excludeAddresses(char* rtcfx_exclude);
    void readAndApplyRtcBlacklistFromNvram(bool suppress_errors);
    
    /**
     *  Current progress mask
     */
    struct ProcessingState {
        enum {
            NothingReady = 0,
            rtcReadRouted = 1,
            rtcWriteRouted = 2,
            EverythingDone = rtcReadRouted | rtcWriteRouted,
        };
    };
    int progressState {ProcessingState::NothingReady};
};


#endif  // kern_rtcfx_hpp
