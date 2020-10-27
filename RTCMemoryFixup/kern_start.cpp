//
//  kern_stat.cpp
//  RTCMemoryFixup
//
//  Copyright © 2020 lvs1974. All rights reserved.
//

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>

#include "kern_config.hpp"
#include "kern_rtcfx.hpp"

static RTCFX rtcfx;

const char *Configuration::bootargOff[] {
    "-rtcfxoff"
};

const char *Configuration::bootargDebug[] {
    "-rtcfxdbg"
};

const char *Configuration::bootargBeta[] {
    "-rtcfxbeta"
};

Configuration ADDPR(rtcfx_config);

void Configuration::readArguments() {
    if (PE_parse_boot_argn(bootargRtcfxExclude, rtcfx_exclude, sizeof(rtcfx_exclude)))
    {
        DBGLOG("RTCFX", "boot-arg rtcfx_exclude specified, value = %s", rtcfx_exclude);
    }
}

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery | LiluAPI::AllowSafeMode,
    ADDPR(rtcfx_config).bootargOff,
    arrsize(ADDPR(rtcfx_config).bootargOff),
    ADDPR(rtcfx_config).bootargDebug,
    arrsize(ADDPR(rtcfx_config).bootargDebug),
    ADDPR(rtcfx_config).bootargBeta,
    arrsize(ADDPR(rtcfx_config).bootargBeta),
    KernelVersion::MountainLion,
    KernelVersion::BigSur,
    []() {
        ADDPR(rtcfx_config).readArguments();
        rtcfx.init();
    }
};
