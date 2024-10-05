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

#include "module_dependencies.h"
#include "multi_value_history.h"

#include "event_log_prefix.h"

#include "gcc_warnings.h"
#ifdef __GNUC__
// The code is this file contains several casts to a type defined by a macro, which may result in useless casts.
#pragma GCC diagnostic ignored "-Wuseless-cast"
#endif

void MultiValueHistory::setup()
{
    for(int j = 0; j < MULTI_VALUE_HISTORY_NUMBER_OF_VALUES; ++j){
        history[j].setup();
        live[j].setup();
        history[j].clear();
        live[j].clear();
    }

    MULTI_VALUE_HISTORY_VALUE_TYPE val_min = std::numeric_limits<MULTI_VALUE_HISTORY_VALUE_TYPE>::lowest();

    for(int j = 0; j < MULTI_VALUE_HISTORY_NUMBER_OF_VALUES; ++j){
        for (size_t i = 0; i < history[i].size(); ++i) {
            // float f = 5000.0f * sinf(static_cast<float>(PI)/120.0f * static_cast<float>(i)) + 5000.0f;
            // switch(j){
            //     case 0:
            //         val_min = static_cast<int16_t>(f);
            //         break;

            //     case 1:
            //         val_min = static_cast<int16_t>(f * -1.0f + 10000.0f);
            //         break;

            //     default:
            //         val_min = static_cast<int16_t>((i) / 240 * 3000);
            // }
            // Use negative state to mark that these are pre-filled.
            history[j].push(val_min);
        }
    }

    chars_per_value = max(String(MULTI_VALUE_HISTORY_VALUE_MIN).length(), String(MULTI_VALUE_HISTORY_VALUE_MAX).length());
    // val_min values are replaced with null -> require at least 4 chars per value.
    chars_per_value = max(4U, chars_per_value);
    // For ',' between the values.
    ++chars_per_value;
}

void MultiValueHistory::register_urls(String base_url_)
{
    base_url = base_url_;

    server.on(("/" + base_url + "/history").c_str(), HTTP_GET, [this](WebServerRequest request) {
        const size_t buf_size = MULTI_VALUE_RING_BUF_SIZE * MULTI_VALUE_HISTORY_NUMBER_OF_VALUES * (chars_per_value + 3) + 100;
        std::unique_ptr<char[]> buf{new char[buf_size]};
        size_t buf_written = format_history(buf.get(), buf_size);
        
        return request.send(200, "application/json; charset=utf-8", buf.get(), static_cast<ssize_t>(buf_written));
    });

    server.on(("/" + base_url + "/live").c_str(), HTTP_GET, [this](WebServerRequest request) {
        const size_t buf_size = MULTI_VALUE_RING_BUF_SIZE * 3 * (chars_per_value + 3) + 100;
        std::unique_ptr<char[]> buf{new char[buf_size]};
        size_t buf_written = format_live(buf.get(), buf_size);

        return request.send(200, "application/json; charset=utf-8", buf.get(), static_cast<ssize_t>(buf_written));
    });


// !!! FIXME
    server.on(("/" + base_url + "/set_history_debug0").c_str(), HTTP_GET, [this](WebServerRequest request) {
        debug_level = 0;
        return request.send(200, "application/text; charset=utf-8", "Multi value history debug level set to 0", 40);
    });
    server.on(("/" + base_url + "/set_history_debug1").c_str(), HTTP_GET, [this](WebServerRequest request) {
        debug_level = 1;
        return request.send(200, "application/text; charset=utf-8", "Multi value history debug level set to 1", 40);
    });
    server.on(("/" + base_url + "/set_history_debug2").c_str(), HTTP_GET, [this](WebServerRequest request) {
        debug_level = 2;
        return request.send(200, "application/text; charset=utf-8", "Multi value history debug level set to 2", 40);
    });
    server.on(("/" + base_url + "/set_history_debug3").c_str(), HTTP_GET, [this](WebServerRequest request) {
        debug_level = 3;
        return request.send(200, "application/text; charset=utf-8", "Multi value history debug level set to 3", 40);
    });
    server.on(("/" + base_url + "/set_history_debug4").c_str(), HTTP_GET, [this](WebServerRequest request) {
        debug_level = 4;
        return request.send(200, "application/text; charset=utf-8", "Multi value history debug level set to 4", 40);
    });
    server.on(("/" + base_url + "/set_history_debug5").c_str(), HTTP_GET, [this](WebServerRequest request) {
        debug_level = 5;
        return request.send(200, "application/text; charset=utf-8", "Multi value history debug level set to 5", 40);
    });
    server.on(("/" + base_url + "/set_history_debug6").c_str(), HTTP_GET, [this](WebServerRequest request) {
        debug_level = 6;
        return request.send(200, "application/text; charset=utf-8", "Multi value history debug level set to 6", 40);
    });

// !!! FIXME

}

void MultiValueHistory::add_sample(float sample[MULTI_VALUE_HISTORY_NUMBER_OF_VALUES])
{
    MULTI_VALUE_HISTORY_VALUE_TYPE val_min = std::numeric_limits<MULTI_VALUE_HISTORY_VALUE_TYPE>::lowest();
    MULTI_VALUE_HISTORY_VALUE_TYPE val[MULTI_VALUE_HISTORY_NUMBER_OF_VALUES];

    for(int j = 0; j < MULTI_VALUE_HISTORY_NUMBER_OF_VALUES; ++j){
        val[j] = clamp(static_cast<MULTI_VALUE_HISTORY_VALUE_TYPE>(MULTI_VALUE_HISTORY_VALUE_MIN),
                                               static_cast<MULTI_VALUE_HISTORY_VALUE_TYPE>(roundf(sample[j])),
                                               static_cast<MULTI_VALUE_HISTORY_VALUE_TYPE>(MULTI_VALUE_HISTORY_VALUE_MAX));
        live[j].push(val[j]);
    }
    live_last_update = millis();
    end_this_interval = live_last_update;

    if (samples_this_interval == 0) {
        begin_this_interval = live_last_update;
    }
    ++samples_this_interval;

    for(int j = 0; j < MULTI_VALUE_HISTORY_NUMBER_OF_VALUES; ++j){
        sum_this_interval[j] += val[j];
    }

#if MODULE_WS_AVAILABLE()
    {
        const size_t buf_size = sizeof("{\"topic\":\"/live_samples\",\"payload\":{\"samples_per_second\":,\"samples\":[]}}\n") + sizeof(base_url) + (MULTI_VALUE_HISTORY_NUMBER_OF_VALUES * chars_per_value + 3) + 100; 
        size_t buf_written = 0;
        char *buf = static_cast<char *>(malloc(buf_size));
        buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "{\"topic\":\"%s/live_samples\",\"payload\":{\"samples_per_second\":%f,\"samples\":[[%d]", base_url.c_str(), static_cast<double>(samples_per_second()), static_cast<int>(val[0]));

        for(int j = 1; j < MULTI_VALUE_HISTORY_NUMBER_OF_VALUES; ++j){
            buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, ",[%d]", static_cast<int>(val[j]));
        }
        buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "%s", "]}}\n");

        if (buf_written > 0) {
            ws.web_sockets.sendToAllOwned(buf, static_cast<size_t>(buf_written));
        }
    }
#endif

    // start history task when first sample arrives. this adds the first sample to the
    // history immediately to avoid an empty history for the first history period
    if (live[0].used() == 1) {
        task_scheduler.scheduleWithFixedDelay([this, val_min](){
            MULTI_VALUE_HISTORY_VALUE_TYPE history_val[MULTI_VALUE_HISTORY_NUMBER_OF_VALUES];

            for(int j = 0; j < MULTI_VALUE_HISTORY_NUMBER_OF_VALUES; ++j){
                if (samples_this_interval == 0) {
                    history_val[j] = val_min; // TODO push 0 or intxy_t min here? intxy_t min will be translated into null when sending as json. However we document that there is only at most one block of null values at the start of the array indicating a reboot
                } else {
                    history_val[j] = static_cast<MULTI_VALUE_HISTORY_VALUE_TYPE>(sum_this_interval[j] / samples_this_interval);
                }
                history[j].push(history_val[j]);
            }

            history_last_update = millis();

            samples_last_interval = samples_this_interval;
            begin_last_interval = begin_this_interval;
            end_last_interval = end_this_interval;

            for(int j = 0; j < MULTI_VALUE_HISTORY_NUMBER_OF_VALUES; ++j){
                sum_this_interval[j] = 0;
            }
            samples_this_interval = 0;
            begin_this_interval = 0;
            end_this_interval = 0;

#if MODULE_WS_AVAILABLE()
            const size_t buf_size = sizeof("{\"topic\":\"/history_samples\",\"payload\":{\"samples\":[]}}\n") + sizeof(base_url) + (MULTI_VALUE_HISTORY_NUMBER_OF_VALUES * chars_per_value + 3) + 100; 

            size_t buf_written = 0;
            char *buf = static_cast<char *>(malloc(buf_size));
            buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "{\"topic\":\"%s/history_samples\",\"payload\":{\"samples\":[", base_url.c_str());

            if (history_val[0] == val_min) {
                buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "[%s]", "null");
            } else {
                buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "[%d]", static_cast<int>(history_val[0]));
            }

            for(int j = 1; j < MULTI_VALUE_HISTORY_NUMBER_OF_VALUES; ++j){
                if (history_val[j] == val_min) {
                    buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, ",[%s]", "null");
                } else {
                    buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, ",[%d]", static_cast<int>(history_val[j]));
                }
            }
            buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "%s", "]}}\n");

            if (buf_written > 0) {
                ws.web_sockets.sendToAllOwned(buf, static_cast<size_t>(buf_written));
            }
#endif
        }, 0, 1000 * 60 * MULTI_VALUE_HISTORY_MINUTE_INTERVAL);
    }
}

size_t MultiValueHistory::format_live(char *buf, size_t buf_size)
{
    size_t buf_written = 0;
    uint32_t offset = millis() - live_last_update;
    MULTI_VALUE_HISTORY_VALUE_TYPE val;

    buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "{\"offset\":%u,\"samples_per_second\":%f,\"samples\":[", offset, static_cast<double>(samples_per_second()));

    for (int i = 0; i < MULTI_VALUE_HISTORY_NUMBER_OF_VALUES; ++i){
        if (i != 0) buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "%s", ",");

        if (!live[i].peek(&val)){
            buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "%s", "[]");
        } else {
            buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "[%d", static_cast<int>(val));

            // This would underflow if live is empty, but it's guaranteed to have at least one entry by the peek() check above.
            size_t last_sample = live[i].used() - 1;
            for (size_t j = 1; j < last_sample && live[i].peek_offset(&val, j) && buf_written < buf_size; ++j) {
                buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, ",%d", static_cast<int>(val));
            }

            if (buf_written < buf_size) {
                buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "%s", "]");
            }
        }
    }

    buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "%s", "]}");

    return buf_written;
}

size_t MultiValueHistory::format_history(char *buf, size_t buf_size)
{
    MULTI_VALUE_HISTORY_VALUE_TYPE val_min = std::numeric_limits<MULTI_VALUE_HISTORY_VALUE_TYPE>::lowest();
    size_t buf_written = 0;
    uint32_t offset = millis() - history_last_update;
    MULTI_VALUE_HISTORY_VALUE_TYPE val;

    buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "{\"offset\":%u,\"samples\":[", offset);

    for (int i = 0; i < MULTI_VALUE_HISTORY_NUMBER_OF_VALUES; ++i){
        if (i != 0) buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "%s", ",");
        
        if (!history[i].peek(&val)){
            buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "%s", "[]");
        } else {
            // intxy_t min values are prefilled, because the ESP was booted less than 48 hours ago.
            if (val == val_min) {
                buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "[%s", "null");
            } else {
                buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "[%d", static_cast<int>(val));
            }

            for (size_t j = 1; j < history[i].used() && history[i].peek_offset(&val, j) && buf_written < buf_size; ++j) {
                // intxy_t min values are prefilled, because the ESP was booted less than 48 hours ago.
                if (val == val_min) {
                    buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "%s", ",null");
                } else {
                    buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, ",%d", static_cast<int>(val));
                }
            }

            if (buf_written < buf_size) {
                buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "%s", "]");
            }
        }
    }

    buf_written += snprintf_u(buf + buf_written, buf_size - buf_written, "%s", "]}");

    return buf_written;
}

float MultiValueHistory::samples_per_second()
{
    float samples_per_second = 0;

    // Only calculate samples_per_second based on the last interval
    // if we have seen at least 2 values. With the API meter module,
    // it can happen that we see exactly one value in the first interval.
    // In this case 0 samples_per_second is reported for the next
    // interval (i.e. four minutes).
    if (this->samples_last_interval > 1) {
        uint32_t duration = end_last_interval - begin_last_interval;

        if (duration > 0) {
            // (samples_last_interval - 1) because there are N samples but only (N - 1) gaps
            // between them covering (end_last_interval - begin_last_interval) milliseconds
            samples_per_second = static_cast<float>((this->samples_last_interval - 1) * 1000) / static_cast<float>(duration);
        }
    }
    // Checking only for > 0 in this branch is fine: If we have seen
    // 0 or 1 samples in the last interval and exactly 1 in this interval,
    // we can only report that samples_per_second is 0.
    // This fixes itself when the next sample arrives.
    else if (this->samples_this_interval > 0) {
        uint32_t duration = end_this_interval - begin_this_interval;

        if (duration > 0) {
            // (samples_this_interval - 1) because there are N samples but only (N - 1) gaps
            // between them covering (end_this_interval - begin_this_interval) milliseconds
            samples_per_second = static_cast<float>((this->samples_this_interval - 1) * 1000) / static_cast<float>(duration);
        }
    }

    return samples_per_second;
}
