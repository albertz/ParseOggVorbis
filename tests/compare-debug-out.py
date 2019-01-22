#!/usr/bin/env python3

import argparse
import struct
import better_exchook
import typing
import tempfile
import subprocess
import os


def create_debug_out(exec_path, ogg_filename):
    """
    :param str exec_path: either to libvorbis-standalone or our own tool (ParseOggVorbis)
    :param str ogg_filename:
    :return: debug out filename
    :rtype: str
    """
    assert os.path.exists(exec_path)
    assert os.path.exists(ogg_filename)
    f = tempfile.NamedTemporaryFile()
    debug_out_fn = f.name
    f.close()
    assert not os.path.exists(debug_out_fn)
    cmd = [exec_path, "--in", ogg_filename, "--debug_out", debug_out_fn]
    print("$ %s" % " ".join(cmd))
    subprocess.check_call(cmd)
    assert os.path.exists(debug_out_fn)
    return debug_out_fn


class Floor1:
    def __init__(self, multiplier, xs):
        """
        :param int multiplier:
        :param tuple[int] xs:
        """
        self.multiplier = multiplier
        self.xs = xs


class Reader:
    def __init__(self, filename):
        """
        :param str filename:
        """
        self.filename = filename
        self.file = open(filename, "rb")
        header_str = self.raw_read().decode("utf8")
        assert header_str == "ParseOggVorbis-header-v1"
        self.decoder_name = self.read_str_expect_key("decoder-name")
        self.decoder_sample_rate = self.read_single_int_expect_key("decoder-sample-rate")
        self.decoder_num_channels = self.read_single_int_expect_key("decoder-num-channels")
        self.floors = []  # type: typing.List[Floor1]

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

    def read_setup(self, dump):
        """
        :param bool dump:
        """
        while True:
            name, channel, data = self.read_entry()
            if dump:
                self.dump_entry(name, channel, data)
            if name == "finish_setup":
                break
            assert name == "floor1_unpack multiplier"
            multiplier, = data
            assert isinstance(multiplier, int)
            name, channel, xs = self.read_entry()
            if dump:
                self.dump_entry(name, channel, xs)
            assert name == "floor1_unpack xs"
            assert len(xs) > 0
            assert isinstance(xs[0], int)
            self.floors.append(Floor1(multiplier=multiplier, xs=xs))


def main():
    better_exchook.install()
    arg_parser = argparse.ArgumentParser()
    arg_parser.add_argument("--ogg")
    arg_parser.add_argument("--ourexec")
    arg_parser.add_argument("--libvorbisexec")
    arg_parser.add_argument("--ourout")
    arg_parser.add_argument("--libvorbisout")
    arg_parser.add_argument("--dump_stdout", action="store_true")
    args = arg_parser.parse_args()

    if args.ogg:
        assert args.ourexec and not args.ourout
        args.ourout = create_debug_out(args.ourexec, args.ogg)
        if args.libvorbisexec:
            assert not args.libvorbisout
            args.libvorbisout = create_debug_out(args.libvorbisexec, args.ogg)

    reader1 = Reader(args.ourout)
    print("Read our (ParseOggVorbis) debug out file:", reader1.filename)
    print("Our decoder name:", reader1.decoder_name)
    print("Our num channels:", reader1.decoder_num_channels)
    print("Our sample rate:", reader1.decoder_sample_rate)

    reader2 = None
    if args.libvorbisout:
        reader2 = Reader(args.libvorbisout)
        print("Read libvorbis debug out file:", reader2.filename)
        print("libvorbis decoder name:", reader2.decoder_name)
        print("libvorbis num channels:", reader2.decoder_num_channels)
        print("libvorbis sample rate:", reader2.decoder_sample_rate)
        assert reader2.decoder_sample_rate == reader1.decoder_sample_rate
        assert reader2.decoder_num_channels == reader1.decoder_num_channels

    reader1.read_setup(dump=args.dump_stdout)
    if reader2:
        reader2.read_setup(dump=args.dump_stdout)
        assert len(reader1.floors) == len(reader2.floors)
        for f1, f2 in zip(reader1.floors, reader2.floors):
            assert f1.multiplier == f2.multiplier
            assert len(f1.xs) == len(f2.xs)
            for x1, x2 in zip(f1.xs, f2.xs):
                assert x1 == x2

    while True:
        try:
            name, channel, data = reader1.read_entry()
        except EOFError:
            print("Reached EOF.")
            break
        if args.dump_stdout:
            reader1.dump_entry(name, channel, data)


if __name__ == '__main__':
    main()
