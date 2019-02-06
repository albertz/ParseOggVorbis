#!/usr/bin/env python3

import argparse
import struct
import better_exchook
import typing
import tempfile
import subprocess
import os
import sys
import numpy


my_dir = os.path.dirname(__file__) or "."
default_ours_exec = "%s/ours.bin" % my_dir
default_libvorbis_exec = "%s/libvorbis-standalone.bin" % my_dir


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
    def __init__(self, number, multiplier, xs):
        """
        :param int number:
        :param int multiplier:
        :param tuple[int] xs:
        """
        self.number = number
        self.multiplier = multiplier
        self.xs = xs


def assert_same_list(l1, l2):
    """
    :param list|tuple l1:
    :param list|tuple l2:
    """
    if l1 is None or l2 is None:
        assert l1 is None and l2 is None
        return
    assert len(l1) == len(l2)
    for i, (y1, y2) in enumerate(zip(l1, l2)):
        assert y1 == y2, "different at pos %i: %r vs %r" % (i, l1, l2)


def short_list_repr(ls, pos, context=5):
    """
    :param list ls:
    :param int pos:
    :param int context:
    :rtype: str
    """
    start_pos = max(pos - context, 0)
    end_pos = min(pos + context + 1, len(ls))
    items = [("_%r_" % ls[i]) if i == pos else repr(ls[i]) for i in range(start_pos, end_pos)]
    if start_pos > 0:
        items.insert(0, "...")
    if end_pos < len(ls) - 1:
        items.append("...")
    return "[%s]" % ", ".join(items)


def assert_close_list(l1, l2, eps=1e-5):
    """
    :param list|tuple l1:
    :param list|tuple l2:
    :param float eps:
    """
    if l1 is None or l2 is None:
        assert l1 is None and l2 is None
        return
    assert len(l1) == len(l2)
    for i, (y1, y2) in enumerate(zip(l1, l2)):
        if abs(y1 - y2) >= eps:
            squared_error = numpy.sum((numpy.array(l1) - numpy.array(l2)) ** 2)
            mean_squared_error = squared_error / len(l1)
            diff = abs(y1 - y2)
            raise Exception(
                ("different at pos %i, diff %f > eps %f\n" % (i, diff, eps)) +
                ("mse: %f, se: %f,\nls1: %s,\nls2: %s" % (
                    mean_squared_error, squared_error, short_list_repr(l1, i), short_list_repr(l2, i))))


class FloorData:
    def __init__(self, channel, floor):
        """
        :param int channel:
        :param Floor1 floor:
        """
        self.channel = channel
        self.floor = floor
        self.ys = None  # type: typing.Union[typing.Tuple[int],None]

    @classmethod
    def assert_same(cls, self, other):
        """
        :param FloorData self:
        :param FloorData other:
        """
        assert self.channel == other.channel
        assert self.floor.number == other.floor.number
        assert_same_list(self.ys, other.ys)


class FloatData:
    """
    "after_reside" in the dump. Or "pcm_after_mdct". Or "after_envelope".
    """
    def __init__(self, channel, data):
        """
        :param int channel:
        :param tuple[float] data:
        """
        self.channel = channel
        self.data = data

    @classmethod
    def assert_same(cls, self, other):
        """
        :param FloatData self:
        :param FloatData other:
        """
        assert self.channel == other.channel
        assert_close_list(self.data, other.data)


class AudioPacket:
    def __init__(self, reader, dump):
        """
        :param Reader reader:
        :param bool dump:
        """
        self._push_back_entry_cache = []
        self.reader = reader
        self.dump = dump
        while True:
            try:
                name, channel, data = self._read_entry()
            except EOFError:
                self.eof = True
                return
            if name == "pcm":
                reader.add_pcm_data(channel=channel, pcm_data=data)
                continue
            break
        self.eof = False
        assert name == "start_audio_packet"
        self.floor_data = []  # type: typing.List[FloorData]
        self.residue_data = []  # type: typing.List[FloatData]
        self.before_mdct_data = []  # type: typing.List[FloatData]
        self.pcm_after_mdct_data = []  # type: typing.List[FloatData]
        while True:
            name, channel, data = self._read_entry()
            if name == "floor_number":
                assert len(data) == 1 and isinstance(data[0], int)
                self._read_floor_data(channel=channel, number=data[0])
            elif name == "after_residue":
                self.residue_data.append(FloatData(channel=channel, data=data))
            elif name == "after_envelope":
                self.before_mdct_data.append(FloatData(channel=channel, data=data))
            elif name == "pcm_after_mdct":
                self.pcm_after_mdct_data.append(FloatData(channel=channel, data=data))
            elif name == "finish_audio_packet":
                break
            elif name in ["floor1 final_ys", "floor1 step2_flag", "floor1 floor", "floor_outputs",
                          "floor1 fit_value unwrapped"]:
                pass  # unhandled for now...
            else:
                print("unknown entry %r" % name)
        assert len(self.pcm_after_mdct_data) == len(self.before_mdct_data)

    def _push_back_entry(self, *args):
        self._push_back_entry_cache.append(args)

    def _read_entry(self):
        if self._push_back_entry_cache:
            return self._push_back_entry_cache.pop(-1)
        name, channel, data = self.reader.read_entry()
        if self.dump:
            self.reader.dump_entry(name, channel, data)
        return name, channel, data

    def _read_floor_data(self, channel, number):
        """
        :param int channel:
        :param int number:
        """
        assert 0 <= number < len(self.reader.floors)
        floor = self.reader.floors[number]
        floor_data = FloorData(channel=channel, floor=floor)
        self.floor_data.append(floor_data)
        name, channel, data = self._read_entry()
        if name != "floor1 ys":
            # It is valid to have an empty floor, i.e. packet without audio.
            self._push_back_entry(name, channel, data)
            return
        floor_data.ys = data

    @classmethod
    def assert_same(cls, self, other):
        """
        :param AudioPacket self:
        :param AudioPacket other:
        """
        assert self.eof == other.eof
        if self.eof:
            return
        assert len(self.floor_data) == len(other.floor_data)
        for f1, f2 in zip(self.floor_data, other.floor_data):
            FloorData.assert_same(f1, f2)
        assert len(self.residue_data) == len(other.residue_data)
        for f1, f2 in zip(self.residue_data, other.residue_data):
            FloatData.assert_same(f1, f2)
        assert len(self.before_mdct_data) == len(other.before_mdct_data)
        for f1, f2 in zip(self.before_mdct_data, other.before_mdct_data):
            FloatData.assert_same(f1, f2)
        assert len(self.pcm_after_mdct_data) == len(other.pcm_after_mdct_data)
        for f1, f2 in zip(self.pcm_after_mdct_data, other.pcm_after_mdct_data):
            FloatData.assert_same(f1, f2)


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
        self.pcm_data = {}  # type: typing.Dict[int, typing.List[typing.Tuple[float, ...]]]
        self.num_samples = {}  # per channel

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
        Fills self.floors.

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
            self.floors.append(Floor1(number=len(self.floors), multiplier=multiplier, xs=xs))

    def read_audio_packet(self, dump):
        """
        :param bool dump:
        :rtype: AudioPacket
        """
        return AudioPacket(reader=self, dump=dump)

    def count_remaining_audio_packets(self, dump):
        """
        :param bool dump:
        :rtype: int
        """
        count = 0
        while True:
            packet = self.read_audio_packet(dump=dump)
            if packet.eof:
                return count
            else:
                count += 1

    def add_pcm_data(self, channel, pcm_data):
        """
        :param int channel:
        :param tuple[float] pcm_data: raw samples
        """
        self.pcm_data.setdefault(channel, []).append(pcm_data)
        self.num_samples.setdefault(channel, 0)
        self.num_samples[channel] += len(pcm_data)


def main():
    better_exchook.install()
    arg_parser = argparse.ArgumentParser()
    arg_parser.add_argument("--ogg")
    arg_parser.add_argument("--ourexec")
    arg_parser.add_argument("--libvorbisexec")
    arg_parser.add_argument("--ourout")
    arg_parser.add_argument("--libvorbisout")
    arg_parser.add_argument("--dump_stdout", action="store_true")
    arg_parser.add_argument("--no_stderr", action="store_true")
    args = arg_parser.parse_args()

    if args.no_stderr:
        sys.stderr = sys.stdout

    if args.ogg:
        assert not args.ourout, "--ogg xor --ourout.\n%s" % arg_parser.format_usage()
        if args.ourexec is None:
            assert os.path.exists(default_ours_exec), "run `compile-libvorbis.py --mode ours`"
            args.ourexec = default_ours_exec
        args.ourout = create_debug_out(args.ourexec, args.ogg)
        if args.libvorbisexec is None:
            assert os.path.exists(default_libvorbis_exec), "run `compile-libvorbis.py --mode standalone`"
            args.libvorbisexec = default_libvorbis_exec
        if args.libvorbisexec:
            assert not args.libvorbisout
            args.libvorbisout = create_debug_out(args.libvorbisexec, args.ogg)

    assert args.ourout, "need --ourout.\n%s" % arg_parser.format_usage()
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
        print("Will check that both are the same.")
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

    num_packets = 0
    while True:
        packet1 = reader1.read_audio_packet(dump=args.dump_stdout)
        if reader2:
            packet2 = reader2.read_audio_packet(dump=args.dump_stdout)
            try:
                AudioPacket.assert_same(packet1, packet2)
                assert sorted(reader1.pcm_data.keys()) == sorted(reader2.pcm_data.keys())
                for channel in sorted(reader1.pcm_data.keys()):
                    pcms1 = reader1.pcm_data[channel]
                    pcms2 = reader2.pcm_data[channel]
                    assert len(pcms1) == len(pcms2)
                    pcm1 = sum(pcms1, tuple())
                    pcm2 = sum(pcms2, tuple())
                    min_len = min(len(pcm1), len(pcm2))
                    assert_close_list(pcm1[:min_len], pcm2[:min_len])
                    pcms1.clear()
                    pcms2.clear()
                    if len(pcm1) > min_len:
                        pcms1.append(pcm1)
                    if len(pcm2) > min_len:
                        pcms2.append(pcm2)
                    if not pcms1:
                        del reader1.pcm_data[channel]
                    if not pcms2:
                        del reader2.pcm_data[channel]
            except Exception:
                print("Exception at packet %i, num samples %r." % (num_packets, reader1.num_samples))
                try:
                    num_remaining_packets1 = reader1.count_remaining_audio_packets(dump=args.dump_stdout)
                    num_remaining_packets2 = reader2.count_remaining_audio_packets(dump=args.dump_stdout)
                    print("Num remaining reader1: %i, reader2: %i" % (num_remaining_packets1, num_remaining_packets2))
                except Exception:
                    print("During count_remaining_audio_packets, another exception occured:")
                    better_exchook.better_exchook(*sys.exc_info())
                print("Reraising original exception now.")
                raise
        if packet1.eof:
            print("EOF")
            if reader2:
                assert not reader1.pcm_data and not reader2.pcm_data
            break
        num_packets += 1
    print("Finished.")
    print("Num audio packets:", num_packets)
    print("Reader1 num samples:", reader1.num_samples)
    if reader2:
        print("Reader2 num samples:", reader2.num_samples)


if __name__ == '__main__':
    main()
