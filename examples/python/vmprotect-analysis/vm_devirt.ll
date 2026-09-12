; devirtualized from the recovered VM CFG
define i32 @check_serial_devirt(ptr %serial) {
entry:
  %stack = alloca [1024 x i8]
  %regs  = alloca [512 x i8]
  %vsp   = alloca i64
  ; the VM stack grows down from the top of the allocation
  store i64 960, ptr %vsp
  ; check_serial's argument arrives in virtual register 0x80
  %t1 = ptrtoint ptr %serial to i64
  %t2 = getelementptr i8, ptr %regs, i64 128
  store i64 %t1, ptr %t2
  br label %vip_14018f95a

vip_1400c05d1:
  ; 0x1400c05d1  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c05d3  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c05d5  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c05d7  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c05d9  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c05db  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c05dd  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c05df  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c05e1  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c05e3  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c05e5  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c05e7  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c05e9  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c05eb  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c05ed  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c05ef  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c05f1  ? 0x86
  ; !! no emitter for ?
  ; 0x1400c05f3  ? 0xf300000000000000
  ; !! no emitter for ?
  ; 0x1400c05fc  ? 0xeb
  ; !! no emitter for ?
  ; 0x1400c05fe  ?
  ; !! no emitter for ?
  ; 0x1400c05ff  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c0601  ? 0xf7
  ; !! no emitter for ?
  ; 0x1400c0603  ? 0xb3ffffff
  ; !! no emitter for ?
  ; 0x1400c0608  ? 0x55
  ; !! no emitter for ?
  ; 0x1400c060a  ?
  ; !! no emitter for ?
  ; no recovered successor: the exploration stopped here
  ret i32 0

vip_1400c064a:
  ; 0x1400c064a  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c064c  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c064e  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c0650  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c0652  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c0654  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c0656  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c0658  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c065a  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c065c  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c065e  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c0660  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c0662  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c0664  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c0666  ? 0x4f
  ; !! no emitter for ?
  ; 0x1400c0668  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c066a  ? 0x3f
  ; !! no emitter for ?
  ; 0x1400c066c  ? 0xe200000000000000
  ; !! no emitter for ?
  ; 0x1400c0675  ? 0x25
  ; !! no emitter for ?
  ; 0x1400c0677  ?
  ; !! no emitter for ?
  ; 0x1400c0678  ? 0xbe
  ; !! no emitter for ?
  ; 0x1400c067a  ? 0xf7
  ; !! no emitter for ?
  ; 0x1400c067c  ? 0xb300ffff
  ; !! no emitter for ?
  ; 0x1400c0681  ? 0x55
  ; !! no emitter for ?
  ; 0x1400c0683  ?
  ; !! no emitter for ?
  ; no recovered successor: the exploration stopped here
  ret i32 0

vip_140103a5e:
  ; 0x140103a5e  ? 0xbe
  ; !! no emitter for ?
  ; 0x140103a60  ? 0x4f
  ; !! no emitter for ?
  ; 0x140103a62  ? 0xbe
  ; !! no emitter for ?
  ; 0x140103a64  ? 0x4f
  ; !! no emitter for ?
  ; 0x140103a66  ? 0xbe
  ; !! no emitter for ?
  ; 0x140103a68  ? 0x4f
  ; !! no emitter for ?
  ; 0x140103a6a  ? 0xbe
  ; !! no emitter for ?
  ; 0x140103a6c  ? 0x4f
  ; !! no emitter for ?
  ; 0x140103a6e  ? 0xbe
  ; !! no emitter for ?
  ; 0x140103a70  ? 0x4f
  ; !! no emitter for ?
  ; 0x140103a72  ? 0xbe
  ; !! no emitter for ?
  ; 0x140103a74  ? 0x4f
  ; !! no emitter for ?
  ; 0x140103a76  ? 0xbe
  ; !! no emitter for ?
  ; 0x140103a78  ? 0x4f
  ; !! no emitter for ?
  ; 0x140103a7a  ? 0xbe
  ; !! no emitter for ?
  ; 0x140103a7c  ? 0x4f
  ; !! no emitter for ?
  ; 0x140103a7e  ? 0x3f
  ; !! no emitter for ?
  ; 0x140103a80  ? 0x3500000000000000
  ; !! no emitter for ?
  ; 0x140103a89  ? 0x25
  ; !! no emitter for ?
  ; 0x140103a8b  ?
  ; !! no emitter for ?
  ; 0x140103a8c  ? 0x4f
  ; !! no emitter for ?
  ; 0x140103a8e  ? 0x52
  ; !! no emitter for ?
  ; 0x140103a90  ?
  ; !! no emitter for ?
  ; 0x140103a91  ? 0xcd
  ; !! no emitter for ?
  ; 0x140103a93  ? 0x4f
  ; !! no emitter for ?
  ; 0x140103a95  ? 0xe2
  ; !! no emitter for ?
  ; 0x140103a97  ? 0xbe
  ; !! no emitter for ?
  ; 0x140103a99  ? 0xef
  ; !! no emitter for ?
  ; 0x140103a9b  ? 0x4f
  ; !! no emitter for ?
  ; 0x140103a9d  ? 0xf3
  ; !! no emitter for ?
  ; 0x140103a9f  ? 0xbe
  ; !! no emitter for ?
  ; 0x140103aa1  ? 0x4f
  ; !! no emitter for ?
  ; 0x140103aa3  ? 0x60
  ; !! no emitter for ?
  ; 0x140103aa5  ?
  ; !! no emitter for ?
  ; 0x140103aa6  ? 0xf7
  ; !! no emitter for ?
  ; 0x140103aa8  ? 0xb3fffffe
  ; !! no emitter for ?
  ; 0x140103aad  ? 0xe9
  ; !! no emitter for ?
  ; 0x140103aaf  ? 0x2a
  ; !! no emitter for ?
  ; 0x140103ab1  ?
  ; !! no emitter for ?
  ; 0x140103ab2  ? 0x7a
  ; !! no emitter for ?
  ; 0x140103ab4  ?
  ; !! no emitter for ?
  ; 0x140103ab5  ? 0x49
  ; !! no emitter for ?
  ; 0x140103ab7  ? 0xf2
  ; !! no emitter for ?
  ; 0x140103ab9  ? 0x3500
  ; !! no emitter for ?
  ; 0x140103abc  ? 0x6e
  ; !! no emitter for ?
  ; 0x140103abe  ?
  ; !! no emitter for ?
  ; 0x140103abf  ? 0x86
  ; !! no emitter for ?
  ; 0x140103ac1  ? 0x80ffffffffffffff
  ; !! no emitter for ?
  ; 0x140103aca  ?
  ; !! no emitter for ?
  ; 0x140103acb  ? 0x3f
  ; !! no emitter for ?
  ; 0x140103acd  ? 0xebffffffffffffff
  ; !! no emitter for ?
  ; 0x140103ad6  ?
  ; !! no emitter for ?
  ; 0x140103ad7  ? 0xbe
  ; !! no emitter for ?
  ; 0x140103ad9  ? 0x86
  ; !! no emitter for ?
  ; 0x140103adb  ? 0xcd0000000140132d
  ; !! no emitter for ?
  ; 0x140103ae4  ? 0x25
  ; !! no emitter for ?
  ; 0x140103ae6  ?
  ; !! no emitter for ?
  ; 0x140103ae7  ? 0xe2
  ; !! no emitter for ?
  ; 0x140103ae9  ? 0xef
  ; !! no emitter for ?
  ; 0x140103aeb  ? 0x24
  ; !! no emitter for ?
  ; 0x140103aed  ?
  ; !! no emitter for ?
  ; 0x140103aee  ? 0x8d
  ; !! no emitter for ?
  ; 0x140103af0  ?
  ; !! no emitter for ?
  ; 0x140103af1  ? 0xc6
  ; !! no emitter for ?
  ; 0x140103af3  ?
  ; !! no emitter for ?
  ; 0x140103af4  ?
  ; !! no emitter for ?
  ; 0x140103af5  ?
  ; !! no emitter for ?
  ; 0x140103af6  ? 0x3f
  ; !! no emitter for ?
  ; 0x140103af8  ? 0xf30000000140171f
  ; !! no emitter for ?
  ; 0x140103b01  ? 0xeb
  ; !! no emitter for ?
  ; 0x140103b03  ?
  ; !! no emitter for ?
  ; 0x140103b04  ? 0x35
  ; !! no emitter for ?
  ; 0x140103b06  ? 0x24
  ; !! no emitter for ?
  ; 0x140103b08  ?
  ; !! no emitter for ?
  ; 0x140103b09  ? 0xd2
  ; !! no emitter for ?
  ; 0x140103b0b  ?
  ; !! no emitter for ?
  ; 0x140103b0c  ?
  ; !! no emitter for ?
  ; 0x140103b0d  ?
  ; !! no emitter for ?
  ; 0x140103b0e  ? 0x25
  ; !! no emitter for ?
  ; 0x140103b10  ?
  ; !! no emitter for ?
  ; 0x140103b11  ? 0x4f
  ; !! no emitter for ?
  ; 0x140103b13  ? 0xcd
  ; !! no emitter for ?
  ; 0x140103b15  ? 0xe2
  ; !! no emitter for ?
  ; 0x140103b17  ? 0xef
  ; !! no emitter for ?
  ; 0x140103b19  ? 0xf3
  ; !! no emitter for ?
  ; 0x140103b1b  ? 0x35
  ; !! no emitter for ?
  ; 0x140103b1d  ? 0xcd
  ; !! no emitter for ?
  ; 0x140103b1f  ? 0xe2
  ; !! no emitter for ?
  ; 0x140103b21  ? 0xef
  ; !! no emitter for ?
  ; 0x140103b23  ? 0xf3
  ; !! no emitter for ?
  ; 0x140103b25  ? 0x35
  ; !! no emitter for ?
  ; 0x140103b27  ? 0xcd
  ; !! no emitter for ?
  ; 0x140103b29  ? 0xe2
  ; !! no emitter for ?
  ; 0x140103b2b  ? 0xef
  ; !! no emitter for ?
  ; 0x140103b2d  ? 0xf3
  ; !! no emitter for ?
  ; 0x140103b2f  ? 0x35
  ; !! no emitter for ?
  ; 0x140103b31  ? 0xcd
  ; !! no emitter for ?
  ; 0x140103b33  ? 0xe2
  ; !! no emitter for ?
  ; 0x140103b35  ? 0xef
  ; !! no emitter for ?
  ; 0x140103b37  ? 0x57
  ; !! no emitter for ?
  ; 0x140103b39  ?
  ; !! no emitter for ?
  br label %vip_140171f7b

vip_140171f7b:
  ; 0x140171f7b  ? 0xbe
  ; !! no emitter for ?
  ; 0x140171f7d  ? 0x4f
  ; !! no emitter for ?
  ; 0x140171f7f  ? 0xbe
  ; !! no emitter for ?
  ; 0x140171f81  ? 0x4f
  ; !! no emitter for ?
  ; 0x140171f83  ? 0xbe
  ; !! no emitter for ?
  ; 0x140171f85  ? 0x4f
  ; !! no emitter for ?
  ; 0x140171f87  ? 0xbe
  ; !! no emitter for ?
  ; 0x140171f89  ? 0x4f
  ; !! no emitter for ?
  ; 0x140171f8b  ? 0xbe
  ; !! no emitter for ?
  ; 0x140171f8d  ? 0x4f
  ; !! no emitter for ?
  ; 0x140171f8f  ? 0xbe
  ; !! no emitter for ?
  ; 0x140171f91  ? 0x4f
  ; !! no emitter for ?
  ; 0x140171f93  ? 0xbe
  ; !! no emitter for ?
  ; 0x140171f95  ? 0x4f
  ; !! no emitter for ?
  ; 0x140171f97  ? 0xbe
  ; !! no emitter for ?
  ; 0x140171f99  ? 0x4f
  ; !! no emitter for ?
  ; 0x140171f9b  ? 0x3f
  ; !! no emitter for ?
  ; 0x140171f9d  ? 0xe200000000000000
  ; !! no emitter for ?
  ; 0x140171fa6  ? 0xeb
  ; !! no emitter for ?
  ; 0x140171fa8  ?
  ; !! no emitter for ?
  ; 0x140171fa9  ? 0x4f
  ; !! no emitter for ?
  ; 0x140171fab  ? 0xfa
  ; !! no emitter for ?
  ; 0x140171fad  ? 0x67000000
  ; !! no emitter for ?
  ; 0x140171fb2  ? 0x38
  ; !! no emitter for ?
  ; 0x140171fb4  ? 0x7d000000
  ; !! no emitter for ?
  ; 0x140171fb9  ? 0xe9
  ; !! no emitter for ?
  ; 0x140171fbb  ? 0x29
  ; !! no emitter for ?
  ; 0x140171fbd  ? 0x2
  ; !! no emitter for ?
  ; 0x140171fbf  ?
  ; !! no emitter for ?
  ; 0x140171fc0  ? 0x67
  ; !! no emitter for ?
  ; 0x140171fc2  ? 0x7d
  ; !! no emitter for ?
  ; 0x140171fc4  ? 0x49
  ; !! no emitter for ?
  ; 0x140171fc6  ? 0xf7
  ; !! no emitter for ?
  ; 0x140171fc8  ? 0x67000000
  ; !! no emitter for ?
  ; 0x140171fcd  ? 0xfa
  ; !! no emitter for ?
  ; 0x140171fcf  ? 0x7d000000
  ; !! no emitter for ?
  ; 0x140171fd4  ? 0x38
  ; !! no emitter for ?
  ; 0x140171fd6  ? 0x49000000
  ; !! no emitter for ?
  ; 0x140171fdb  ? 0xf7
  ; !! no emitter for ?
  ; 0x140171fdd  ? 0x67000000
  ; !! no emitter for ?
  ; 0x140171fe2  ? 0xb3
  ; !! no emitter for ?
  ; 0x140171fe4  ? 0x7d
  ; !! no emitter for ?
  ; 0x140171fe6  ? 0xfa
  ; !! no emitter for ?
  ; 0x140171fe8  ? 0x49000000
  ; !! no emitter for ?
  ; 0x140171fed  ? 0x38
  ; !! no emitter for ?
  ; 0x140171fef  ? 0xe900000f
  ; !! no emitter for ?
  ; 0x140171ff4  ? 0xb6
  ; !! no emitter for ?
  ; 0x140171ff6  ?
  ; !! no emitter for ?
  ; 0x140171ff7  ? 0xc6
  ; !! no emitter for ?
  ; 0x140171ff9  ?
  ; !! no emitter for ?
  ; 0x140171ffa  ?
  ; !! no emitter for ?
  ; 0x140171ffb  ?
  ; !! no emitter for ?
  ; 0x140171ffc  ? 0x67
  ; !! no emitter for ?
  ; 0x140171ffe  ? 0xf7
  ; !! no emitter for ?
  ; 0x140172000  ? 0x7d000000
  ; !! no emitter for ?
  ; 0x140172005  ? 0xfa
  ; !! no emitter for ?
  ; 0x140172007  ? 0x2900000f
  ; !! no emitter for ?
  ; 0x14017200c  ? 0x53
  ; !! no emitter for ?
  ; 0x14017200e  ?
  ; !! no emitter for ?
  ; 0x14017200f  ? 0x49
  ; !! no emitter for ?
  ; 0x140172011  ? 0x59
  ; !! no emitter for ?
  ; 0x140172013  ? 0xef00
  ; !! no emitter for ?
  ; 0x140172016  ? 0xa7
  ; !! no emitter for ?
  ; 0x140172018  ?
  ; !! no emitter for ?
  ; 0x140172019  ? 0xd2
  ; !! no emitter for ?
  ; 0x14017201b  ?
  ; !! no emitter for ?
  ; 0x14017201c  ?
  ; !! no emitter for ?
  ; 0x14017201d  ?
  ; !! no emitter for ?
  ; 0x14017201e  ? 0x86
  ; !! no emitter for ?
  ; 0x140172020  ? 0x8d00000000000000
  ; !! no emitter for ?
  ; 0x140172029  ?
  ; !! no emitter for ?
  ; 0x14017202a  ? 0x52
  ; !! no emitter for ?
  ; 0x14017202c  ?
  ; !! no emitter for ?
  ; 0x14017202d  ?
  ; !! no emitter for ?
  ; 0x14017202e  ?
  ; !! no emitter for ?
  ; 0x14017202f  ? 0x3f
  ; !! no emitter for ?
  ; 0x140172031  ? 0x25ffffffffffffff
  ; !! no emitter for ?
  ; 0x14017203a  ?
  ; !! no emitter for ?
  ; 0x14017203b  ? 0xbe
  ; !! no emitter for ?
  ; 0x14017203d  ? 0x86
  ; !! no emitter for ?
  ; 0x14017203f  ? 0xf300000001400c06
  ; !! no emitter for ?
  ; 0x140172048  ? 0xeb
  ; !! no emitter for ?
  ; 0x14017204a  ?
  ; !! no emitter for ?
  ; 0x14017204b  ? 0x35
  ; !! no emitter for ?
  ; 0x14017204d  ? 0xcd
  ; !! no emitter for ?
  ; 0x14017204f  ? 0x24
  ; !! no emitter for ?
  ; 0x140172051  ?
  ; !! no emitter for ?
  ; 0x140172052  ? 0x8d
  ; !! no emitter for ?
  ; 0x140172054  ?
  ; !! no emitter for ?
  ; 0x140172055  ? 0x60
  ; !! no emitter for ?
  ; 0x140172057  ?
  ; !! no emitter for ?
  ; 0x140172058  ?
  ; !! no emitter for ?
  ; 0x140172059  ?
  ; !! no emitter for ?
  ; 0x14017205a  ? 0x3f
  ; !! no emitter for ?
  ; 0x14017205c  ? 0xe200000001400c05
  ; !! no emitter for ?
  ; 0x140172065  ? 0x25
  ; !! no emitter for ?
  ; 0x140172067  ?
  ; !! no emitter for ?
  ; 0x140172068  ? 0xef
  ; !! no emitter for ?
  ; 0x14017206a  ? 0x8e
  ; !! no emitter for ?
  ; 0x14017206c  ?
  ; !! no emitter for ?
  ; 0x14017206d  ? 0xc6
  ; !! no emitter for ?
  ; 0x14017206f  ?
  ; !! no emitter for ?
  ; 0x140172070  ?
  ; !! no emitter for ?
  ; 0x140172071  ?
  ; !! no emitter for ?
  ; 0x140172072  ? 0xeb
  ; !! no emitter for ?
  ; 0x140172074  ?
  ; !! no emitter for ?
  ; 0x140172075  ? 0x4f
  ; !! no emitter for ?
  ; 0x140172077  ? 0xf3
  ; !! no emitter for ?
  ; 0x140172079  ? 0x35
  ; !! no emitter for ?
  ; 0x14017207b  ? 0xcd
  ; !! no emitter for ?
  ; 0x14017207d  ? 0xe2
  ; !! no emitter for ?
  ; 0x14017207f  ? 0xef
  ; !! no emitter for ?
  ; 0x140172081  ? 0xf3
  ; !! no emitter for ?
  ; 0x140172083  ? 0x35
  ; !! no emitter for ?
  ; 0x140172085  ? 0xcd
  ; !! no emitter for ?
  ; 0x140172087  ? 0xe2
  ; !! no emitter for ?
  ; 0x140172089  ? 0xef
  ; !! no emitter for ?
  ; 0x14017208b  ? 0xf3
  ; !! no emitter for ?
  ; 0x14017208d  ? 0x35
  ; !! no emitter for ?
  ; 0x14017208f  ? 0xcd
  ; !! no emitter for ?
  ; 0x140172091  ? 0xe2
  ; !! no emitter for ?
  ; 0x140172093  ? 0xef
  ; !! no emitter for ?
  ; 0x140172095  ? 0xf3
  ; !! no emitter for ?
  ; 0x140172097  ? 0x35
  ; !! no emitter for ?
  ; 0x140172099  ? 0xcd
  ; !! no emitter for ?
  ; 0x14017209b  ? 0x57
  ; !! no emitter for ?
  ; 0x14017209d  ?
  ; !! no emitter for ?
  ; 2 successors and no recovered condition
  br label %vip_1400c05d1

vip_14018f95a:
  ; 0x14018f95a  ? 0x4f
  ; !! no emitter for ?
  ; 0x14018f95c  ? 0xbe
  ; !! no emitter for ?
  ; 0x14018f95e  ? 0x4f
  ; !! no emitter for ?
  ; 0x14018f960  ? 0xbe
  ; !! no emitter for ?
  ; 0x14018f962  ? 0x4f
  ; !! no emitter for ?
  ; 0x14018f964  ? 0xbe
  ; !! no emitter for ?
  ; 0x14018f966  ? 0x4f
  ; !! no emitter for ?
  ; 0x14018f968  ? 0xbe
  ; !! no emitter for ?
  ; 0x14018f96a  ? 0x4f
  ; !! no emitter for ?
  ; 0x14018f96c  ? 0xbe
  ; !! no emitter for ?
  ; 0x14018f96e  ? 0x4f
  ; !! no emitter for ?
  ; 0x14018f970  ? 0xbe
  ; !! no emitter for ?
  ; 0x14018f972  ? 0x4f
  ; !! no emitter for ?
  ; 0x14018f974  ? 0xbe
  ; !! no emitter for ?
  ; 0x14018f976  ? 0x4f
  ; !! no emitter for ?
  ; 0x14018f978  ? 0xbe
  ; !! no emitter for ?
  ; 0x14018f97a  ? 0x86
  ; !! no emitter for ?
  ; 0x14018f97c  ? 0xcd00000000000000
  ; !! no emitter for ?
  ; 0x14018f985  ? 0x25
  ; !! no emitter for ?
  ; 0x14018f987  ?
  ; !! no emitter for ?
  ; 0x14018f988  ? 0xbe
  ; !! no emitter for ?
  ; 0x14018f98a  ? 0x52
  ; !! no emitter for ?
  ; 0x14018f98c  ?
  ; !! no emitter for ?
  ; 0x14018f98d  ? 0xe2
  ; !! no emitter for ?
  ; 0x14018f98f  ? 0xbe
  ; !! no emitter for ?
  ; 0x14018f991  ? 0xef
  ; !! no emitter for ?
  ; 0x14018f993  ? 0x4f
  ; !! no emitter for ?
  ; 0x14018f995  ? 0xf3
  ; !! no emitter for ?
  ; 0x14018f997  ? 0xbe
  ; !! no emitter for ?
  ; 0x14018f999  ? 0x35
  ; !! no emitter for ?
  ; 0x14018f99b  ? 0x4f
  ; !! no emitter for ?
  ; 0x14018f99d  ? 0xcd
  ; !! no emitter for ?
  ; 0x14018f99f  ? 0x60
  ; !! no emitter for ?
  ; 0x14018f9a1  ?
  ; !! no emitter for ?
  ; 0x14018f9a2  ? 0xe2
  ; !! no emitter for ?
  ; 0x14018f9a4  ? 0xc6
  ; !! no emitter for ?
  ; 0x14018f9a6  ?
  ; !! no emitter for ?
  ; 0x14018f9a7  ? 0xef
  ; !! no emitter for ?
  ; 0x14018f9a9  ? 0xd2
  ; !! no emitter for ?
  ; 0x14018f9ab  ?
  ; !! no emitter for ?
  ; 0x14018f9ac  ? 0xf3
  ; !! no emitter for ?
  ; 0x14018f9ae  ? 0x52
  ; !! no emitter for ?
  ; 0x14018f9b0  ?
  ; !! no emitter for ?
  ; 0x14018f9b1  ? 0x35
  ; !! no emitter for ?
  ; 0x14018f9b3  ? 0x60
  ; !! no emitter for ?
  ; 0x14018f9b5  ?
  ; !! no emitter for ?
  ; 0x14018f9b6  ? 0x3f
  ; !! no emitter for ?
  ; 0x14018f9b8  ? 0xcd00000001400c27
  ; !! no emitter for ?
  ; 0x14018f9c1  ? 0xeb
  ; !! no emitter for ?
  ; 0x14018f9c3  ?
  ; !! no emitter for ?
  ; 0x14018f9c4  ? 0xc6
  ; !! no emitter for ?
  ; 0x14018f9c6  ?
  ; !! no emitter for ?
  ; 0x14018f9c7  ?
  ; !! no emitter for ?
  ; 0x14018f9c8  ?
  ; !! no emitter for ?
  ; 0x14018f9c9  ? 0x59
  ; !! no emitter for ?
  ; 0x14018f9cb  ? 0xfa00
  ; !! no emitter for ?
  ; 0x14018f9ce  ? 0x70000000
  ; !! no emitter for ?
  ; 0x14018f9d3  ? 0x83
  ; !! no emitter for ?
  ; 0x14018f9d5  ? 0x3e
  ; !! no emitter for ?
  ; 0x14018f9d7  ?
  ; !! no emitter for ?
  ; 0x14018f9d8  ? 0x86
  ; !! no emitter for ?
  ; 0x14018f9da  ? 0x25000000000000ff
  ; !! no emitter for ?
  ; 0x14018f9e3  ?
  ; !! no emitter for ?
  ; 0x14018f9e4  ? 0xd2
  ; !! no emitter for ?
  ; 0x14018f9e6  ?
  ; !! no emitter for ?
  ; 0x14018f9e7  ?
  ; !! no emitter for ?
  ; 0x14018f9e8  ?
  ; !! no emitter for ?
  ; 0x14018f9e9  ? 0x4f
  ; !! no emitter for ?
  ; 0x14018f9eb  ? 0x74
  ; !! no emitter for ?
  ; 0x14018f9ed  ? 0x3800
  ; !! no emitter for ?
  ; 0x14018f9f0  ? 0xad000000
  ; !! no emitter for ?
  ; 0x14018f9f5  ? 0x7b
  ; !! no emitter for ?
  ; 0x14018f9f7  ? 0x6ffc
  ; !! no emitter for ?
  ; 0x14018f9fa  ?
  ; !! no emitter for ?
  ; 0x14018f9fb  ? 0x3f
  ; !! no emitter for ?
  ; 0x14018f9fd  ? 0xeb000000000000ff
  ; !! no emitter for ?
  ; 0x14018fa06  ?
  ; !! no emitter for ?
  ; 0x14018fa07  ? 0x52
  ; !! no emitter for ?
  ; 0x14018fa09  ?
  ; !! no emitter for ?
  ; 0x14018fa0a  ?
  ; !! no emitter for ?
  ; 0x14018fa0b  ?
  ; !! no emitter for ?
  ; 0x14018fa0c  ? 0xe2
  ; !! no emitter for ?
  ; 0x14018fa0e  ? 0xa1
  ; !! no emitter for ?
  ; 0x14018fa10  ?
  ; !! no emitter for ?
  ; 0x14018fa11  ? 0x60
  ; !! no emitter for ?
  ; 0x14018fa13  ?
  ; !! no emitter for ?
  ; 0x14018fa14  ?
  ; !! no emitter for ?
  ; 0x14018fa15  ?
  ; !! no emitter for ?
  ; 0x14018fa16  ? 0x4f
  ; !! no emitter for ?
  ; 0x14018fa18  ? 0xce
  ; !! no emitter for ?
  ; 0x14018fa1a  ? 0xf700
  ; !! no emitter for ?
  ; 0x14018fa1d  ? 0xc2000000
  ; !! no emitter for ?
  ; 0x14018fa22  ? 0xf2
  ; !! no emitter for ?
  ; 0x14018fa24  ? 0xd5f4
  ; !! no emitter for ?
  ; 0x14018fa27  ?
  ; !! no emitter for ?
  ; 0x14018fa28  ? 0x86
  ; !! no emitter for ?
  ; 0x14018fa2a  ? 0x25000000000000ff
  ; !! no emitter for ?
  ; 0x14018fa33  ?
  ; !! no emitter for ?
  ; 0x14018fa34  ? 0xc6
  ; !! no emitter for ?
  ; 0x14018fa36  ?
  ; !! no emitter for ?
  ; 0x14018fa37  ?
  ; !! no emitter for ?
  ; 0x14018fa38  ?
  ; !! no emitter for ?
  ; 0x14018fa39  ? 0xef
  ; !! no emitter for ?
  ; 0x14018fa3b  ? 0x80
  ; !! no emitter for ?
  ; 0x14018fa3d  ?
  ; !! no emitter for ?
  ; 0x14018fa3e  ? 0xd2
  ; !! no emitter for ?
  ; 0x14018fa40  ?
  ; !! no emitter for ?
  ; 0x14018fa41  ?
  ; !! no emitter for ?
  ; 0x14018fa42  ?
  ; !! no emitter for ?
  ; 0x14018fa43  ? 0x4f
  ; !! no emitter for ?
  ; 0x14018fa45  ? 0x19
  ; !! no emitter for ?
  ; 0x14018fa47  ? 0xf300
  ; !! no emitter for ?
  ; 0x14018fa4a  ? 0xa7
  ; !! no emitter for ?
  ; 0x14018fa4c  ?
  ; !! no emitter for ?
  ; 0x14018fa4d  ? 0x3f
  ; !! no emitter for ?
  ; 0x14018fa4f  ? 0x90ffffffffffffff
  ; !! no emitter for ?
  ; 0x14018fa58  ?
  ; !! no emitter for ?
  ; 0x14018fa59  ? 0x86
  ; !! no emitter for ?
  ; 0x14018fa5b  ? 0xebffffffffffffff
  ; !! no emitter for ?
  ; 0x14018fa64  ?
  ; !! no emitter for ?
  ; 0x14018fa65  ? 0x3f
  ; !! no emitter for ?
  ; 0x14018fa67  ? 0x8e00000000ac3e0f
  ; !! no emitter for ?
  ; 0x14018fa70  ?
  ; !! no emitter for ?
  ; 0x14018fa71  ? 0x52
  ; !! no emitter for ?
  ; 0x14018fa73  ?
  ; !! no emitter for ?
  ; 0x14018fa74  ?
  ; !! no emitter for ?
  ; 0x14018fa75  ?
  ; !! no emitter for ?
  ; 0x14018fa76  ? 0x25
  ; !! no emitter for ?
  ; 0x14018fa78  ?
  ; !! no emitter for ?
  ; 0x14018fa79  ? 0x35
  ; !! no emitter for ?
  ; 0x14018fa7b  ? 0xcd
  ; !! no emitter for ?
  ; 0x14018fa7d  ? 0xe2
  ; !! no emitter for ?
  ; 0x14018fa7f  ? 0xef
  ; !! no emitter for ?
  ; 0x14018fa81  ? 0xf3
  ; !! no emitter for ?
  ; 0x14018fa83  ? 0x35
  ; !! no emitter for ?
  ; 0x14018fa85  ? 0xcd
  ; !! no emitter for ?
  ; 0x14018fa87  ? 0xe2
  ; !! no emitter for ?
  ; 0x14018fa89  ? 0xef
  ; !! no emitter for ?
  ; 0x14018fa8b  ? 0xf3
  ; !! no emitter for ?
  ; 0x14018fa8d  ? 0x35
  ; !! no emitter for ?
  ; 0x14018fa8f  ? 0xcd
  ; !! no emitter for ?
  ; 0x14018fa91  ? 0xe2
  ; !! no emitter for ?
  ; 0x14018fa93  ? 0xef
  ; !! no emitter for ?
  ; 0x14018fa95  ? 0xf3
  ; !! no emitter for ?
  ; 0x14018fa97  ? 0x35
  ; !! no emitter for ?
  ; 0x14018fa99  ? 0xc3
  ; !! no emitter for ?
  ; 0x14018fa9b  ?
  ; !! no emitter for ?
  br label %vip_140103a5e
}

declare i32 @dragon.opaque.cpuid.0() #0
declare i32 @dragon.opaque.cpuid.1() #0
declare i32 @dragon.opaque.cpuid.2() #0
declare i32 @dragon.opaque.cpuid.3() #0
declare i32 @dragon.opaque.rdtsc.0() #0
declare i32 @dragon.opaque.rdtsc.1() #0

attributes #0 = { nounwind memory(none) }
