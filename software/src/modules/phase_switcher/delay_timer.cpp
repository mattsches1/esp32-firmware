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

bool DelayTimer::on_delay(bool signal, seconds_t delay)
{
    const auto now = now_us();
    const micros_t delay_us = delay;
    if (!signal) deadline_on = now + delay_us;
    current_value_on_delay = (min(now + delay_us - deadline_on, delay_us)).to<seconds_t>().as<uint32_t>();

    return deadline_elapsed(deadline_on);
}

bool DelayTimer::off_delay(bool signal, seconds_t delay)
{
    const auto now = now_us();
    const micros_t delay_us = delay;
    if (signal) deadline_off = now + delay_us;
    current_value_off_delay = (min(now + delay_us - deadline_off, delay_us)).to<seconds_t>().as<uint32_t>();

    return !deadline_elapsed(deadline_off);
}