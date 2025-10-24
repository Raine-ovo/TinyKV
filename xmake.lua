-- 添加调试模式和发布模式
-- 在构建时可以通过 xmake -m debug 或 xmake -m release 构建不同版本
add_rules("mode.debug", "mode.release")

-- 指定 c++20 标准
set_languages("c++20")

-- 安装依赖库
-- add_requires("protobuf", "gtest 1.14.0");
add_requires("protobuf")

-- 加入 include
add_includedirs("include")

for _, dir in ipairs(os.dirs("src/*")) do
    add_includedirs(dir)
end

-- target("tinykv")
--     set_kind("static")
--     add_files("src/**.cpp")
--     add_headerfiles("include/(**/*.h)")

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