#include "common/timer/timer.h"

#include <iostream>
#include <chrono>

int main ()
{
    auto now = std::chrono::steady_clock::now();
    Timer timer;
    timer.start(3000, true, [] { std::cout << "hello" << std::endl; }, Timer::Mode::Normal);
    auto after = std::chrono::steady_clock::now();

    return 0;
}