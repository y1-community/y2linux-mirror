"""Package only a freshly validated offline D08 artifact; never access a device."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from tools.validation.artifacts import check as check_artifacts
from tools.validation.bootimg import check as check_package
from tools.build.bootimage import pack


def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('root',type=Path)
    args = parser.parse_args(); project = Path(__file__).resolve().parents[2]
    layout,kernel = check_artifacts(args.root,project)
    if not (args.root/'zImage-dtb').exists(): raise ValueError('validated appended payload required')
    ramdisk = (args.root/'initramfs.cpio.gz').read_bytes()
    image = pack(kernel,ramdisk)
    result = check_package(image,kernel,ramdisk,layout)
    (args.root/'BOOTIMG.img').write_bytes(image)
    layout['bootimg'] = result
    (args.root/'layout.json').write_text(json.dumps(layout,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__ == '__main__': main()
