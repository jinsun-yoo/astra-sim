#ifndef TIME_KEEPER_HH
#define TIME_KEEPER_HH

#include <chrono>

class Timekeeper {
public:
    Timekeeper() : start(std::chrono::high_resolution_clock::now()) {}

    // Starts the timer
    void startTime();

    // Returns the elapsed time in milliseconds
    long long elapsedTimeNanoseconds() const;

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
};

#endif // TIME_KEEPER_HH