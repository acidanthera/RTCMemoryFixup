//
//  kern_config.hpp
//  RTCMemoryFixup
//
//  Copyright © 2020 lvs1974. All rights reserved.
//

#ifndef kern_config_private_h
#define kern_config_private_h

#include <Headers/kern_util.hpp>

class Configuration {
public:
    /**
     *  Possible boot arguments
     */
    static const char *bootargOff[];
    static const char *bootargDebug[];
    static const char *bootargBeta[];
    static constexpr const char *bootargRtcfxExclude  {"rtcfx_exclude"};            //
public:
    /**
     *  Retrieve boot arguments
     */
    void readArguments();
	
    /**
     * Size of rtcfx_exclude
     */
    static constexpr size_t rtcfx_exclude_size = 512;
    
    /**
     *  List of offsets or ranges of offsets where writing is not allowed
     */
    char rtcfx_exclude[rtcfx_exclude_size];

    Configuration() = default;
};

extern Configuration ADDPR(rtcfx_config);

#endif /* kern_config_private_h */
