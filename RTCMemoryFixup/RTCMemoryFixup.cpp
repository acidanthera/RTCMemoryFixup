//
//  RTCMemoryFixup.cpp
//  RTCMemoryFixup
//
//  Copyright © 2020 lvs1974. All rights reserved.
//

#include <Library/LegacyIOService.h>
#include <Headers/kern_efi.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_nvram.hpp>

#include "RTCMemoryFixup.hpp"

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

static constexpr const char     *OcRtcBlacklistKey     {NVRAM_PREFIX(LILU_VENDOR_GUID, "rtc-blacklist")};
static constexpr const char16_t *OcRtcBlacklistKeyEfi  {u"rtc-blacklist"};

OSDefineMetaClassAndStructors(RTCMemoryFixup, IOService);

RTCMemoryFixup::t_io_read8  RTCMemoryFixup::orgIoRead8       {nullptr};
RTCMemoryFixup::t_io_write8 RTCMemoryFixup::orgIoWrite8      {nullptr};

UInt16                      RTCMemoryFixup::cmd_reg          {0xFFFF};
UInt16                      RTCMemoryFixup::cmd_offset       {0xFFFF};

UInt8                       RTCMemoryFixup::emulated_rtc_mem[RTC_SIZE] {};
bool                        RTCMemoryFixup::emulated_flag[RTC_SIZE]    {};

//==============================================================================

bool RTCMemoryFixup::init(OSDictionary *propTable)
{
    ADDPR(debugEnabled) = checkKernelArgument("-rtcfxdbg") || checkKernelArgument("-liludbgall");
    PE_parse_boot_argn("liludelay", &ADDPR(debugPrintDelay), sizeof(ADDPR(debugPrintDelay)));

    DBGLOG("RTCFX", "RTCMemoryFixup::init()");
    
    bool ret = super::init(propTable);
    if (!ret)
    {
        SYSLOG("RTCFX", "RTCMemoryFixup super::init returned false\n");
        return false;
    }

    memset(emulated_rtc_mem, 0, sizeof(emulated_rtc_mem));
    memset(emulated_flag, 0, sizeof(emulated_flag));
    
    return true;
}

//==============================================================================

bool RTCMemoryFixup::attach(IOService *provider)
{
    hookProvider(provider);
    return super::attach(provider);
}

//==============================================================================

IOService* RTCMemoryFixup::probe(IOService * provider, SInt32 *score)
{
    DBGLOG("RTCFX", "RTCMemoryFixup::probe()");
    
    IOService* ret = super::probe(provider, score);
    if (!ret)
    {
        SYSLOG("RTCFX", "RTCMemoryFixup super::probe returned nullptr\n");
        return nullptr;
    }
    
    *score = 20000;
    
    DBGLOG("RTCFX", "RTCMemoryFixup::probe(): service provider is %s", provider->getName());
    
    return ret;
}

//==============================================================================

bool RTCMemoryFixup::start(IOService *provider)
{
    DBGLOG("RTCFX", "RTCMemoryFixup::start()");
    
    if (!super::start(provider))
    {
        SYSLOG("RTCFX", "RTCMemoryFixup super::start returned false\n");
        return false;
    }
    
    retain();

    hookProvider(provider);
    
    return false;
}

//==============================================================================

void RTCMemoryFixup::stop(IOService *provider)
{
    DBGLOG("RTCFX", "RTCMemoryFixup::stop() ignored");
    //super::stop(provider);
}

//==============================================================================

void RTCMemoryFixup::free()
{
    DBGLOG("RTCFX", "RTCMemoryFixup::free() ignored");
    //super::free();
}

//==============================================================================

UInt8 RTCMemoryFixup::ioRead8(IOService * that, UInt16 offset, IOMemoryMap * map)
{
    UInt8 result = orgIoRead8(that, offset, map);
    
    if (offset != CMOS_DATAREG1 && offset != CMOS_DATAREG2)
        return result;
    
    if (cmd_reg == CMOS_ADDREG1 || cmd_reg == CMOS_ADDREG2)
    {
        //DBGLOG("RTCFX", "RTCMemoryFixup::ioRead8 read cmd_reg = %02X, cmd_offset = %02X, offset = %02X", cmd_reg, cmd_offset, offset);
        int safe_offset = (cmd_reg == CMOS_ADDREG2) ? 0x80 : 0;
        safe_offset += (cmd_offset & 0x7F);
        if (emulated_flag[safe_offset])
        {
            result = emulated_rtc_mem[safe_offset];
        }
        cmd_reg = 0xFFFF;
    }
    else
    {
        //DBGLOG("RTCFX", "RTCMemoryFixup::ioRead8 unexpected values: cmd_reg = %02X, cmd_offset = %02X, offset = %02X", cmd_reg, cmd_offset, offset);
    }
    
    return result;
}

//==============================================================================

void RTCMemoryFixup::ioWrite8(IOService * that, UInt16 offset, UInt8 value, IOMemoryMap * map)
{
    if (offset != CMOS_ADDREG1 && offset != CMOS_DATAREG1 && offset != CMOS_ADDREG2 && offset != CMOS_DATAREG2)
    {
        orgIoWrite8(that, offset, value, map);
        return;
    }
    
    bool write_enabled = true;
    if (offset == CMOS_ADDREG1 || offset == CMOS_ADDREG2)
    {
        cmd_reg = offset;
        cmd_offset = value;
        //DBGLOG("RTCFX", "RTCMemoryFixup::ioWrite8 request cmd_reg = %02X, cmd_offset = %02X", cmd_reg, cmd_offset);
    }
    else if (cmd_reg != 0xFFFF)
    {
        if ((cmd_reg == CMOS_ADDREG1 && offset == CMOS_DATAREG1) || (cmd_reg == CMOS_ADDREG2 && offset == CMOS_DATAREG2))
        {
            int safe_offset = (cmd_reg == CMOS_ADDREG2) ? 0x80 : 0;
            safe_offset += (cmd_offset & 0x7F);
            if (emulated_flag[safe_offset])
            {
                emulated_rtc_mem[safe_offset] = value;
                write_enabled = false;
                //DBGLOG("RTCFX", "RTCMemoryFixup::ioWrite8 rejects write to cmd_reg = %02X, cmd_offset = %02X, offset = %02X, value = %02X", cmd_reg, cmd_offset, offset, value);
            }
        }
        
        cmd_reg = 0xFFFF;
    }
    
    if (write_enabled)
        orgIoWrite8(that, offset, value, map);
    else
        orgIoRead8(that, value, map);
}

//==============================================================================

bool RTCMemoryFixup::excludeAddresses(char* rtcfx_exclude)
{
    bool result = false;
    char *tok = rtcfx_exclude, *end = rtcfx_exclude;
    char *dash = nullptr;
    while (tok != nullptr)
    {
        strsep(&end, ",");
        DBGLOG("RTCFX", "rtc offset token = %s", tok);
        if ((dash = strchr(tok, '-')) == nullptr)
        {
            unsigned int offset = RTC_SIZE;
            if (sscanf(tok, "%02X", &offset) != 1)
                break;
            if (offset >= RTC_SIZE)
            {
                DBGLOG("RTCFX", "rtc offset %02X is not valid", offset);
                break;
            }
            result = emulated_flag[offset] = true;
            DBGLOG("RTCFX", "rtc offset %02X is marked as emulated", offset);
        }
        else
        {
            unsigned int soffset = RTC_SIZE, eoffset = RTC_SIZE;
            char *rstart = tok, *rend = dash+1;
            *dash = '\0';
            if (sscanf(rstart, "%02X", &soffset) == 1 && sscanf(rend, "%02X", &eoffset) == 1)
            {
                if (soffset >= RTC_SIZE)
                {
                    DBGLOG("RTCFX", "rtc start offset %02X is not valid", soffset);
                    break;
                }
                
                if (eoffset >= RTC_SIZE)
                {
                    DBGLOG("RTCFX", "rtc end offset %02X is not valid", eoffset);
                    break;
                }
                
                if (soffset >= eoffset)
                {
                    DBGLOG("RTCFX", "rtc start offset %02X must be less than end offset %02X", soffset, eoffset);
                    break;
                }
                
                for (unsigned int i = soffset; i <= eoffset; ++i)
                    result = emulated_flag[i] = true;
                DBGLOG("RTCFX", "rtc range from offset %02X to offset %02X is marked as emulated", soffset, eoffset);
            }
            else
            {
                DBGLOG("RTCFX", "boot-arg rtcfx_exclude can't be parsed properly");
                break;
            }
        }
        
        tok = end;
    }
    return result;
}

//==============================================================================

void RTCMemoryFixup::hookProvider(IOService *provider)
{
    if (orgIoRead8 == nullptr || orgIoWrite8 == nullptr)
    {
        bool suppress_errors = false;
        OSData* data = nullptr;
        static constexpr size_t rtcfx_exclude_size = 512;
        auto rtcfx_exclude_tmp = Buffer::create<char>(rtcfx_exclude_size);
        if (rtcfx_exclude_tmp && PE_parse_boot_argn("rtcfx_exclude", rtcfx_exclude_tmp, rtcfx_exclude_size))
        {
            DBGLOG("RTCFX", "boot-arg rtcfx_exclude specified, value = %s", rtcfx_exclude_tmp);
            suppress_errors = excludeAddresses(rtcfx_exclude_tmp);
        }
        else if (rtcfx_exclude_tmp && (data = OSDynamicCast(OSData, provider->getProperty("rtcfx_exclude"))) != nullptr)
        {
            if (data->getLength() < rtcfx_exclude_size)
            {
                lilu_os_strncpy(rtcfx_exclude_tmp, reinterpret_cast<const char*>(data->getBytesNoCopy()), data->getLength());
                DBGLOG("RTCFX", "property rtcfx_exclude specified, value = %s", rtcfx_exclude_tmp);
                suppress_errors = excludeAddresses(rtcfx_exclude_tmp);
            }
            else
            {
                SYSLOG("RTCFX", "RTCMemoryFixup::hookProvider: length of rtcfx_exclude cannot excceed 512 bytes");
            }
        }

        readAndApplyRtcBlacklistFromNvram(suppress_errors);
        
        if (rtcfx_exclude_tmp)
            Buffer::deleter(rtcfx_exclude_tmp);
    }

    if (orgIoRead8 == nullptr)
    {
        if (KernelPatcher::routeVirtual(provider, IOPortAccessOffset::ioRead8, ioRead8, &orgIoRead8))
            DBGLOG("RTCFX", "RTCMemoryFixup::hookProvider for ioRead8 was successful");
        else
            SYSLOG("RTCFX", "RTCMemoryFixup::hookProvider for ioRead8 was failed");
    }
    
    if (orgIoWrite8 == nullptr)
    {
        if (KernelPatcher::routeVirtual(provider, IOPortAccessOffset::ioWrite8, ioWrite8, &orgIoWrite8))
            DBGLOG("RTCFX", "RTCMemoryFixup::hookProvider for ioWrite8 was successful");
        else
            SYSLOG("RTCFX", "RTCMemoryFixup::hookProvider for ioWrite8 was failed");
    }
}

//==============================================================================

void RTCMemoryFixup::readAndApplyRtcBlacklistFromNvram(bool suppress_errors) {
    NVStorage storage;
    if (storage.init()) {
        uint32_t size = 0;
        auto buf = storage.read(OcRtcBlacklistKey, size, NVStorage::OptRaw);
        if (buf) {
            if (size > RTC_SIZE)
                size = RTC_SIZE;
            for (unsigned int i=0; i<size; ++i)
                emulated_flag[buf[i]] = true;
            DBGLOG("RTCFX", "successfully got %u bytes from nvram for key %s", size, OcRtcBlacklistKey);
            Buffer::deleter(buf);
        }
        else if (!suppress_errors || ADDPR(debugEnabled))
            SYSLOG("RTCFX", "failed to load rtc-blacklist config from nvram");

        storage.deinit();
    } else {
        // Otherwise use EFI services if available.
        auto rt = EfiRuntimeServices::get(true);
        if (rt) {
            uint64_t size = RTC_SIZE;
            auto buf = Buffer::create<uint8_t>(size);
            if (buf) {
                uint32_t attr = 0;
                auto status = rt->getVariable(OcRtcBlacklistKeyEfi, &EfiRuntimeServices::LiluVendorGuid, &attr, &size, buf);
                if (status == EFI_SUCCESS) {
                    if (size > RTC_SIZE)
                        size = RTC_SIZE;
                    for (unsigned int i=0; i<size; ++i)
                        emulated_flag[buf[i]] = true;
                    DBGLOG("RTCFX", "successfully got %u bytes from UEFI nvram for key %s", size, OcRtcBlacklistKey);
                }
                else if (!suppress_errors || ADDPR(debugEnabled))
                    SYSLOG("RTCFX", "failed to load rtc-blacklist config from UEFI nvram");
                Buffer::deleter(buf);
            }
            else
                SYSLOG("RTCFX", "failed to create temporary buffer");
            rt->put();
        } else
            SYSLOG("RTCFX", "failed to load efi rt services for rtc-blacklist");
    }
}
