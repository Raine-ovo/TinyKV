#include "common/persister/persister.h"

#include "gtest/gtest.h"
#include <cstdint>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

class PersisterTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 每个测试前清理测试目录
        if (fs::exists(test_dir))
        {
            fs::remove_all(test_dir);
        }
        fs::create_directories(test_dir);
    }

    void TearDown() override
    {
        // 每个测试后清理测试目录
        if (fs::exists(test_dir))
        {
            fs::remove_all(test_dir);
        }
    }

    static const inline fs::path test_dir = "test_data";
    static const inline fs::path state_file = test_dir / "state";
    static const inline fs::path snapshot_file = test_dir / "snapshot";
};

// 基本的读写 state
TEST_F(PersisterTest, BasicSaveAndLoadState)
{
    Persister persister(state_file.string(), snapshot_file.string());

    uint64_t current_term = 5;
    uint32_t voted_for = 2;
    std::vector<uint8_t> log_data = {0x1, 0x2, 0x3, 0x4};

    // 保存，这里使用非致命断言
    EXPECT_TRUE(persister.save_state(current_term, voted_for, log_data));

    // 加载数据
    auto result = persister.load_state();
    ASSERT_TRUE(result.has_value()); // 这里是致命断言

    auto [loaded_term, loaded_voted_for, loaded_log_data] = result.value();

    EXPECT_EQ(loaded_term, current_term);
    EXPECT_EQ(loaded_voted_for, voted_for);
    EXPECT_EQ(loaded_log_data, log_data);
}

// 文件不存在
TEST_F(PersisterTest, LoadStateWhenFileNotExists)
{
    Persister persister(state_file.string(), snapshot_file.string());
    auto result = persister.load_state();
    EXPECT_FALSE(result.has_value());
}

// 日志数据为空
TEST_F(PersisterTest, SaveAndLoadEmptyLog)
{
    Persister persister(state_file.string(), snapshot_file.string());

    uint64_t current_term = 1;
    uint32_t voted_for = 0;
    std::vector<uint8_t> empty_log;

    EXPECT_TRUE(persister.save_state(current_term, voted_for, empty_log));

    auto result = persister.load_state();
    ASSERT_TRUE(result.has_value());

    auto [loaded_term, loaded_voted_for, loaded_log_data] = result.value();

    EXPECT_EQ(loaded_term, current_term);
    EXPECT_EQ(loaded_voted_for, voted_for);
    EXPECT_TRUE(loaded_log_data.empty());
}

// 快照
TEST_F(PersisterTest, SaveAndLoadSnapshot)
{
    Persister persister(state_file.string(), snapshot_file.string());

    std::vector<uint8_t> snapshot_data = {0x10, 0x20, 0x30, 0x40, 0x50};

    EXPECT_TRUE(persister.save_snapshot(snapshot_data));

    auto result = persister.load_snapshot();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), snapshot_data);
}

// 多次进行保存-加载，看会不会出错
TEST_F(PersisterTest, MultipleSaveAndLoad)
{
    Persister persister(state_file.string(), snapshot_file.string());

    EXPECT_TRUE(persister.save_state(1, 0, {0x01, 0x02}));
    auto res1 = persister.load_state();
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(std::get<0>(res1.value()), 1);

    EXPECT_TRUE(persister.save_state(2, 1, {0x03, 0x04}));
    auto res2 = persister.load_state();
    ASSERT_TRUE(res2.has_value());
    EXPECT_EQ(std::get<0>(res2.value()), 2);
    EXPECT_EQ(std::get<1>(res2.value()), 1);
}

// 测试并发性
TEST_F(PersisterTest, ConcurrentAccess) {
    Persister persister(state_file.string(), snapshot_file.string());
    
    std::vector<std::thread> threads;
    const int num_threads = 10;
    std::atomic<bool> has_data{false};
    
    // 先保存一个初始状态
    persister.save_state(1, 0, {0x01});
    has_data = true;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&persister, i, &has_data]() {
            try {
                if (i % 3 == 0) {
                    // 写入线程
                    persister.save_state(i + 10, i, {static_cast<uint8_t>(i)});
                } else if (i % 3 == 1) {
                    // 读取状态线程
                    auto result = persister.load_state();
                    // 不检查结果，主要测试不崩溃
                } else {
                    // 快照操作线程
                    if (has_data) {
                        auto result = persister.load_snapshot();
                        // 可能为空，正常
                    }
                }
            } catch (const std::exception& e) {
                // 捕获异常，测试失败
                FAIL() << "Thread " << i << " threw exception: " << e.what();
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // 最终应该能正常加载状态
    auto result = persister.load_state();
    // 不强制要求有值，只要不崩溃就行
    SUCCEED();
}

int main ()
{
    // 运行所有测试点
    ::testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}