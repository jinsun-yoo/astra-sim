#ifndef TIME_KEEPER_HH
#define TIME_KEEPER_HH

#include <chrono>

class Timekeeper {
public:
    Timekeeper() : _startTime(std::chrono::high_resolution_clock::now()) {}

    // Starts the timer
    void startTimer();

    // Returns the elapsed time in milliseconds
    long long elapsedTimeNanoseconds() const;

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> _startTime;
    // Flag to indicate if the timer has started.
    bool _is_started = false;
};

#endif // TIME_KEEPER_HH