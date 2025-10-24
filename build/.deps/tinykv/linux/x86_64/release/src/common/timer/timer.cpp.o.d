{
    depfiles = "timer.o: src/common/timer/timer.cpp include/common/timer/timer.h\
",
    files = {
        "src/common/timer/timer.cpp"
    },
    values = {
        "/usr/bin/g++",
        {
            "-m64",
            "-fvisibility=hidden",
            "-fvisibility-inlines-hidden",
            "-O3",
            "-std=c++20",
            "-Iinclude",
            "-Isrc/metadata",
            "-Isrc/discovery",
            "-Isrc/proto",
            "-Isrc/common",
            "-Isrc/client",
            "-Isrc/storage",
            "-DNDEBUG"
        }
    },
    depfiles_format = "gcc"
}