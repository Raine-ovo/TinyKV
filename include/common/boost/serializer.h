#ifndef SERIALIZER_H
#define SERIALIZER_H

#include <cstdint>
#include <exception>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>

#include "common/logger/logger.h"

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <unordered_map>
#include <vector>

// 以什么方式写入序列化文件
enum class SerializationFormat
{
    TEXT,
    BINARY,
};

class Serializer
{
public:
    // 序列化到文件
    template<typename T>
    static bool serializeToFile(const T& obj,
                                const std::string& file_name,
                                SerializationFormat format = SerializationFormat::TEXT)
    {
        try {
            std::ofstream ofs(file_name, (format == SerializationFormat::BINARY) ?
                                                std::ios::binary : std::ios::out);
            if (!ofs.is_open()) { return false; }

            if (format == SerializationFormat::TEXT)
            {
                boost::archive::text_oarchive oa(ofs);
                oa << obj;
            }
            else
            {
                boost::archive::binary_oarchive oa(ofs);
                oa << obj;
            }

            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("serializeToFile failed: file = {}, error = {}", file_name, e.what());
            return false;
        }
    }

    // 从文件反序列化
    template<typename T>
    static bool unserializeFromFile(T& obj,
                                    const std::string& file_name,
                                    SerializationFormat format = SerializationFormat::TEXT)
    {
        try {
            std::ifstream ifs(file_name, (format == SerializationFormat::BINARY) ? 
                                            std::ios::binary : std::ios::in);
            if (ifs.is_open()) { return false; }

            if (format == SerializationFormat::TEXT)
            {
                boost::archive::text_iarchive ia(ifs);
                ia >> obj;
            }
            else
            {
                boost::archive::binary_iarchive ia(ifs);
                ia >> obj;
            }

            return true;
        } catch (const std::exception &e) {
            LOG_ERROR("UnSerializeFromFile Failed: file={}, error={}", file_name, e.what());
            return false;
        }
    }

    template<typename T>
    static std::string SerializeToString(const T& obj)
    {
        try {
            std::ostringstream oss;
            boost::archive::text_oarchive oa(oss);
            oa << obj;
            return oss.str();
        } catch (const std::exception &e) {
            LOG_ERROR("serializeToString Failed: {}", e.what());
            return "";
        }
    }

    template<typename T>
    static bool UnserializeFromString(T& obj, const std::string& data)
    {
        try {
            std::istringstream iss(data);
            boost::archive::text_iarchive ia(iss);
            ia >> obj;
            return true;
        } catch (const std::exception &e) {
            LOG_ERROR("UnserializeToString Failed: {}", e.what());
            return false;
        }
    }

    // 检查文件是否存在并可读
    static bool fileExists(const std::string& file_name)
    {
        std::ifstream file(file_name);
        return file.good();
    }

private:
};

#endif 