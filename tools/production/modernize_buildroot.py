#!/usr/bin/env python3
"""Apply the reviewed userspace package pins to the locked Buildroot tree.

POWER-FINAL-15: the base is Buildroot 2026.08. Packages whose newest upstream
release is newer than that Buildroot ships are raised here, each with the
SHA-256 of the exact archive. The edits are applied after extracting that exact
release, so the resulting source tree is fully reproducible without carrying a
private Buildroot fork.
"""
from pathlib import Path

# (package, recipe edits (old, new), old archive, new archive, new sha256, patches dropped)
BUMPS = (
    ("bluez5_utils", (("BLUEZ5_UTILS_VERSION = 5.86", "BLUEZ5_UTILS_VERSION = 5.87"),),
     "bluez-5.86.tar.xz", "bluez-5.87.tar.xz",
     "26bdcf2cebd7310c6f598850606b037ef0c515fe6608ebc54d22c50c4c32b35f", ()),
    ("bluez5_utils-headers", (("BLUEZ5_UTILS_HEADERS_VERSION = 5.86", "BLUEZ5_UTILS_HEADERS_VERSION = 5.87"),),
     "bluez-5.86.tar.xz", "bluez-5.87.tar.xz",
     "26bdcf2cebd7310c6f598850606b037ef0c515fe6608ebc54d22c50c4c32b35f", ()),
    ("sbc", (("SBC_VERSION = 2.1", "SBC_VERSION = 2.2"),),
     "sbc-2.1.tar.xz", "sbc-2.2.tar.xz",
     "a1ada76ef35e5af9c2fbd063754dc9e37a8d989417c6eb1ecebb089b1383ae9e", ()),
    ("sqlite", (("SQLITE_VERSION = 3.53.4", "SQLITE_VERSION = 3.54.0"),
                ("SQLITE_TAR_VERSION = 3530400", "SQLITE_TAR_VERSION = 3540000")),
     "sqlite-autoconf-3530400.tar.gz", "sqlite-autoconf-3540000.tar.gz",
     "134ec0802dda5795816e25d25872d20b312cb3973438c49b30bc40b7705ea9ed", ()),
    ("libopenssl", (("LIBOPENSSL_VERSION = 3.6.4", "LIBOPENSSL_VERSION = 4.0.3"),),
     "openssl-3.6.4.tar.gz", "openssl-4.0.3.tar.gz",
     "325b5c806167c13b40b1ffeadfe0248197c00eccc4cf123ec1e28d2d2fd216d9",
     # ppc64 big-endian only; not this ARM target.
     ("0002-Configure-use-ELFv2-ABI-on-some-ppc64-big-endian-sys.patch",)),
    ("python3", (("PYTHON3_VERSION_MAJOR = 3.14", "PYTHON3_VERSION_MAJOR = 3.15"),
                 ("PYTHON3_VERSION = $(PYTHON3_VERSION_MAJOR).7", "PYTHON3_VERSION = $(PYTHON3_VERSION_MAJOR).0")),
     "Python-3.14.7.tar.xz", "Python-3.15.0.tar.xz",
     "ba4bed1ba346b916890b76d9e320451420aa69f6408997d33c66482eeae3d575",
     # A 3.14 build fix for the remote debugging module, part of 3.15.
     ("0010-gh-139275-Fix-compilation-of-Modules-_remote_debuggi.patch",)),
    ("mesa3d", (("MESA3D_VERSION = 26.1.8", "MESA3D_VERSION = 26.2.4"),),
     "mesa-26.1.8.tar.xz", "mesa-26.2.4.tar.xz",
     "bce5f7fbebb934373b86c999a064d52fb5065878dc57f287f95346648ec832e9",
     # Intel "jay" compiler workaround for host GCC < 13; no Intel driver here.
     ("0005-src-intel-compiler-jay-jay_ir.h-do-not-used-typed-en.patch",)),
    ("libglib2", (("LIBGLIB2_VERSION_MAJOR = 2.88", "LIBGLIB2_VERSION_MAJOR = 2.90"),
                  ("LIBGLIB2_VERSION = $(LIBGLIB2_VERSION_MAJOR).3", "LIBGLIB2_VERSION = $(LIBGLIB2_VERSION_MAJOR).1")),
     "glib-2.88.3.tar.xz", "glib-2.90.1.tar.xz",
     "93c941aa17d5eb1d53fe838365f29a8b4e539c222a256d974ec8f30fc413e396", ()),
    ("openssh", (("OPENSSH_VERSION_MAJOR = 10.5", "OPENSSH_VERSION_MAJOR = 10.6"),),
     "openssh-10.5p1.tar.gz", "openssh-10.6p1.tar.gz",
     "a9dc9565dffe8640f64d863cd29a32bc4a3dbdec0566a7fc44c5d6ee767d5f39", ()),
    ("util-linux", (("UTIL_LINUX_VERSION_MAJOR = 2.41", "UTIL_LINUX_VERSION_MAJOR = 2.42"),
                    ("UTIL_LINUX_VERSION = $(UTIL_LINUX_VERSION_MAJOR).5", "UTIL_LINUX_VERSION = $(UTIL_LINUX_VERSION_MAJOR).4")),
     "util-linux-2.41.5.tar.xz", "util-linux-2.42.4.tar.xz",
     "fbd62a100ab7bb8746ba0661255c3c48185b1e9021507c624da01fbc696330ec",
     # All three are part of 2.42.4.
     ("0001-include-mount-api-utils.h-fix-LSMT_ROOT-definition.patch",
      "0002-autotools-optionally-add-libpthread-to-uuid.pc.patch",
      "0003-loopdev-use-openat2-RESOLVE_NO_SYMLINKS-for-backing-.patch")),
    ("iproute2", (("IPROUTE2_VERSION = 7.1.0", "IPROUTE2_VERSION = 7.2.0"),),
     "iproute2-7.1.0.tar.xz", "iproute2-7.2.0.tar.xz",
     "4c2fa124c2cf0afd7ca34d1eeacba6ba048a56f6374e2aab93dafbdbd4eea9c0", ()),
    ("expat", (("EXPAT_VERSION = 2.8.4", "EXPAT_VERSION = 2.9.0"),),
     "expat-2.8.4.tar.xz", "expat-2.9.0.tar.xz",
     "1e6371862cc31999b368c3b89b49994f0677e1bab5f1b2b85ae3741f5d803051", ()),
    # 1.4 needs libblkid; undeclared, a serial build only worked because
    # util-linux happened to come first (the parallel build found it).
    ("exfatprogs", (("EXFATPROGS_VERSION = 1.2.9", "EXFATPROGS_VERSION = 1.4.3"),
                    ("$(eval $(autotools-package))",
                     "EXFATPROGS_DEPENDENCIES = host-pkgconf util-linux\n$(eval $(autotools-package))")),
     "exfatprogs-1.2.9.tar.xz", "exfatprogs-1.4.3.tar.xz",
     "57226a8ec1bfbce06d68a42cde8cd980414a9457882e691fbce4a4f86c8d5f08", ()),
    ("iperf3", (("IPERF3_VERSION = 3.21", "IPERF3_VERSION = 3.22"),),
     "iperf-3.21.tar.gz", "iperf-3.22.tar.gz",
     "1c0d0fb02c52626111d6e132db80edfbf27bbaff8bd9245df2a371dcb0b35a92", ()),
    ("json-c", (("JSON_C_VERSION = 0.18", "JSON_C_VERSION = 0.19"),),
     "json-c-0.18.tar.gz", "json-c-0.19.tar.gz",
     "37ad0249902e301bd9052bf712e511fcc6acff4ecaad4b5900aad9ce564e26de", ()),
    ("pcre2", (("PCRE2_VERSION = 10.47", "PCRE2_VERSION = 10.49"),),
     "pcre2-10.47.tar.bz2", "pcre2-10.49.tar.bz2",
     "53c156e1ba416a20da8e65395daa132da0d80e76910424caca3fcdae7831d384", ()),
    ("procps-ng", (("PROCPS_NG_VERSION = 4.0.6", "PROCPS_NG_VERSION = 4.0.7"),),
     "procps-ng-4.0.6.tar.xz", "procps-ng-4.0.7.tar.xz",
     "9d2021f47a4501c667862c9942a92d1953694b21d11bcd1702e83eb594e3d67d", ()),
    ("strace", (("STRACE_VERSION = 7.1", "STRACE_VERSION = 7.2"),),
     "strace-7.1.tar.xz", "strace-7.2.tar.xz",
     "4bde6246926890dcee824f6e6ac42a06752f47d77e5097d86e3c0d6d4b709fe5", ()),
    ("ca-certificates", (("CA_CERTIFICATES_VERSION = 20260223", "CA_CERTIFICATES_VERSION = 20260816"),
                         ("archive/debian/20260223T202245Z/", "archive/debian/20260817T022356Z/")),
     "ca-certificates_20260223.tar.xz", "ca-certificates_20260816.tar.xz",
     "d939bcdd0cb058712cf4175bac76997676eb8b68fe9473765e1b40fb3d5b186a", ()),
    # (OpenSSL 4.0 removed the c_rehash script; ca_certificates() below uses
    # the built-in "openssl rehash".)
    ("meson", (("MESON_VERSION = 1.11.1", "MESON_VERSION = 1.12.1"),),
     "meson-1.11.1.tar.gz", "meson-1.12.1.tar.gz",
     "ab0a6ca09f8ef70c564c8241fb5a23957886a0b53fb58412b5e07eaf07dba743", ()),
    # Buildroot 2025.02.18 (September 2026) already carried a newer database
    # than 2026.08 (August 2026); tools/build/regulatory.py pins the same file.
    ("wireless-regdb", (("WIRELESS_REGDB_VERSION = 2026.05.30", "WIRELESS_REGDB_VERSION = 2026.09.03"),),
     "wireless-regdb-2026.05.30.tar.xz", "wireless-regdb-2026.09.03.tar.xz",
     "b22e0901227b820cd1c280abe681a15b773a5103a5e10dc442e94ebb34cbf58d", ()),
)

# Host Python build packages: newest PyPI sdist, PyPI-published md5/sha256 (Python 3.15 needs
# the newest pypa-build; the rest follow the "newest release" policy).
PYPI_BUMPS = (
    ('python-cython', '3.3.0', 'cython-3.3.0.tar.gz', 'https://files.pythonhosted.org/packages/a9/d8/4981ef716ad0e3ff0d3ef383aefc6b03c4a88dee33b272bf8e0d833001ca', 'df8ac911bb3c6af48fb0158160e5b029', 'eed0d93fbca7087f143b42c34b05a825849bdf17f101572c2105acfa49aa88b8'),
    ('python-flit-core', '4.1.0', 'flit_core-4.1.0.tar.gz', 'https://files.pythonhosted.org/packages/e7/91/add211b38c357bf1b94900b4f79c34661a92be65c0243d2b0a3393c5092d', '6aed048324976ab2178d4d57a7933763', '62e12b63ead8335b37f59fabb977c7167fe476dafb5e41785dfa8c9aff843bc6'),
    ('python-installer', '1.0.1', 'installer-1.0.1.tar.gz', 'https://files.pythonhosted.org/packages/06/fe/b9f481cf0cc867958a21338baa900357b7b7d86cac9b025948049d77923c', 'c919fdc746e46b0a93ae53ed375e047f', '052c7fc3721d54c696e2dea019be67539d7b144e924f559f54beb3121831c364'),
    ('python-mako', '1.4.3', 'mako-1.4.3.tar.gz', 'https://files.pythonhosted.org/packages/5a/09/e07c4b5579a79f4b16f8d4f29f6c54514ac787c4ad506b8c4f28a0e6b0bf', '4b7555300a75b951bf99aa067cbb3633', 'cd6537fe88d5fec315c55c2f8529bc4ce7a9a352ad7db3eeaa6a66e2dd4ec37a'),
    ('python-markupsafe', '3.0.4', 'markupsafe-3.0.4.tar.gz', 'https://files.pythonhosted.org/packages/38/9b/e422a865e1d5d57d0e509b4e0bf1c1a70a7f6382c29a5aa428df994c8bc8', '7c70b0284d3733795287d8d67e7164e0', '2e9ad7dd851bf45fab9f75cbff4cb493fee9979e8d8c7c9c3ee119022518edd6'),
    ('python-packaging', '26.3', 'packaging-26.3.tar.gz', 'https://files.pythonhosted.org/packages/7d/fa/3944b40b07da9ce895c0e6303a5ab7d53da063554f534556b134a54d6093', 'c6809ec56986682ceffe573b8778a1ce', '94edc256424af38762eb31306eed28beb9f0efc50a8837492c9d6fd6004aed79'),
    ('python-pypa-build', '1.6.1', 'build-1.6.1.tar.gz', 'https://files.pythonhosted.org/packages/bd/67/4898a44ea4f3f8e213b0954ec0aa0a16971d62a6212d6ea3931e97115b99', '56f61c2663493ebe4916b8acf40bb039', '51cc11666391ab6f092070437ac747002ff46f3e4113a3622177ee6b488bfc53'),
    ('python-pyproject-hooks', '1.3.3', 'pyproject_hooks-1.3.3.tar.gz', 'https://files.pythonhosted.org/packages/6d/5d/f2ddeef4a855a102aaae5e97826a0260007522ab504421b75addfdb1517c', '4946aadbb25ac57019c431257e910583', 'defda19b854fa0d3bd4f76ea4ddcba8abd7dcfcdd585a6690ade050744fc5f43'),
    ('python-setuptools', '84.0.0', 'setuptools-84.0.0.tar.gz', 'https://files.pythonhosted.org/packages/6d/44/f5da03a8ef95d369145c5bb53050e7877c9f3d312e128605fd9504829143', 'c6919a4b0862ca30604559e163a6cce9', 'f4695c21257f0d9b537ec2692c941d02ee143b7cc1276941349a546573b2ef73'),
    ('python-wheel', '0.48.0', 'wheel-0.48.0.tar.gz', 'https://files.pythonhosted.org/packages/d0/20/50ed6bdf27dec98b568a8ae25dc599f35baa3d9709f9e83fd1edb56b9a90', 'f668e4885de814378b332eceea04a8a0', '94800765601e9171bf5d58d066e640662842bcedcbab982b2c90787a2c987322'),
)

BLUEZ_ALSA_VERSION = "5.0.0"
BLUEZ_ALSA_SHA256 = "e1249cbebd24925c977814f62adab71f2ebd771e28e6a4ac58bb1ac97ce3beb5"
TOOLCHAIN_VERSION = "2026.08-1"
TOOLCHAIN_SHA256 = "9b7e25a74e87dac1e05d399444295e254a3073a056101e3197a859490e5701cd"


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    if old in text:
        path.write_text(text.replace(old, new, 1))
    elif new not in text:
        raise ValueError(f"expected pin missing from {path}: {old}")


def replace_archive_hash(path: Path, old_archive: str, new_archive: str, sha256: str) -> None:
    """Every hash line of the old archive becomes one SHA-256 line of the new one."""
    lines = path.read_text().splitlines()
    kept = [line for line in lines if not line.rstrip().endswith("  " + old_archive)]
    new_line = f"sha256  {sha256}  {new_archive}"
    if len(kept) == len(lines) and new_line not in lines:
        raise ValueError(f"expected source hash missing from {path}: {old_archive}")
    if new_line not in kept:
        kept.append(new_line)
    path.write_text("\n".join(kept) + "\n")


def bump(buildroot_source: Path, name, edits, old_archive, new_archive, sha256, dropped) -> None:
    package = buildroot_source / "package" / name
    for old, new in edits:
        replace_once(package / f"{name}.mk", old, new)
    replace_archive_hash(package / f"{name}.hash", old_archive, new_archive, sha256)
    for patch_name in dropped:
        patch = package / patch_name
        if patch.exists():
            patch.unlink()


def bump_pypi(buildroot_source: Path, name, version, source, site, md5, sha256) -> None:
    """Point a Buildroot python package at a newer PyPI sdist (version, source, site, hashes)."""
    import re
    package = buildroot_source / "package" / name
    recipe = package / f"{name}.mk"
    var = name.upper().replace("-", "_")
    text = recipe.read_text()
    for key, value in (("VERSION", version), ("SOURCE", source), ("SITE", site)):
        pattern = re.compile(rf"^{var}_{key} = .*$", re.M)
        if not pattern.search(text):
            raise ValueError(f"expected {var}_{key} missing from {recipe}")
        text = pattern.sub(f"{var}_{key} = {value}", text, count=1)
    recipe.write_text(text)
    hashes = package / f"{name}.hash"
    lines = [l for l in hashes.read_text().splitlines()
             if not re.match(r"^(md5|sha256)\s+[0-9a-f]+\s+\S+\.(tar\.gz|tar\.bz2|zip)$", l)]
    lines += [f"md5  {md5}  {source}", f"sha256  {sha256}  {source}"]
    hashes.write_text("\n".join(lines) + "\n")


# RELEASE-49 (owner decision 2026-10-16): AAC from fdk-aac-free, Red Hat's
# stripped FDK library (AAC-LC only; the patented HE-AAC SBR/PS parts removed)
# that Fedora ships after legal review and Debian/Ubuntu package. This is
# upstream gitlab.freedesktop.org/wtaymans/fdk-aac-stripped branch stripped5
# (9896ddc0d08b, version 2.0.3) as Ubuntu's release tarball with generated
# autotools files; same NOTICE as the full library. A2DP uses AAC-LC.
FDK_AAC_FREE_SOURCE = "fdk-aac-free_2.0.3.orig.tar.gz"
FDK_AAC_FREE_SHA256 = "2dc6952b70283888994ae6e551a79039781f0a64c67fb637bffd5a8f483c34c5"
FDK_AAC_NOTICE_SHA256 = "95ec80da40b4af12ad4c4f3158c9cfb80f2479f3246e4260cb600827cc8c7836"


def fdk_aac_free(buildroot_source: Path) -> None:
    package = buildroot_source / "package/fdk-aac"
    (package / "fdk-aac.mk").write_text(
        "################################################################################\n"
        "#\n# fdk-aac (Y2: fdk-aac-free, AAC-LC only)\n#\n"
        "################################################################################\n\n"
        "FDK_AAC_VERSION = 2.0.3\n"
        f"FDK_AAC_SOURCE = {FDK_AAC_FREE_SOURCE}\n"
        "FDK_AAC_SITE = https://archive.ubuntu.com/ubuntu/pool/universe/f/fdk-aac-free\n"
        "FDK_AAC_LICENSE = fdk-aac license\n"
        "FDK_AAC_LICENSE_FILES = NOTICE\n\n"
        "FDK_AAC_INSTALL_STAGING = YES\n\n"
        "$(eval $(autotools-package))\n")
    (package / "fdk-aac.hash").write_text(
        "# fdk-aac-free (Ubuntu orig tarball = freedesktop wtaymans/fdk-aac-stripped stripped5)\n"
        f"sha256  {FDK_AAC_NOTICE_SHA256}  NOTICE\n"
        f"sha256  {FDK_AAC_FREE_SHA256}  {FDK_AAC_FREE_SOURCE}\n")


def ca_certificates(buildroot_source: Path) -> None:
    """OpenSSL 4.0 has no c_rehash script; "openssl rehash" does the same."""
    recipe = buildroot_source / "package/ca-certificates/ca-certificates.mk"
    text = recipe.read_text()
    updated = text.replace("$(HOST_DIR)/bin/c_rehash ", "$(HOST_DIR)/bin/openssl rehash ")
    if "c_rehash" in updated or "openssl rehash" not in updated:
        raise ValueError("ca-certificates rehash anchor missing")
    recipe.write_text(updated)
    # 20260816 ships a revised debian/copyright (license file of legal-info);
    # the archive itself is pinned by its own hash above.
    replace_once(buildroot_source / "package/ca-certificates/ca-certificates.hash",
                 "e85e1bcad3a915dc7e6f41412bc5bdeba275cadd817896ea0451f2140a93967c  debian/copyright",
                 "1ae033c4679e4440848d2fcf6bc8e57d4da2644662775f401e2fede57862674c  debian/copyright")


def bluez_alsa(buildroot_source: Path) -> None:
    package = buildroot_source / "package/bluez-alsa"
    recipe = package / "bluez-alsa.mk"
    replace_once(recipe, "BLUEZ_ALSA_VERSION = 4.3.1", f"BLUEZ_ALSA_VERSION = {BLUEZ_ALSA_VERSION}")
    replace_once(recipe, "--enable-cli", "--enable-ctl")
    # BlueALSA 5 has more optional A2DP/profile switches. Keep every one
    # disabled in the product package; a future codec decision must change the
    # Buildroot configuration deliberately and add its own evidence.
    marker = "\t--disable-debug-time \\\n"
    text = recipe.read_text()
    disables = (
        "\t--disable-asha \\\n"
        "\t--disable-faststream \\\n"
        "\t--disable-lc3plus \\\n"
        "\t--disable-lc3-swb \\\n"
        "\t--disable-ldac \\\n"
        "\t--disable-lhdc \\\n"
    )
    if "--disable-ldac" not in text:
        if marker not in text:
            raise ValueError("BlueALSA configure option anchor missing")
        recipe.write_text(text.replace(marker, disables + marker, 1))
    # Explicit owner-private qualification profile. Keep this after the stock
    # option branches: independently selecting another library must never enable
    # an unreviewed encoder or mix GPL-3 libopenaptx with FDK-AAC.
    text = recipe.read_text()
    # RELEASE-49: aptX/aptX HD only in owner-private builds (Y2_CODEC_APTX);
    # public releases carry SBC, AAC (fdk-aac-free) and LDAC.
    experiment_options = """# Y2 owner-private optional encoder profile
ifeq ($(BR2_PACKAGE_Y2_CODEC_EXPERIMENTS),y)
ifeq ($(BR2_PACKAGE_LIBOPENAPTX),y)
$(error Y2 codec profile requires libfreeaptx, not libopenaptx)
endif
BLUEZ_ALSA_DEPENDENCIES += fdk-aac libldac
BLUEZ_ALSA_CONF_OPTS := $(filter-out --disable-aac --disable-aptx --disable-aptx-hd --disable-ldac,$(BLUEZ_ALSA_CONF_OPTS))
BLUEZ_ALSA_CONF_OPTS += --enable-aac --enable-ldac
ifeq ($(BR2_PACKAGE_Y2_CODEC_APTX),y)
BLUEZ_ALSA_DEPENDENCIES += libfreeaptx
BLUEZ_ALSA_CONF_OPTS += --with-libfreeaptx --enable-aptx --enable-aptx-hd
else
BLUEZ_ALSA_CONF_OPTS += --disable-aptx --disable-aptx-hd
endif
endif

"""
    anchor = "$(eval $(autotools-package))"
    if "# Y2 owner-private optional encoder profile" in text:
        start = text.index("# Y2 owner-private optional encoder profile")
        text = text[:start] + text[text.index(anchor, start):]
    if anchor not in text:
        raise ValueError("BlueALSA package evaluation anchor missing")
    recipe.write_text(text.replace(anchor, experiment_options + anchor, 1))
    # BlueALSA 5.0.0 places -static in AM_CFLAGS. With Buildroot's normal
    # shared D-Bus/ALSA/BlueZ stack that flag reaches the bluealsad link and
    # makes the ARM build reject dynamic libraries. Keep the stack dynamic and
    # carry this narrowly scoped, source-pinned integration fix.
    (package / "0001-bluealsad-use-shared-system-libraries.patch").write_text(
        """--- a/src/Makefile.am
+++ b/src/Makefile.am
@@ -171,8 +171,7 @@
\t@MPG123_CFLAGS@ \\
\t@OPUS_CFLAGS@ \\
\t@SBC_CFLAGS@ \\
-\t@SPANDSP_CFLAGS@ \\
-\t-static
+\t@SPANDSP_CFLAGS@

 libbluealsad_la_LIBADD = \\
\tshared/libshared.la \\
"""
    )
    replace_archive_hash(package / "bluez-alsa.hash", "bluez-alsa-4.3.1.tar.gz",
                         f"bluez-alsa-{BLUEZ_ALSA_VERSION}.tar.gz", BLUEZ_ALSA_SHA256)
    replace_once(
        package / "bluez-alsa.hash",
        "956564dcf06ba65cd7a5eb6cdfd695e2ab0f09ea9684e4eaf079f5d533bd206d",
        "47a67fe6b44d4aff90c837ebce38814bf89fdf5efd6bc6c9c078f960c35f61b8",
    )


def bootlin_toolchain(buildroot_source: Path) -> None:
    """Bootlin armv7-eabihf glibc stable 2025.08-1 -> 2026.08-1 (GCC 15.3, glibc 2.44)."""
    base = buildroot_source / "toolchain/toolchain-external/toolchain-external-bootlin"
    options = base / "Config.in.options"
    text = options.read_text()
    start = text.index("config BR2_TOOLCHAIN_EXTERNAL_BOOTLIN_ARMV7_EABIHF_GLIBC_STABLE\n")
    end = text.index("\nconfig ", start + 1)
    block = text[start:end]
    updated = (block.replace('bool "armv7-eabihf glibc stable 2025.08-1"',
                             f'bool "armv7-eabihf glibc stable {TOOLCHAIN_VERSION}"')
                    .replace("\tdepends on !BR2_ARCH_NEEDS_GCC_AT_LEAST_15\n", "")
                    .replace("select BR2_TOOLCHAIN_GCC_AT_LEAST_14\n", "select BR2_TOOLCHAIN_GCC_AT_LEAST_15\n")
                    # Buildroot checks a Bootlin toolchain's headers strictly: 2026.08-1 has 5.10.
                    .replace("select BR2_TOOLCHAIN_HEADERS_AT_LEAST_5_4\n", "select BR2_TOOLCHAIN_HEADERS_AT_LEAST_5_10\n"))
    if (f"stable {TOOLCHAIN_VERSION}" not in updated or "GCC_AT_LEAST_15" not in updated
            or "HEADERS_AT_LEAST_5_10" not in updated):
        raise ValueError("Bootlin armv7-eabihf glibc stable option anchor missing")
    options.write_text(text[:start] + updated + text[end:])
    recipe = base / "toolchain-external-bootlin.mk"
    text = recipe.read_text()
    anchor = "ifeq ($(BR2_TOOLCHAIN_EXTERNAL_BOOTLIN_ARMV7_EABIHF_GLIBC_STABLE),y)\nTOOLCHAIN_EXTERNAL_BOOTLIN_VERSION = 2025.08-1\n"
    if anchor in text:
        recipe.write_text(text.replace(anchor, anchor.replace("2025.08-1", TOOLCHAIN_VERSION), 1))
    elif anchor.replace("2025.08-1", TOOLCHAIN_VERSION) not in text:
        raise ValueError("Bootlin armv7-eabihf glibc stable version anchor missing")
    replace_archive_hash(base / "toolchain-external-bootlin.hash",
                         "armv7-eabihf--glibc--stable-2025.08-1.tar.xz",
                         f"armv7-eabihf--glibc--stable-{TOOLCHAIN_VERSION}.tar.xz", TOOLCHAIN_SHA256)


# License files that changed with a bump (checked by `make legal-info`): the
# archives are pinned above; only the copyright years moved.
LICENSE_HASHES = (
    ("python-mako", "LICENSE", "68d7064e53e3ffa8e9f420a9e604bd2e2059d9c330492fa6ebd4eb3d48111689",
     "8c1d171bc5d341ad9cd33d5e2c39b96a82c7bd49b3ae89a470c37e5822232054"),
)


def apply(buildroot_source: Path) -> None:
    for row in BUMPS:
        bump(buildroot_source, *row)
    for row in PYPI_BUMPS:
        bump_pypi(buildroot_source, *row)
    for name, file, old, new in LICENSE_HASHES:
        replace_once(buildroot_source / "package" / name / f"{name}.hash", f"{old}  {file}", f"{new}  {file}")
    fdk_aac_free(buildroot_source)
    bluez_alsa(buildroot_source)
    ca_certificates(buildroot_source)
    bootlin_toolchain(buildroot_source)
    # Local product-policy patches follow the pinned package security fixes.
    # python3: Buildroot's 3.14 patches 0002/0005 rebased onto 3.15.0 (same
    # names, so they replace the originals; the series applies with fuzz 0).
    for package in ('dropbear', 'openssh', 'bluez-alsa', 'python3'):
        for policy in (Path(__file__).resolve().parents[2] / 'buildroot/patches' / package).glob('*.patch'):
            (buildroot_source / 'package' / package / policy.name).write_bytes(policy.read_bytes())


if __name__ == "__main__":
    import sys

    apply(Path(sys.argv[1]))
