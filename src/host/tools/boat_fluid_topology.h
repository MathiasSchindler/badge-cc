#ifndef UNIVERSE26_HOST_BOAT_FLUID_TOPOLOGY_H
#define UNIVERSE26_HOST_BOAT_FLUID_TOPOLOGY_H

#include "../../shared/boat_fluid.h"

/* Attached water above a >=6 px air gap and bed-connected water below.
   Ignore disconnected spray and sub-cell cracks; not a curvature metric. */
static uint32_t boat_fluid_overhangs(const boat_fluid_t *water) {
    uint8_t connected[BOAT_FLUID_FIELD] = {0};
    uint16_t queue[BOAT_FLUID_FIELD];
    uint32_t count = 0u, read = 0u;
    for (uint32_t x = 0u; x < 160u; ++x) {
        uint32_t at = x * 120u + 112u;
        if (water->field[at] >= 10u) {
            connected[at] = 1u; queue[count++] = (uint16_t)at;
        }
    }
    while (read < count) {
        uint32_t at = queue[read++], x = at / 120u, y = at % 120u;
        const int32_t offset[4] = {-120, 120, -1, 1};
        for (uint32_t direction = 0u; direction < 4u; ++direction) {
            if ((direction == 0u && x == 0u) || (direction == 1u && x == 159u) ||
                (direction == 2u && y == 0u) || (direction == 3u && y == 113u)) continue;
            uint32_t next = (uint32_t)((int32_t)at + offset[direction]);
            if (!connected[next] && water->field[next] >= 10u) {
                connected[next] = 1u; queue[count++] = (uint16_t)next;
            }
        }
    }
    uint32_t overhangs = 0u;
    for (uint32_t x = 1u; x < 159u; ++x) {
        uint32_t above = 0u, air = 0u;
        for (uint32_t y = 16u; y < 112u; ++y) {
            uint32_t at = x * 120u + y;
            if (water->field[at] < 10u) {
                if (above >= 2u) ++air;
                else above = 0u;
            } else if (connected[at]) {
                if (above >= 2u && air >= 3u) { ++overhangs; break; }
                if (air != 0u) above = 0u;
                ++above; air = 0u;
            } else above = air = 0u;
        }
    }
    return overhangs;
}

#endif
