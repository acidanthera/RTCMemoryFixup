//
//  kern_rtcfx.cpp
//  RTCMemoryFixup
//
//  Copyright © 2020 lvs1974. All rights reserved.
//

#include <Headers/kern_api.hpp>
#include <Headers/kern_cpu.hpp>
#include <Headers/kern_iokit.hpp>
#include <Headers/plugin_start.hpp>

#include "kern_config.hpp"
#include "kern_rtcfx.hpp"

static constexpr const char     *OcRtcBlacklistKey                {NVRAM_PREFIX(LILU_VENDOR_GUID, "rtc-blacklist")};
static constexpr const char16_t *OcRtcBlacklistKeyEfi             {u"rtc-blacklist"};
UInt8                           RTCFX::emulated_rtc_mem[RTC_SIZE] {};
bool                            RTCFX::emulated_flag[RTC_SIZE]    {};

// Only used in apple-driven callbacks
static RTCFX *callbackRtcfx = nullptr;
static KernelPatcher *callbackPatcher = nullptr;

static const char *kextAppleRTCPath[]          { "/System/Library/Extensions/AppleRTC.kext/Contents/MacOS/AppleRTC" };

static KernelPatcher::KextInfo kextList[] {
    {
        "com.apple.driver.AppleRTC",
        kextAppleRTCPath,
        arrsize(kextAppleRTCPath),
        {true},
        {},
        KernelPatcher::KextInfo::Unloaded
    }
};

enum : size_t {
    KextAppleRTCDriver
};

static size_t kextListSize = arrsize(kextList);


bool RTCFX::init() {
	bool suppress_errors = false;
	
	DBGLOG("RTCFX", "init() called");

	
	memset(emulated_rtc_mem, 0, RTC_SIZE);
	memset(emulated_flag, 0, RTC_SIZE);
	suppress_errors = excludeAddresses(ADDPR(rtcfx_config).rtcfx_exclude);
	readAndApplyRtcBlacklistFromNvram(suppress_errors);
	excludeOffsetsInstalled = true;

    
    callbackRtcfx = this;

    auto error = lilu.onKextLoad(kextList, kextListSize, [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
        callbackPatcher = &patcher;
        callbackRtcfx->processKext(patcher, index, address, size);
    }, this);
    

    if (error != LiluAPI::Error::NoError) {
        SYSLOG("RTCFX", "failed to register onKextLoad method %d", error);
        return false;
    }

    return true;
}


void RTCFX::deinit() {
}


bool RTCFX::excludeAddresses(char* rtcfx_exclude)
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


void RTCFX::readAndApplyRtcBlacklistFromNvram(bool suppress_errors) {
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


uint8_t RTCFX::rtcRead(void *AppleRTC, uint32_t Address) {
    if (emulated_flag[Address]){
        return emulated_rtc_mem[Address];
    }
    
    if (callbackRtcfx && callbackPatcher && callbackRtcfx->orgRtcRead) {
        DBGLOG("RTCFX", "rtcRead address = %d", Address);
        return callbackRtcfx->orgRtcRead(AppleRTC, Address);
    }

	DBGLOG("RTCFX", "rtcRead callback arrived at nowhere");
    return 0u;
}


void RTCFX::rtcWrite(void *AppleRTC, uint32_t Address, uint8_t ByteToWrite) {
    if (emulated_flag[Address]){
        emulated_rtc_mem[Address] = ByteToWrite;
        return;
    }
    
    if (callbackRtcfx && callbackPatcher && callbackRtcfx->orgRtcWrite) {
		DBGLOG("RTCFX", "rtcWrite address = %d , byte = %02x ", Address, ByteToWrite);
        return callbackRtcfx->orgRtcWrite(AppleRTC, Address, ByteToWrite);
    }

	DBGLOG("RTCFX", "rtcWrite callback arrived at nowhere");
}


void RTCFX::processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    if (progressState != ProcessingState::EverythingDone) {
        for (size_t i = 0; i < kextListSize; i++) {
            if (kextList[i].loadIndex == index) {
                DBGLOG("RTCFX", "found %s (%d)", kextList[i].id, progressState);

                if (!(progressState & ProcessingState::rtcReadRouted)) {
                    mach_vm_address_t rtcReadCallback = 0;

                    rtcReadCallback = patcher.solveSymbol(index, "__ZN8AppleRTC7rtcReadEj", address, size);
                    
                    if (rtcReadCallback) {
                        DBGLOG("RTCFX", "obtained rtcReadCallback");
                        patcher.clearError();
                        orgRtcRead = reinterpret_cast<t_rtc_read>(patcher.routeFunction(rtcReadCallback, reinterpret_cast<mach_vm_address_t>(rtcRead), true));
                        if (patcher.getError() == KernelPatcher::Error::NoError)
                            DBGLOG("RTCFX", "routed rtcRead method");
                        else
                            SYSLOG("RTCFX", "failed to route rtcRead");
                    } else {
                        SYSLOG("RTCFX", "failed to resolve rtcRead");
                    }
                    progressState |= ProcessingState::rtcReadRouted;
                }
                if (!(progressState & ProcessingState::rtcWriteRouted)) {
                    mach_vm_address_t rtcWriteCallback = 0;

                    rtcWriteCallback = patcher.solveSymbol(index, "__ZN8AppleRTC8rtcWriteEjh", address, size);
                    
                    if (rtcWriteCallback) {
                        DBGLOG("RTCFX", "obtained rtcWriteCallback");
                        patcher.clearError();
                        orgRtcWrite = reinterpret_cast<t_rtc_write>(patcher.routeFunction(rtcWriteCallback, reinterpret_cast<mach_vm_address_t>(rtcWrite), true));
                        if (patcher.getError() == KernelPatcher::Error::NoError)
                            DBGLOG("RTCFX", "routed rtcWrite method");
                        else
                            SYSLOG("RTCFX", "failed to route rtcWrite");
                    } else {
                        SYSLOG("RTCFX", "failed to resolve rtcWrite");
                    }
                    progressState |= ProcessingState::rtcWriteRouted;
                }
                
            }
        }
    }

    // Ignore all the errors for other processors
    patcher.clearError();
}
