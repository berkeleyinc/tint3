#ifndef TINT3_CLOCK_TIME_UTILS_H
#define TINT3_CLOCK_TIME_UTILS_H

#include <ctime>
#include <string>

struct tm* ClockGetTimeForTimezone(std::string const& timezone,
                                   time_t const* time_ptr);
std::string FormatTime(std::string format, struct tm const* timeptr);

#endif  // TINT3_CLOCK_TIME_UTILS_H
