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
#include "module_dependencies.h"

#include "tools.h"

bool DelayTimer::on_delay(bool signal, uint32_t delay_ms)
{
    if (!signal) start_time_on = millis();
    current_value_on_delay = min((uint32_t)(millis() - start_time_on), delay_ms);
    return (deadline_elapsed(start_time_on + delay_ms) && millis() > delay_ms);
}

bool DelayTimer::off_delay(bool signal, uint32_t delay_ms)
{
    if (signal) start_time_off = millis();
    current_value_off_delay = min((uint32_t)(millis() - start_time_off), delay_ms);
    return (!deadline_elapsed(start_time_off + delay_ms) && millis() > delay_ms);
}
