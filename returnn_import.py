"""
This can be imported from `RETURNN <https://github.com/rwth-i6/returnn>`__.
"""

from glob import glob
import os
import sys
import numpy

my_dir = os.path.dirname(os.path.abspath(__file__))

if __name__ == '__main__':
    # https://stackoverflow.com/questions/54576879/
    __path__ = [my_dir]
    # Also expect that we are inside RETURNN/extern/ParseOggVorbis,
    # and make the RETURNN modules importable here.
    sys.path.append(my_dir + "/../..")

# This comes from RETURNN.
from Util import NativeCodeCompiler

from .demo_live_extract import ParseOggVorbisLib as _ParseOggVorbisLib


src_dir = "%s/src" % my_dir


def get_auto_compiled_lib_filename(verbose=False):
    """
    :param bool verbose: for compiling
    :return: path to lib file
    :rtype: str
    """
    # See e.g. test_NativeCodeCompiler for an example for NativeCodeCompiler.
    # See ./compile_lib_simple.py about how to compile.
    assert os.path.exists(src_dir)
    src_files = glob("%s/*.cpp" % src_dir)
    src_files.remove("%s/main.cpp" % src_dir)
    assert src_files

    src_code = ""
    for src_fn in src_files:
        f_code = open(src_fn).read()
        src_code += "\n// ------------ %s : BEGIN { ------------\n" % os.path.basename(src_fn)
        # https://gcc.gnu.org/onlinedocs/cpp/Line-Control.html#Line-Control
        src_code += "#line 1 \"%s\"\n" % os.path.basename(src_fn)
        src_code += f_code
        src_code += "\n// ------------ %s : END } --------------\n\n" % os.path.basename(src_fn)

    native = NativeCodeCompiler(
        base_name="ParseOggVorbis", code_version=1, code=src_code,
        include_paths=[src_dir], use_cxx11_abi=True,
        verbose=verbose)
    return native.get_lib_filename()


class ParseOggVorbisLib(_ParseOggVorbisLib):
    instance = None

    def __init__(self):
        super(ParseOggVorbisLib, self).__init__(lib_filename=get_auto_compiled_lib_filename())

    @classmethod
    def get_instance(cls):
        """
        It is save to work with one single global instance of this class.

        :rtype: ParseOggVorbisLib
        """
        if cls.instance is None:
            cls.instance = cls()
        return cls.instance

    def get_features_from_raw_bytes(self, raw_bytes, output_dim, kind="floor_final_ys", **kwargs):
        """
        :param bytes raw_bytes:
        :param int output_dim:
        :param str kind:
        :param kwargs: passed to underlying function
        :return: shape (time,output_dim). time does not correspond to the real time, although it correlates.
            It corresponds to the number of audio frames in the Vorbis stream.
        :rtype: numpy.ndarray
        """
        if kind == "floor_final_ys":
            data_filter = [
                "floor1_unpack multiplier", "floor1_unpack xs", "finish_setup",
                "floor_number", "floor1 final_ys", "finish_audio_packet"]
            reader = self.decode_ogg_vorbis(raw_bytes=raw_bytes, data_filter=data_filter)
            return reader.read_floor_ys(output_dim=output_dim, **kwargs)
        if kind == "floor_final_ys_rendered":
            data_filter = [
                "floor1_unpack multiplier", "floor1_unpack xs", "finish_setup",
                "floor_number", "floor1 floor", "finish_audio_packet"]
            reader = self.decode_ogg_vorbis(raw_bytes=raw_bytes, data_filter=data_filter)
            return reader.read_floor_ys(output_dim=output_dim, **kwargs)
        elif kind == "residue_ys":
            data_filter = [
                "floor1_unpack multiplier", "floor1_unpack xs", "finish_setup",
                "floor_number", "after_residue", "finish_audio_packet"]
            reader = self.decode_ogg_vorbis(raw_bytes=raw_bytes, data_filter=data_filter)
            return reader.read_residue_ys(output_dim=output_dim, **kwargs)
        else:
            raise Exception("%s.get_features_from_raw_bytes: invalid kind %r" % (self.__class__.__name__, kind))


def _demo():
    lib_fn = get_auto_compiled_lib_filename(verbose=True)
    print("Lib filename:", lib_fn)


if __name__ == '__main__':
    _demo()
