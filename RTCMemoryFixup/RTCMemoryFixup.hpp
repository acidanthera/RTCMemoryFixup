//
//  RTCMemoryFixup.hpp
//  RTCMemoryFixup
//
//  Copyright © 2018 lvs1974. All rights reserved.
//

#ifndef RTCMemoryFixup_h
#define RTCMemoryFixup_h

#define CMOS_ADDREG1     0x70
#define CMOS_DATAREG1    0x71
#define CMOS_ADDREG2     0x72
#define CMOS_DATAREG2    0x73

class EXPORT RTCMemoryFixup : public IOService
{
    OSDeclareDefaultStructors(RTCMemoryFixup);
    typedef IOService super;
    
public:
    virtual bool init(OSDictionary *propTable) override;
    virtual bool attach(IOService *provider) override;
    virtual IOService* probe(IOService * provider, SInt32 *score) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    virtual void free() override;
    
private:
    static void  hookProvider(IOService* provider);
    
    using t_io_read8 = UInt8 (*)(IOService * that, UInt16 offset, IOMemoryMap * map);
    using t_io_write8 = void (*)(IOService * that, UInt16 offset, UInt8 value, IOMemoryMap * map);
    
    
    static UInt8 ioRead8(IOService * that, UInt16 offset, IOMemoryMap * map);
    static void  ioWrite8(IOService * that, UInt16 offset, UInt8 value, IOMemoryMap * map);
    
    /**
     *  Fixed offsets for I/O port access virtual methods
     */
    struct IOPortAccessOffset {
        enum : size_t {
            ioRead8      = 0x978/8,
            ioWrite8     = 0x960/8,
        };
    };
    
private:
    static IOService                    *service_provider;
    static t_io_read8                   orgIoRead8;
    static t_io_write8                  orgIoWrite8;
    
    static UInt16                       cmd_reg;
    static UInt16                       cmd_offset;
    
    enum { RTC_SIZE = 256 };
    static UInt8                        emulated_rtc_mem[RTC_SIZE];
    static bool                         emulated_flag[RTC_SIZE];
};


#endif  // RTCMemoryFixup_h
