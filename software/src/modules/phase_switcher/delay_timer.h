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

#pragma once

#include "config.h"

class DelayTimer{

public:
    bool on_delay(bool signal, seconds_t delay);
    bool off_delay(bool signal, seconds_t delay);

    uint32_t current_value_on_delay, current_value_off_delay;

private:
    micros_t deadline_on = 0_us;
    micros_t deadline_off = 0_us;
};
                                    
