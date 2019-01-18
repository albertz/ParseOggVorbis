#!/usr/bin/env python3

import os
import sys
from subprocess import check_call

import better_exchook
better_exchook.install()

libvorbis_dir = "libvorbis-1.3.6"
libogg_dir = "libogg-1.3.3"

def call(args):
    print("$ %s" % " ".join(args))
    check_call(args)


assert os.path.exists(libvorbis_dir)
assert os.path.exists(libogg_dir)

c_files = [
    "window.c",
    "floor0.c", "floor1.c",
    "codebook.c",
    "sharedbook.c",
    "block.c",
    "registry.c",
    "envelope.c"
]
cmd = ["cc"]
cmd += ["-I", "%s/include" % libogg_dir, "-I", "%s/include" % libvorbis_dir]
cmd += ["%s/lib/%s" % (libvorbis_dir, fn) for fn in c_files]
cmd += ["libvorbis-demo.c"]
check_call(cmd)
