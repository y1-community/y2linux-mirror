"""Legacy Android/MTK boot image writer shared by packaging, recovery and tests."""
import hashlib
import struct


def pack(kernel, ramdisk, recovery=False):
    """BOOTIMG, or with recovery=True the RECOVERY image LK loads for "boot-recovery"
    (its ramdisk wrapper must say RECOVERY; the name tells our updater it is ours)."""
    def wrapper(name, data):
        return struct.pack('<II32s',0x58881688,len(data),name.ljust(32,b'\0')) + b'\xff'*472 + data
    k, r = wrapper(b'KERNEL',kernel), wrapper(b'RECOVERY' if recovery else b'ROOTFS',ramdisk)
    header = bytearray(2048)
    name = b'Y2Recovery-1' if recovery else b'Y2Linux-M1'
    struct.pack_into('<8s10I16s',header,0,b'ANDROID!',len(k),0x10008000,len(r),0x11000000,
                     0,0x10f00000,0x10000100,2048,0,0,name.ljust(16,b'\0'))
    identity = hashlib.sha1()
    for payload in (k,r,b''):
        identity.update(payload); identity.update(struct.pack('<I',len(payload)))
    header[576:596] = identity.digest()
    return bytes(header) + k + bytes(-len(k)%2048) + r + bytes(-len(r)%2048) + bytes(4096)
