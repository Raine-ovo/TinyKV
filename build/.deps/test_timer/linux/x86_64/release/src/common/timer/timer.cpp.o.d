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
        "src/common/timer/timer.cpp"
    },
    depfiles = "timer.o: src/common/timer/timer.cpp include/common/timer/timer.h\
",
    depfiles_format = "gcc",
    depfiles_gcc = "timer.o: src/common/timer/timer.cpp include/common/timer/timer.h\
"
}