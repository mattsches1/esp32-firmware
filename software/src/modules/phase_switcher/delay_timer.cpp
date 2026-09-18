/* Delay timer helper for warp-charger
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
#include "delay_timer.h"

#include "generated/module_dependencies.h"
#include "tools.h"

extern EventLog logger;

bool DelayTimer::on_delay(bool signal, uint32_t delay_ms)
{
    const auto now = now_us();
    const millis_t delay = millis_t{delay_ms};
    if (!signal) start_time_on = now;
    const millis_t elapsed = min((now - start_time_on).to<millis_t>(), delay);
    current_value_on_delay = elapsed.as<uint32_t>();
    return elapsed >= delay;
}

bool DelayTimer::off_delay(bool signal, uint32_t delay_ms)
{
    const auto now = now_us();
    const millis_t delay = millis_t{delay_ms};
    if (signal) start_time_off = now;
    const millis_t elapsed = min((now - start_time_off).to<millis_t>(), delay);
    current_value_off_delay = elapsed.as<uint32_t>();
    return elapsed >= delay;
}