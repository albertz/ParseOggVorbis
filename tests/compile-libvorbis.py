#!/usr/bin/env python3

import os
import sys
from subprocess import check_call
import argparse

import better_exchook
better_exchook.install()

libvorbis_dir = "libvorbis-1.3.6"
libogg_dir = "libogg-1.3.3"

def call(args):
    print("$ %s" % " ".join(args))
    check_call(args)


assert os.path.exists(libvorbis_dir)
assert os.path.exists(libogg_dir)

ogg_c_files = [
    "bitwise.c",
    "framing.c"
]
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


def compile_direct():
    cmd = ["cc"]
    cmd += ["-I", "%s/include" % libogg_dir, "-I", "%s/include" % libvorbis_dir]
    cmd += ["%s/src/%s" % (libogg_dir, fn) for fn in ogg_c_files]
    cmd += ["%s/lib/%s" % (libvorbis_dir, fn) for fn in vorbis_c_files]
    cmd += ["libvorbis-demo.c"]
    check_call(cmd)


def main():
    argparser = argparse.ArgumentParser()
    argparser.add_argument("--mode", required=True, help="direct or copy")
    args = argparser.parse_args()
    if args.mode == "direct":
        compile_direct()
    elif args.mode == "copy":
        raise NotImplementedError
    else:
        raise Exception("invalid mode %r" % args.mode)


if __name__ == "__main__":
    main()
