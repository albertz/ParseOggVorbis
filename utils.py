
from subprocess import check_call
import tempfile
import shutil
from glob import glob
import os


def call(args):
    print("$ %s" % " ".join(args))
    check_call(args)


def c_compile(src_files, common_opts, out_filename, link_opts=()):
    """
    :param list[str] src_files:
    :param list[str] common_opts:
    :param list[str] link_opts:
    :param str out_filename:
    """
    tmp_dir = tempfile.mkdtemp()
    try:
        used_cpp = False
        for fn in src_files:
            is_c = fn.endswith(".c")
            if not is_c:
                assert fn.endswith(".cpp")
                used_cpp = True
            call(
                ["cc" if is_c else "c++", "-c", "-std=c99" if is_c else "-std=c++11"] +
                common_opts +
                [fn, "-o", "%s/%s.o" % (tmp_dir, os.path.basename(fn))])
        o_files = glob("%s/*.o" % tmp_dir)
        call(["c++" if used_cpp else "cc"] + list(link_opts) + o_files + ["-o", out_filename])
    finally:
        shutil.rmtree(tmp_dir)


def install_better_exchook():
    try:
        import better_exchook
    except ImportError:
        return  # not so critical...
    better_exchook.install()
