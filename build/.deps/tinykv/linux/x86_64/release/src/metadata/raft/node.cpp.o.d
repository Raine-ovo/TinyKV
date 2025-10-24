{
    depfiles_format = "gcc",
    depfiles = "build/.objs/tinykv/linux/x86_64/release/src/metadata/raft/node.cpp.o:  src/metadata/raft/node.cpp include/metadata/raft/node.h\
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
        "src/metadata/raft/node.cpp"
    }
}