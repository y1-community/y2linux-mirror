#!/bin/sh
set -eu
# Caller supplies a checked actual gzip length through /build/initrd-size.h.
test -s /build/initrd-size.h
make -C /src O=/build/kernel scripts_dtc
dts=${Y2_DTS:-innioasis-y2-first-boot.dts}
# Y2_DTB_OUT/Y2_DTS_DEFINES build the RECOVERY variant (-DY2_RECOVERY) beside it.
out=${Y2_DTB_OUT:-/build/y2.dtb}
# shellcheck disable=SC2086 # the defines are separate preprocessor words
clang -E -nostdinc -undef -D__DTS__ ${Y2_DTS_DEFINES:-} -x assembler-with-cpp \
    -I /src/include -I /build "/project/kernel/dts/$dts" \
    -o "${out%.dtb}.preprocessed.dts"
/build/kernel/scripts/dtc/dtc -I dts -O dtb -o "$out" "${out%.dtb}.preprocessed.dts"
