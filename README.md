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
