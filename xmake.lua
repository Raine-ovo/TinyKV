-- 添加调试模式和发布模式
-- 在构建时可以通过 xmake -m debug 或 xmake -m release 构建不同版本
add_rules("mode.debug", "mode.release")
-- 指定 c++20 标准
set_languages("c++20")
-- 指定中间文件生成路径
set_objectdir("build")
-- 指定目标文件的生成路径
set_targetdir("bin", {kind = "binary"})
set_targetdir("lib", {kind = "static"})
set_targetdir("lib", {kind = "shared"})


-- 安装依赖库
add_requires("protobuf")
add_requires("toml11")
add_requires("muduo")
add_requires("gtest")
add_requires("rocksdb v10.5.1", {
    configs = {
        cxxflags = "-std=c++20"  -- 为 RocksDB 编译添加 C++20 标志
    }
})

-- 加入 include
add_includedirs("include")

-- 全局依赖包
add_packages("toml11")

-- target("tinykv")
--     set_kind("static")
--     add_files("src/**.cpp")
--     add_headerfiles("include/(**/*.h)")

target("logger")
    set_kind("static")
    add_files("src/common/logger/logger.cpp")
    add_headerfiles("include/common/logger/logger.h")
    set_targetdir("lib")

target("config")
    set_kind("static")
    add_files("src/common/config/config.cpp")
    add_headerfiles("include/common/config/config.h")
    add_deps("logger")
    add_packages("toml11")
    set_targetdir("lib")

target("zk_client")
    set_kind("static")
    add_files("src/discovery/zookeeper/zk_client.cpp")
    add_headerfiles("include/discovery/zookeeper/zk_client.h")
    add_deps("logger", "config")
    add_packages("toml11")
    add_links("zookeeper_mt") -- 多线程版本的 zookeeper
    set_targetdir("lib")

target("rpc")
    set_kind("static")
    add_files("src/common/rpc/*.cpp", "src/proto/rpc_header.pb.cc")
    add_headerfiles("include/common/rpc/*.h", "include/proto/rpc_header.pb.h")
    add_deps("logger", "zk_client")
    add_packages("protobuf", "muduo")
    set_targetdir("lib")

target("persister")
    set_kind("static")
    add_files("src/common/persister/persister.cpp", "src/proto/persister.pb.cc")
    add_headerfiles("include/common/persister/persister.h")
    add_deps("logger")
    set_targetdir("lib")

target("timer")
    set_kind("static")
    add_files("src/common/timer/timer.cpp")
    add_headerfiles("include/common/timer/timer.h")
    add_deps("logger")
    set_targetdir("lib")

target("rocksdb")
    set_kind("static")
    add_files("src/common/storage/rocksdb_client.cpp", "src/proto/command.pb.cc")
    add_headerfiles("include/common/storage/rocksdb_client.h", "include/proto/command.pb.h")
    add_deps("logger")
    add_packages("rocksdb", "protobuf")
    add_links("rocksdb", "protobuf")
    set_targetdir("lib")

target("shard")
    set_kind("static")
    add_files("src/common/shard/shard_manager.cpp")
    add_headerfiles("include/common/shard/shard_manager.h", "include/common/shard/shard_group.h")
    set_targetdir("lib")

target("kvservice")
    set_kind("static")
    add_files("src/kvservice/*.cpp", "src/proto/command.pb.cc")
    add_headerfiles("include/kvservice/*.h", "include/proto/command.pb.h")
    add_deps("rocksdb", "rpc", "raft", "shard")
    add_packages("protobuf")
    set_targetdir("lib")

target("raft")
    set_kind("static")
    add_files("src/storage/raft/*.cpp", "src/proto/raft.pb.cc", "src/proto/command.pb.cc")
    add_headerfiles("include/storage/raft/*.h")
    add_deps("rpc", "persister", "logger", "timer")
    add_packages("protobuf")
    set_targetdir("lib")

target("test_timer")
    set_kind("binary")
    add_files("tests/common/test_timer.cpp", "src/common/timer/timer.cpp")
    -- 把生成的可执行文件放到 bin 目录下
    set_targetdir("bin")

target("test_logger")
    set_kind("binary")
    add_files("tests/common/test_logger.cpp", "src/common/logger/logger.cpp")
    -- 把生成的可执行文件放到 bin 目录下
    set_targetdir("bin")

target("test_zk_client")
    set_kind("binary")
    add_files("tests/discovery/test_zk_client.cpp")
    add_deps("zk_client", "logger", "config")
    set_targetdir("bin")

target("test_rpc_consumer")
    set_kind("binary")
    add_files("tests/common/rpc/consumer.cpp", "tests/common/rpc/rpc.pb.cc")
    add_deps("rpc")
    set_targetdir("bin")

target("test_rpc_provider")
    set_kind("binary")
    add_files("tests/common/rpc/provider.cpp", "tests/common/rpc/rpc.pb.cc")
    add_deps("rpc")
    set_targetdir("bin")

target("test_persister")
    set_kind("binary")
    add_files("tests/common/test_persister.cpp")
    add_deps("persister", "logger")
    set_targetdir("bin")
    add_packages("gtest")

target("test_raft")
    set_kind("binary")
    add_files("tests/metadata/raft/test_raft.cpp")
    add_deps("raft", "rpc", "persister", "logger")
    set_targetdir("bin")
    add_packages("gtest")

target("test_rocksdb")
    set_kind("binary")
    -- ? 不知道为什么一直报错，说 rocksb 头文件声明的函数没实现，这里先把 rocksdb_client.cpp 也加进来
    add_files("tests/common/test_rocksdb.cpp", "src/common/storage/rocksdb_client.cpp")
    add_files("src/proto/command.pb.cc")
    add_headerfiles("include/proto/command.pb.h")
    add_deps("logger", "rocksdb")
    add_packages("gtest", "rocksdb")
    add_links("protobuf")
    set_targetdir("bin")