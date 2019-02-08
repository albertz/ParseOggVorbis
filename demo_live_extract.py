#!/usr/bin/env python3

import os
import sys

if __name__ == '__main__':
    # https://stackoverflow.com/questions/54576879/
    __path__ = [os.path.dirname(os.path.abspath(__file__))]

import cffi
from argparse import ArgumentParser
from threading import Thread, Condition
from _thread import interrupt_main
import io
import struct
from collections import defaultdict
import numpy
from concurrent.futures import ThreadPoolExecutor
from .utils import install_better_exchook


class ParseOggVorbisLib:
    lib_name = "ParseOggVorbis"
    lib_ext = "so"
    if sys.platform == "darwin":
        lib_ext = "dylib"
    lib_filename = "./%s.%s" % (lib_name, lib_ext)

    def __init__(self, lib_filename=None):
        """
        :param str|None lib_filename:
        """
        if lib_filename:
            self.lib_filename = lib_filename
        assert os.path.exists(self.lib_filename), "maybe run `./compile_lib_simple.py`"
        self.ffi = cffi.FFI()
        self.ffi.cdef("""
            int ogg_vorbis_full_read_from_memory(const char* data, size_t data_len, const char** error_out);
            void set_data_output_file(const char* fn);
            void set_data_filter(const char** allowed_names);
            """)
        self.lib = self.ffi.dlopen(self.lib_filename)

    def set_data_filter(self, data_names):
        """
        Possible interesting values:
        From setup:

          floor1_unpack multiplier, floor1_unpack xs

        From each audio frame:

          floor_number
          floor1 ys, floor1 final_ys, floor1 floor, floor_outputs
          after_residue, after_envelope, pcm_after_mdct

        after_envelope is the last before MDCT.

        :param list[str] data_names:
        """
        self.lib.set_data_filter(
            [self.ffi.new("char[]", s.encode("utf8")) for s in data_names] + [self.ffi.NULL])

    def decode_ogg_vorbis(self, raw_bytes, data_filter=None):
        """
        :param bytes raw_bytes:
        :param list[str]|None data_filter:
        :rtype: CallbacksOutputReader
        """
        if data_filter:
            self.set_data_filter(data_filter)

        callback_data_collector = _BackgroundReader()
        callback_data_collector.start()

        self.lib.set_data_output_file(
            self.ffi.new("char[]", ("/dev/fd/%i" % callback_data_collector.write_fd).encode("utf8")))

        error_out = self.ffi.new("char**")
        res = self.lib.ogg_vorbis_full_read_from_memory(
            self.ffi.new("char[]", raw_bytes), len(raw_bytes), error_out)
        if res:
            # This means we got an error.
            raise Exception(
                "ParseOggVorbisLib ogg_vorbis_full_read_from_memory error: %s" % (
                    self.ffi.string(error_out[0]).decode("utf8")))

        callback_data_collector.wait()
        callback_data_collector.buffer.seek(0)
        reader = CallbacksOutputReader(file=callback_data_collector.buffer)
        return reader


class _BackgroundReader(Thread):
    daemon = True

    def __init__(self):
        super(_BackgroundReader, self).__init__()
        self.cond = Condition()
        self.read_fd, self.write_fd = os.pipe()
        self.buffer = io.BytesIO()
        self.finished = False

    def run(self):
        # noinspection PyBroadException
        try:
            while True:
                buf = os.read(self.read_fd, 1024 * 1024)
                if not buf:
                    break
                self.buffer.write(buf)
        except Exception:
            sys.excepthook(*sys.exc_info())
            interrupt_main()
        finally:
            with self.cond:
                self.finished = True
                os.close(self.read_fd)
                self.cond.notify_all()

    def wait(self):
        """
        Call this when you know that everything has been written to self.write_fd.
        This waits until the thread has read everything from self.read_fd.
        """
        with self.cond:
            os.close(self.write_fd)
            while True:
                if self.finished:
                    return
                self.cond.wait()


class CallbacksOutputReader:
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
        assert 1 <= type_id <= 7
        elem_size, = struct.unpack("B", self.raw_read(expect_size=1))
        raw_data = self.raw_read()
        assert len(raw_data) % elem_size == 0
        num_elem = len(raw_data) / elem_size
        raw_unpack_type_id = {1: "f", 2: "i", 3: "I", 4: "B", 5: "B", 6: "q", 7: "Q"}
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

    def read_floor_ys(self, output_dim, include_floor_number=None, only_biggest_floor=False, verbose=0):
        """
        :param int output_dim:
        :param bool include_floor_number:
        :param bool only_biggest_floor:
        :param int verbose:
        :return: float values in [-1,1], shape (time,dim)
        :rtype: numpy.ndarray
        """
        if only_biggest_floor:
            assert include_floor_number in (None, False)
            include_floor_number = False
        if include_floor_number is None:
            include_floor_number = True
        floor_multipliers = []
        floor_xs = []
        while True:
            name, channel, data = self.read_entry()
            if name == "floor1_unpack multiplier":
                assert len(data) == 1
                floor_multipliers.append(data[0])
            if name == "floor1_unpack xs":
                floor_xs.append(data)
            if name == "finish_setup":
                break
        assert len(floor_multipliers) == len(floor_xs) > 0
        res_float = numpy.zeros((500, output_dim), dtype="float32")
        num_floors = len(floor_xs)
        biggest_floor_idx = max(range(num_floors), key=lambda i: len(floor_xs[i]))
        dim = output_dim
        if include_floor_number:
            dim -= 1
        if verbose:
            if verbose >= 5:
                for i in range(num_floors):
                    print("Floor %i/%i, multiplier %i, xs: %r" % (i + 1, num_floors, floor_multipliers[i], floor_xs[i]))
                print("Biggest floor: %i, len(xs) = %i" % (biggest_floor_idx + 1, len(floor_xs[biggest_floor_idx])))
            if dim > len(floor_xs[biggest_floor_idx]):
                print("Warning: Dim = %i > len(biggest floor xs) = %i" % (dim, len(floor_xs[biggest_floor_idx])))
        recent_floor_number = None
        frame_num = 0
        while True:
            try:
                name, channel, data = self.read_entry()
            except EOFError:
                break
            if name == "floor_number":
                recent_floor_number = data[0]
                assert 0 <= recent_floor_number < len(floor_xs)
            if name in {"floor1 ys", "floor1 final_ys"}:
                assert recent_floor_number is not None
                if only_biggest_floor and recent_floor_number != biggest_floor_idx:
                    continue
                assert len(data) == len(floor_xs[recent_floor_number])
                # values [0..255]
                data_int = numpy.array(data[:dim], dtype="float32") * floor_multipliers[recent_floor_number]
                # values [-1.0,1.0]
                data_float = (data_int.astype("float32") - 127.5) / 127.5
                frame_float = numpy.zeros((output_dim,), dtype="float32")
                offset_dim = 0
                if include_floor_number:
                    frame_float[0] = (recent_floor_number + 1.0) / num_floors - 0.5  # (-0.5,0.5)
                    offset_dim = 1
                frame_float[offset_dim:offset_dim + frame_float.shape[0]] = data_float
                if frame_num >= res_float.shape[0]:
                    res_float = numpy.concatenate([res_float, numpy.zeros_like(res_float)], axis=0)
                res_float[frame_num] = frame_float
                frame_num += 1
        return res_float[:frame_num]


def _do_file(lib, args, fn=None, reader=None, raw_bytes=None):
    """
    :param ParseOggVorbisLib lib:
    :param bytes|None raw_bytes:
    :param args:
    :param str|None fn:
    :param CallbacksOutputReader|None reader:
    """
    if fn:
        print(fn)

    if not reader:
        assert raw_bytes is not None
        reader = lib.decode_ogg_vorbis(raw_bytes, data_filter=args.filter)
    else:
        assert raw_bytes is None

    if args.mode == "dump":
        entry_name_counts = defaultdict(int)
        while True:
            try:
                name, channel, data = reader.read_entry()
            except EOFError:
                break
            entry_name_counts[name] += 1
            reader.dump_entry(name, channel, data)
        print("Entry name counts:", dict(entry_name_counts))

    elif args.mode == "floor_ys":
        assert args.output_dim
        res = reader.read_floor_ys(output_dim=args.output_dim)
        print("res shape:", res.shape)
        print("res:")
        print(res)

    else:
        raise Exception("invalid mode %r" % (args.mode,))


def main():
    arg_parser = ArgumentParser()
    arg_parser.add_argument("file")
    arg_parser.add_argument(
        "--filter", nargs="*", default=[
            "floor1_unpack multiplier", "floor1_unpack xs", "finish_setup",
            "floor_number", "floor1 final_ys", "finish_audio_packet"])
    arg_parser.add_argument("--mode", default="dump")
    arg_parser.add_argument("--output_dim", type=int)
    arg_parser.add_argument("--multi_threaded", action="store_true")
    args = arg_parser.parse_args()

    lib = ParseOggVorbisLib()

    if args.file.endswith(".zip"):
        print("Got a ZIP file, iterating through all OGG inside.")
        import zipfile
        ogg_count = 0
        with zipfile.ZipFile(args.file) as zip_f:
            if args.multi_threaded:
                fns_futures = {}  # dict fn -> future of reader
                with ThreadPoolExecutor(max_workers=10) as executor:
                    for fn in zip_f.namelist():
                        if fn.endswith(".ogg"):
                            fns_futures[fn] = executor.submit(
                                lib.decode_ogg_vorbis, raw_bytes=zip_f.read(fn), data_filter=args.filter)
                    for fn in zip_f.namelist():
                        ogg_count += 1
                        if fn.endswith(".ogg"):
                            _do_file(lib, args=args, reader=fns_futures[fn].result(), fn=fn)
            else:
                for fn in zip_f.namelist():
                    ogg_count += 1
                    if fn.endswith(".ogg"):
                        _do_file(lib, args=args, raw_bytes=zip_f.read(fn), fn=fn)

        print("Found %i OGG files." % ogg_count)

    else:
        raw_bytes_in_memory_value = open(args.file, "rb").read()
        _do_file(lib, raw_bytes=raw_bytes_in_memory_value, args=args)

    print("Finished")


if __name__ == '__main__':
    install_better_exchook()
    main()
