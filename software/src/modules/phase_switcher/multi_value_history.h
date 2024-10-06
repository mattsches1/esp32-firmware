/* esp32-firmware
 * Copyright (C) 2020-2021 Erik Fleckstein <erik@tinkerforge.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ringbuffer.h"
#include "malloc_tools.h"
#include "esp_heap_caps.h"

// How many hours to keep the coarse history for
#define MULTI_VALUE_HISTORY_HOURS 12
// How many minutes to keep the fine history for.
// This also controls the coarseness of the coarse history.
// For example 4 means that we accumulate 4 minutes of samples
// with the maximum rate i.e. ~ 3 samples per second (Querying the state
// takes about 380 ms).
// When we have 4 minutes worth of samples, we take the average
// and add it to the coarse history.
#define MULTI_VALUE_HISTORY_MINUTE_INTERVAL 1

#define MULTI_VALUE_RING_BUF_SIZE (MULTI_VALUE_HISTORY_HOURS * 60 / MULTI_VALUE_HISTORY_MINUTE_INTERVAL)

#ifndef MULTI_VALUE_HISTORY_VALUE_TYPE
#define MULTI_VALUE_HISTORY_VALUE_TYPE int16_t
#endif

#ifndef MULTI_VALUE_HISTORY_VALUE_MAX
#define MULTI_VALUE_HISTORY_VALUE_MAX 32767
#endif

#ifndef MULTI_VALUE_HISTORY_VALUE_MIN
#define MULTI_VALUE_HISTORY_VALUE_MIN -32767
#endif

#define MULTI_VALUE_HISTORY_NUMBER_OF_VALUES 3


// Check for < because ::lowest() is a reserved value.
static_assert(std::numeric_limits<MULTI_VALUE_HISTORY_VALUE_TYPE>::lowest() < MULTI_VALUE_HISTORY_VALUE_MIN);
static_assert(std::numeric_limits<MULTI_VALUE_HISTORY_VALUE_TYPE>::max() >= MULTI_VALUE_HISTORY_VALUE_MAX);

// We use int to format the buffer, so at most int is allowed.
static_assert(std::numeric_limits<int>::lowest() <= MULTI_VALUE_HISTORY_VALUE_MIN);
static_assert(std::numeric_limits<int>::max() >= MULTI_VALUE_HISTORY_VALUE_MAX);

class MultiValueHistory
{
public:
    MultiValueHistory()
    {
    }

    void setup();
    void register_urls(String base_url);
    void add_sample(float sample[MULTI_VALUE_HISTORY_NUMBER_OF_VALUES]);
    size_t format_live(char *buf, size_t buf_size);
    size_t format_history(char *buf, size_t buf_size);
    float samples_per_second();

    int64_t sum_this_interval[MULTI_VALUE_HISTORY_NUMBER_OF_VALUES] = {0};
    int samples_this_interval = 0;
    uint32_t begin_this_interval = 0;
    uint32_t end_this_interval = 0;

    int samples_last_interval = 0;
    uint32_t begin_last_interval = 0;
    uint32_t end_last_interval = 0;

    TF_PackedRingbuffer<MULTI_VALUE_HISTORY_VALUE_TYPE,
                  3 * 60 * MULTI_VALUE_HISTORY_MINUTE_INTERVAL,
                  uint32_t,
#if defined(BOARD_HAS_PSRAM)
                  malloc_psram,
#else
                  malloc_32bit_addressed,
#endif
                  heap_caps_free> live[MULTI_VALUE_HISTORY_NUMBER_OF_VALUES];
    uint32_t live_last_update = 0;

    TF_PackedRingbuffer<MULTI_VALUE_HISTORY_VALUE_TYPE,
                  MULTI_VALUE_RING_BUF_SIZE,
                  uint32_t,
#if defined(BOARD_HAS_PSRAM)
                  malloc_psram,
#else
                  malloc_32bit_addressed,
#endif
                  heap_caps_free> history[MULTI_VALUE_HISTORY_NUMBER_OF_VALUES];
    uint32_t history_last_update = 0;

    size_t chars_per_value = -1;

    String base_url = "default";

// !!! FIXME
    int debug_level = 1;

};
