#!/usr/bin/env python3

import os
import sys
from subprocess import check_call
import argparse
import shutil
from glob import glob

import better_exchook
better_exchook.install()

libvorbis_dir = "libvorbis-1.3.6"
libogg_dir = "libogg-1.3.3"
standalone_dir = "libvorbis-standalone"


def call(args):
    print("$ %s" % " ".join(args))
    check_call(args)


assert os.path.exists(libvorbis_dir)
assert os.path.exists(libogg_dir)

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
    cmd = ["cc"]
    cmd += ["-I", "%s/include" % libogg_dir, "-I", "%s/include" % libvorbis_dir]
    cmd += ogg_c_files
    cmd += vorbis_c_files
    # TODO need to split cc/c++...
    cmd += ["libvorbis-demo.cpp"]
    check_call(cmd)


def compile_standalone():
    copy_to_standalone()
    cmd = ["cc"]
    cmd += ["-I", standalone_dir, "-I", "../src"]
    cmd += glob("%s/*.c" % standalone_dir)
    # TODO need to split cc/c++...
    cmd += ["libvorbis-demo.cpp"]
    check_call(cmd)


def main():
    argparser = argparse.ArgumentParser()
    argparser.add_argument("--mode", required=True, help="direct or copy")
    args = argparser.parse_args()
    if args.mode == "direct":
        compile_direct()
    elif args.mode == "standalone":
        compile_standalone()
    else:
        raise Exception("invalid mode %r" % args.mode)


if __name__ == "__main__":
    main()
