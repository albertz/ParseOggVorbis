#!/usr/bin/env python3

import sys
import os
from glob import glob
from utils import c_compile, install_better_exchook
from argparse import ArgumentParser

my_dir = os.path.dirname(os.path.abspath(__file__))
src_dir = "%s/src" % my_dir
lib_name = "ParseOggVorbis"
lib_ext = "so"
if sys.platform == "darwin":
    lib_ext = "dylib"
lib_filename = "%s.%s" % (lib_name, lib_ext)


def compile_lib():
    src_files = glob("%s/*.cpp" % src_dir)
    src_files.remove("%s/main.cpp" % src_dir)
    c_compile(
        src_files=src_files,
        common_opts=["-I", src_dir, "-fpic"],
        link_opts=["-shared"],
        out_filename=lib_filename)


def main():
    arg_parser = ArgumentParser()
    args = arg_parser.parse_args()
    compile_lib()


if __name__ == '__main__':
    install_better_exchook()
    main()
