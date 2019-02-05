#!/usr/bin/env python3

import os
import sys
import cffi
from utils import install_better_exchook
import io
from argparse import ArgumentParser


install_better_exchook()
arg_parser = ArgumentParser()
arg_parser.add_argument("file")
args = arg_parser.parse_args()

raw_bytes_in_memory_value = open(args.file, "rb").read()


lib_name = "ParseOggVorbis"
lib_ext = "so"
if sys.platform == "darwin":
    lib_ext = "dylib"
lib_filename = "%s.%s" % (lib_name, lib_ext)

ffi = cffi.FFI()
ffi.cdef("""
int ogg_vorbis_full_read_from_memory(const char* data, size_t data_len, const char** error_out);
void set_data_output_file(const char* fn);
void set_data_filter(const char** allowed_names);
""")
lib = ffi.dlopen("ParseOggVorbis.dylib")

# Possible interesting values:
# From setup:
# floor1_unpack multiplier, floor1_unpack xs
# From each audio frame:
# floor_number
# floor1 ys, floor1 final_ys, floor1 floor, floor_outputs
# after_residue, after_envelope, pcm_after_mdct
# after_envelope is the last before MDCT.
print("set_data_filter")
lib.set_data_filter(
    [ffi.new("char[]", s.encode("utf8")) for s in [
        "floor1_unpack multiplier", "floor1_unpack xs", "floor1 ys"]] + [ffi.NULL])

print("ogg_vorbis_full_read_from_memory")
error_out = ffi.new("char**")
res = lib.ogg_vorbis_full_read_from_memory(
    ffi.new("char[]", raw_bytes_in_memory_value), len(raw_bytes_in_memory_value), error_out)
print("res:", res)
if res:
    # This means we got an error.
    print(ffi.string(error_out[0]).decode("utf8"))
