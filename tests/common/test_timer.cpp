#include "common/timer/timer.h"

#include <iostream>
#include <chrono>

int main ()
{
    auto now = std::chrono::steady_clock::now();
    Timer timer(1000, []() { std::cout << "after 2s ..." << std::endl; });
    timer.run();
    auto after = std::chrono::steady_clock::now();

    return 0;
}