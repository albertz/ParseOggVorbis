#  ParseOggVorbis

This is a C++ implementation to parse/decode OGG Vorbis files.
The main impementation lives in [`src/ParseOggVorbis.hpp`](https://github.com/albertz/ParseOggVorbis/blob/master/src/ParseOggVorbis.hpp).
A small demo can be seen in [`src/main.cpp`](https://github.com/albertz/ParseOggVorbis/blob/master/src/main.cpp).
Exclude the file `src/main.cpp`, and the remaining files are enough to build the library.

To run the demo, and also compare it to the reference libvorbis implementation, do this:

    cd tests
    ./compile-libvorbis.py --mode standalone  # libvorbis with added debug hooks
    ./compile-libvorbis.py --mode ours
    ./compare-debug-out.py --ogg audio/test.stereo44khz.ogg

## Why

* I found the [reference C implementation by Xiph](https://github.com/xiph/vorbis/tree/master/lib) hard to read,
  so this implementation tries to provide clean and easy to read C++ code instead.

* The provided C++ API is simpler to use.

* There is a debugging C API to inspect intermediate states of the decoder.
  This was used for comparison with the reference C implementation,
  and is also useful in general if you want to get access to the intermediate representation / state.

