#!/usr/bin/env python3

import os
import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from utils import c_compile, install_better_exchook
import argparse
import shutil
from glob import glob


libvorbis_dir = "libvorbis-1.3.6"
libogg_dir = "libogg-1.3.3"
standalone_dir = "libvorbis-standalone"
src_dir = "../src"


ogg_c_files = [
    "bitwise.c",
    "framing.c"
]
ogg_c_files = ["%s/src/%s" % (libogg_dir, fn) for fn in ogg_c_files]
vorbis_c_files = [
    # lib files
    "info.c",
    "window.c",
    "floor0.c", "floor1.c",
    "codebook.c",
    "sharedbook.c",
    "block.c",
    "registry.c",
    "envelope.c",
    "bitrate.c",
    "psy.c",
    "smallft.c",
    "mapping0.c",
    "res0.c",
    "lpc.c",
    "lsp.c",
    "mdct.c",
    "synthesis.c",
    # vorbisfile extra
    "vorbisfile.c"
]
vorbis_c_files = ["%s/lib/%s" % (libvorbis_dir, fn) for fn in vorbis_c_files]
ogg_h_files = [
    "ogg/ogg.h",
    "ogg/os_types.h"
]
vorbis_h_files = [
    "vorbis/codec.h",
    "vorbis/vorbisfile.h",
]
vorbis_intern_h_files = [
    "smallft.h",
    "os.h",
    "misc.h",
    "codec_internal.h",
    "envelope.h",
    "bitrate.h",
    "highlevel.h",
    "psy.h",
    "backends.h",
    "mdct.h",
    "codebook.h",
    "scales.h",
    "lsp.h",
    "window.h",
    "lookup.h",
    "lpc.h",
    "registry.h",
    "masking.h",
]


def copy_to_standalone():
    os.makedirs(standalone_dir, exist_ok=True)
    copy_file_list = []  # (src,dst)
    for fn in ["COPYING"]:
        copy_file_list += [("%s/%s" % (libvorbis_dir, fn), "%s/%s" % (standalone_dir, fn))]
    for fn in ogg_c_files:
        copy_file_list += [(fn, "%s/ogg_%s" % (standalone_dir, os.path.basename(fn)))]
    for fn in vorbis_c_files:
        copy_file_list += [(fn, "%s/vorbis_%s" % (standalone_dir, os.path.basename(fn)))]
    for fn in ogg_h_files:
        copy_file_list += [("%s/include/%s" % (libogg_dir, fn), "%s/%s" % (standalone_dir, fn))]
    for fn in vorbis_h_files:
        copy_file_list += [("%s/include/%s" % (libvorbis_dir, fn), "%s/%s" % (standalone_dir, fn))]
    for fn in vorbis_intern_h_files:
        copy_file_list += [("%s/lib/%s" % (libvorbis_dir, fn), "%s/%s" % (standalone_dir, fn))]
    for src, dst in copy_file_list:
        if not os.path.exists(os.path.dirname(dst)):
            os.makedirs(os.path.dirname(dst))
        if os.path.exists(dst):
            continue
        assert os.path.exists(src)
        shutil.copy(src, dst)


def compile_direct():
    c_compile(
        src_files=ogg_c_files + vorbis_c_files + ["libvorbis-demo.cpp", "%s/Callbacks.cpp" % src_dir],
        common_opts=["-I", "%s/include" % libogg_dir, "-I", "%s/include" % libvorbis_dir, "-I", src_dir],
        out_filename="libvorbis-direct.bin")


def compile_standalone():
    copy_to_standalone()
    c_compile(
        src_files=glob("%s/*.c" % standalone_dir) + ["libvorbis-demo.cpp", "%s/Callbacks.cpp" % src_dir],
        common_opts=["-I", standalone_dir, "-I", src_dir],
        out_filename="libvorbis-standalone.bin")


def compile_ours():
    c_compile(
        src_files=glob("%s/*.cpp" % src_dir),
        common_opts=["-I", src_dir],
        out_filename="ours.bin")


def main():
    argparser = argparse.ArgumentParser()
    argparser.add_argument("--mode", required=True, help="direct or standalone or ours")
    args = argparser.parse_args()
    compile_modes = {"direct": compile_direct, "standalone": compile_standalone, "ours": compile_ours}
    assert args.mode in compile_modes, "invalid mode %r, available modes: %r" % (args.mode, list(compile_modes.keys()))
    compile_modes[args.mode]()


if __name__ == "__main__":
    install_better_exchook()
    main()
