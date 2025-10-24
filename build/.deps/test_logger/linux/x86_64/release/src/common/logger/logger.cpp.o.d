{
    depfiles_format = "gcc",
    depfiles_gcc = "logger.o: src/common/logger/logger.cpp include/common/logger/logger.h  include/common/lockqueue/lockqueue.h\
",
    files = {
        "src/common/logger/logger.cpp"
    },
    values = {
        "/usr/bin/gcc",
        {
            "-m64",
            "-fvisibility=hidden",
            "-fvisibility-inlines-hidden",
            "-O3",
            "-std=c++20",
            "-Iinclude",
            "-Isrc/client",
            "-Isrc/common",
            "-Isrc/proto",
            "-Isrc/discovery",
            "-Isrc/metadata",
            "-Isrc/storage",
            "-DNDEBUG"
        }
    },
    depfiles = "logger.o: src/common/logger/logger.cpp include/common/logger/logger.h  include/common/lockqueue/lockqueue.h\
"
}