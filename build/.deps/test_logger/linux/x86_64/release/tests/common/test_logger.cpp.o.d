{
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
    files = {
        "tests/common/test_logger.cpp"
    },
    depfiles = "test_logger.o: tests/common/test_logger.cpp  include/common/logger/logger.h include/common/lockqueue/lockqueue.h\
",
    depfiles_format = "gcc",
    depfiles_gcc = "test_logger.o: tests/common/test_logger.cpp  include/common/logger/logger.h include/common/lockqueue/lockqueue.h\
"
}