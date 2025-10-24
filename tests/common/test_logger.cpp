#include "common/logger/logger.h"

#include <iostream>
using namespace std;

int main ()
{
    LOG_INFO("用户{}登录成功, Ip: {}, Port: {}", 1001, "127.0.0.1", 1234);
    LOG_ERROR("用户{}登录成功, Ip: {}, Port: {}", 1001, "127.0.0.1", 1234);
    LOG_DEBUG("用户{}登录成功, Ip: {}, Port: {}", 1001, "127.0.0.1", 1234);
    LOG_FATAL("用户{}登录成功, Ip: {}, Port: {}", 1001, "127.0.0.1", 1234);
    return 0;
}