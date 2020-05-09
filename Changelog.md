RTCMemoryFixup Changelog
============================
#### v.1.0.6
- Fix reading of key rtc-blacklist from NVRAM (only 4 bytes could be read)
- rtcfx_exclude can be combined with rtc-blacklist

#### v1.0.5
- Support key rtc-blacklist set by OpenCore in NVRAM

#### v1.0.4
- Unified release archive names

#### v1.0.3
- Fixed compiling and loading on older OS
- Fixed loading from /Library/Extensions
- rtcfx_exclude property is read as string from RTC provider's properties

#### v1.0.2
- Wrong range start value in debug message has been fixed (thanks to nms42)

#### v1.0.1
- Bug fix: prevent kext unloading 

#### v1.0.0
- Initial release
