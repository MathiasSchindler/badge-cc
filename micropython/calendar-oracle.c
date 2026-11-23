#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second, weekday;
    uint16_t yearday;
} calendar_t;

_Static_assert(sizeof(calendar_t) == 10 && offsetof(calendar_t, yearday) == 8,
               "RP2 timeutils record layout");

void timeutils_seconds_since_1970_to_struct_time(uint32_t, calendar_t *);
void gcc_calendar_from_seconds(uint32_t, calendar_t *);
uint32_t timeutils_mktime_1970(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
uint32_t gcc_calendar_to_seconds(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t);

int main(void) {
    uint32_t seed = 0x12345678;
    for (unsigned i = 0; i < 2048; ++i) {
        seed = seed * 1664525u + 1013904223u;
        uint32_t seconds = seed & 0x7fffffffu;
        calendar_t a = {0}, b = {0};
        timeutils_seconds_since_1970_to_struct_time(seconds, &a);
        gcc_calendar_from_seconds(seconds, &b);
        for (size_t byte = 0; byte < sizeof(a); ++byte)
            if (((const unsigned char *)&a)[byte] != ((const unsigned char *)&b)[byte])
                return 1;
        if (timeutils_mktime_1970(a.year, a.month, a.day, a.hour, a.minute, a.second) != seconds ||
            gcc_calendar_to_seconds(b.year, b.month, b.day, b.hour, b.minute, b.second) != seconds)
            return 2;
    }
    return 0;
}
