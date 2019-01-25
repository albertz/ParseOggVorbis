file(GLOB SRC
    "*.h"
    "*.hpp"
    "*.c"
    "*.cpp"
)

add_executable(ParseOggVorbis ${SRC})
