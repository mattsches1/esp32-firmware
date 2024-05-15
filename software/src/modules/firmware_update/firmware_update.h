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

#include "config.h"

#include <stdint.h>

#include "module.h"
#include "web_server.h"

class FirmwareUpdate final : public IModule
{
public:
    FirmwareUpdate(){}
    void pre_setup() override;
    void setup() override;
    void register_urls() override;

    bool firmware_update_allowed = true;
    bool firmware_update_running = false;

private:
    bool handle_update_chunk(int command, WebServerRequest request, size_t chunk_index, uint8_t *data, size_t chunk_length, bool final, size_t complete_length);
    void reset_firmware_info();
    bool handle_firmware_info_chunk(size_t chunk_index, uint8_t *data, size_t chunk_length);
    String check_firmware_info(bool firmware_info_found, bool detect_downgrade, bool log);
    void check_for_updates();

    struct firmware_info_t {
        uint32_t magic[2] = {0};
        char firmware_name[61] = {0};
        uint8_t fw_version[3] = {0};
        uint32_t fw_build_time = {0};
        uint8_t fw_version_beta = {0};
    };

    ConfigRoot config;
    ConfigRoot available_updates;

    firmware_info_t info;
    uint32_t info_offset = 0;
    uint32_t calculated_checksum = 0;
    uint32_t checksum = 0;
    uint32_t checksum_offset = 0;
    bool update_aborted = false;
    bool info_found = false;
    String update_url;
    uint32_t update_cookie = 0;
};
