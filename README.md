RTCMemoryFixup
==================

An open source kernel extension providing a way to emulate some offsets in your CMOS (RTC) memory.
It can help you to avoid some conflicts between osx AppleRTC and firmware/BIOS of your PC.
It can also help you to find out which at offsets you have a conflict. In most cases it is enough to boot with some
offsets in boot-args, perform sleep, wake and reboot. If you don't see any CMOS errors or some unexpected reboots,  
it means you have managed to exclude conflicted CMOS offsets.
Offsets in boot-args  `rtcfx_exclude` can have value from 00 to FF (wihout prefix 0x)
Be careful: offsets from 0 to 0D usually are more or less 'compatible' and should not cause conflicts.
Offsets from 0x80 to 0xAB are used to store some hibernation information (IOHibernateRTCVariables),
if any offset in this range causes a conflict, you can exclude it, but hibernation won't work.
In my case it was only the one offset: B2. B0 - B4 offsets are used for PowerManagement features, but they don't work on hacks anyway)


#### Features
- 

#### Boot-args
- `rtcfx_exclude=offset1,offset2,start_offset-end_offset...` -  list of offsets or ranges of offsets where writing is not allowed
- `-rtcfxdbg` turns on debugging output

#### Credits
- [Apple](https://www.apple.com) for macOS  
- [vit9696](https://github.com/vit9696) for [Lilu.kext](https://github.com/vit9696/Lilu) and great help in implementing some features 
- [lvs1974](https://applelife.ru/members/lvs1974.53809/) for writing the software and maintaining it


