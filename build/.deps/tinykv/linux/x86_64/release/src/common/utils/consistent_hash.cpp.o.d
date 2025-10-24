{
    depfiles_format = "gcc",
    depfiles = "consistent_hash.o: src/common/utils/consistent_hash.cpp\
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
        "src/common/utils/consistent_hash.cpp"
    }
}