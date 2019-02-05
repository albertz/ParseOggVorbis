#!/usr/bin/env python3

import os
import sys
import cffi
from utils import install_better_exchook
from argparse import ArgumentParser
from threading import Thread, Condition
import io
import struct
import typing


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


class BackgroundReader(Thread):
    def __init__(self):
        super(BackgroundReader, self).__init__()
        self.cond = Condition()
        self.read_fd, self.write_fd = os.pipe()
        self.buffer = io.BytesIO()
        self.finished = False

    def run(self):
        try:
            while True:
                buf = os.read(self.read_fd, 1024 * 1024)
                if not buf:
                    print("Finished reading.")
                    break
                self.buffer.write(buf)
        except Exception:
            sys.excepthook(*sys.exc_info())
        finally:
            with self.cond:
                self.finished = True
                self.cond.notify_all()

    def wait(self):
        with self.cond:
            os.close(self.write_fd)
            os.close(self.read_fd)
            while True:
                if self.finished:
                    return
                self.cond.wait()


callback_data_collector = BackgroundReader()
callback_data_collector.start()

print("set_data_output_file")
lib.set_data_output_file(ffi.new("char[]", ("/dev/fd/%i" % callback_data_collector.write_fd).encode("utf8")))

print("ogg_vorbis_full_read_from_memory")
error_out = ffi.new("char**")
res = lib.ogg_vorbis_full_read_from_memory(
    ffi.new("char[]", raw_bytes_in_memory_value), len(raw_bytes_in_memory_value), error_out)
if res:
    # This means we got an error.
    print("Error:", ffi.string(error_out[0]).decode("utf8"))
    sys.exit(1)
print("Ok.")

callback_data_collector.wait()
print("Got bytes:", len(callback_data_collector.buffer.getvalue()))


class Reader:
    def __init__(self, file):
        """
        :param io.BytesIO file:
        """
        self.file = file
        header_str = self.raw_read().decode("utf8")
        assert header_str == "ParseOggVorbis-header-v1"
        self.decoder_name = self.read_str_expect_key("decoder-name")
        self.decoder_sample_rate = self.read_single_int_expect_key("decoder-sample-rate")
        self.decoder_num_channels = self.read_single_int_expect_key("decoder-num-channels")

    def raw_read(self, expect_size=None):
        """
        Matches one raw_write_to_file() in C++.

        :param int|None expect_size:
        :rtype: bytes
        """
        raw_size = self.file.read(4)
        if len(raw_size) == 0:
            raise EOFError  # not really an error, depending on the state
        size, = struct.unpack("I", raw_size)
        if expect_size is not None:
            assert size == expect_size
        raw_data = self.file.read(size)
        assert len(raw_data) == size
        return raw_data

    def read(self, return_uint8_as_bytes=False, return_uint8_as_str=False):
        """
        Matches one write_to_file() in C++.

        :param bool return_uint8_as_bytes:
        :param bool return_uint8_as_str:
        :return: key, data
        :rtype: (str, tuple[float|int]|bytes|str)
        """
        key = self.raw_read()
        key = key.decode("utf8")
        type_id, = struct.unpack("B", self.raw_read(expect_size=1))
        assert 1 <= type_id <= 5
        elem_size, = struct.unpack("B", self.raw_read(expect_size=1))
        raw_data = self.raw_read()
        assert len(raw_data) % elem_size == 0
        num_elem = len(raw_data) / elem_size
        raw_unpack_type_id = {1: "f", 2: "i", 3: "I", 4: "B", 5: "B"}
        assert struct.calcsize(raw_unpack_type_id[type_id]) == elem_size
        if type_id == 4:  # uint8 data
            if return_uint8_as_bytes:
                return key, raw_data  # just return as-is
            if return_uint8_as_str:
                return key, raw_data.decode("utf8")
        assert not return_uint8_as_bytes and not return_uint8_as_str, "got type %i" % type_id
        struct_fmt = "%i%s" % (num_elem, raw_unpack_type_id[type_id])
        assert struct.calcsize(struct_fmt) == len(raw_data)
        data = struct.unpack(struct_fmt, raw_data)
        assert isinstance(data, tuple)
        assert len(data) == num_elem
        return key, data

    def read_expect_key(self, expected_key, **kwargs):
        """
        :param str expected_key:
        :param kwargs: passed to self.read
        :return: data
        :rtype: tuple[float|int]|bytes|str
        """
        key, value = self.read(**kwargs)
        assert key == expected_key
        return value

    def read_str_expect_key(self, expected_key):
        """
        :param str expected_key:
        :rtype: str
        """
        key, value = self.read(return_uint8_as_str=True)
        assert key == expected_key
        assert isinstance(value, str)
        return value

    def read_single_int_expect_key(self, expected_key):
        """
        :param str expected_key:
        :rtype: int
        """
        key, value = self.read()
        assert key == expected_key
        assert isinstance(value, tuple)
        assert len(value) == 1
        value, = value
        assert isinstance(value, int)
        return value

    def read_entry(self):
        """
        Matches push_data_file_T() in C++.

        :return: name, channel, data
        :rtype: (str, int|None, tuple[float|int])
        """
        name = self.read_str_expect_key("entry-name")
        key, value = self.read()
        if key == "entry-channel":
            assert isinstance(value, tuple) and len(value) == 1
            channel, = value
            assert isinstance(channel, int)
            key, value = self.read()
        else:
            channel = None
        assert key == "entry-data"
        return name, channel, value

    def dump_entry(self, name, channel, data):
        """
        :param str name:
        :param int|None channel:
        :param tuple[float|int] data:
        """
        if len(data) > 10:
            data_repr = repr(list(data[:10])) + "..."
        else:
            data_repr = repr(list(data))
        print("Decoder %r name=%r channel=%r data=%s len=%i" % (self.decoder_name, name, channel, data_repr, len(data)))


callback_data_collector.buffer.seek(0)
reader = Reader(file=callback_data_collector.buffer)

while True:
    try:
        name, channel, data = reader.read_entry()
    except EOFError:
        break
    reader.dump_entry(name, channel, data)

print("Finished")
