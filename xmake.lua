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
    add_files("src/common/persister/persister.cpp")
    add_headerfiles("include/common/persister/persister.h")
    add_deps("logger")
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