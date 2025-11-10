#include "common/storage/rocksdb_client.h"

#include "gtest/gtest.h"
#include <filesystem>
#include <string>
#include <vector>

#include <filesystem>
namespace fs = std::filesystem;

class RocksDBClientTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (fs::exists(test_dir))
        {
            fs::remove_all(test_dir);
        }
        fs::create_directories(test_dir);
    }

    void TearDown() override
    {
        if (fs::exists(test_dir))
        {
            fs::remove_all(test_dir);
        }
    }

    // 初始化类的静态常量
    static const inline fs::path test_dir = "test_data";
    static const inline fs::path db_file = test_dir / "test_db";
};

TEST_F(RocksDBClientTest, BasicOperations)
{
    RocksDBClient rocksdb_cli(db_file);
    
    std::string key = "key_0", value = "value_0";
    EXPECT_TRUE(rocksdb_cli.Put(key, value));
    std::string check_value;
    EXPECT_TRUE(rocksdb_cli.Get(key, check_value));
    EXPECT_EQ(check_value, value);

    EXPECT_TRUE(rocksdb_cli.Delete(key));
    EXPECT_FALSE(rocksdb_cli.Get(key, check_value));
    EXPECT_FALSE(rocksdb_cli.Exists(key));

    std::vector<std::pair<std::string, std::string>> kvs;
    for (int i = 1; i <= 100; i ++ )
    {
        kvs.push_back({"key_" + std::to_string(i), "value_" + std::to_string(i)});
    }
    rocksdb_cli.PutBatch(kvs);
    for (int i = 1; i <= 100; i ++ )
    {
        std::string key = "key_" + std::to_string(i);
        EXPECT_TRUE(rocksdb_cli.Exists(key));
    }
}

int main ()
{
    ::testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}