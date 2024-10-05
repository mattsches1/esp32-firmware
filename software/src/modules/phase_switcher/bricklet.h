/* bricklet handler for warp-charger phase_switcher module
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

#include "config.h"

#include "bindings/base58.h"
#include "bindings/hal_common.h"
#include "bindings/errors.h"
#include "header_logger.h"

extern TF_HAL hal;

#define BOOTLOADER_MODE_FIRMWARE 1
#define FIRMWARE_DEVICE_IDENTIFIER_OFFSET 8

template <class DeviceT,
          int (*init_function)(DeviceT *, const char*, TF_HAL *)>
class Bricklet {
public:
    Bricklet(const uint16_t device_id,
             const char *device_name, 
             const char *module_name) :
        device_id(device_id),
        device_name(device_name),
        module_name(module_name)
        {}     

    bool setup_device() {
        TF_TFP *tfp = tf_hal_get_tfp(&hal, nullptr, nullptr, &device_id, false);

        if (tfp == nullptr) {
            header_printfln("No %s Bricklet found. Disabling %s support.", device_name, module_name);
            return false;
        }

        device_found = true;

        char uid[7] = {0};

        tf_base58_encode(tfp->uid_num, uid);

        int result = init_function(&device, uid, &hal);

        if(result != TF_E_OK) {
            header_printfln("Failed to initialize %s bricklet (%d). Disabling %s support.", device_name, result, module_name);
            return false;
        }

        return true;
    }

    bool device_found = false;

    DeviceT device;

    uint16_t device_id;
    const char *device_name;
    const char *module_name;

};
