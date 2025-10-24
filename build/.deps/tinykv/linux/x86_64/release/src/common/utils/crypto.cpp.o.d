{
    depfiles_format = "gcc",
    depfiles = "crypto.o: src/common/utils/crypto.cpp\
",
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
    files = {
        "src/common/utils/crypto.cpp"
    }
}