/* esp32-firmware
 * Copyright (C) 2023 Thomas Hein
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

#define EVENT_LOG_PREFIX "meters_sma_swire"

#include <WiFiUdp.h>

#include "meter_sma_speedwire.h"
#include "module_dependencies.h"

#include "event_log.h"
#include "task_scheduler.h"
#include "tools.h"

#include "gcc_warnings.h"

WiFiUDP   udp;
IPAddress mc_groupIP(239, 12, 255, 254);
uint16_t  mc_Port = 9522;

#define SMA_PACKET_LEN 608
#define SMA_SPEEDWIRE_VALUE_COUNT 59

MeterClassID MeterSMASpeedwire::get_class() const
{
    return MeterClassID::SMASpeedwire;
}

void MeterSMASpeedwire::setup(const Config &ephemeral_config)
{
    // Wirkleistung Bezug (aktueller Mittelwert)
    _values[MeterValueID::PowerActiveLSumImport]        = obis(0,  1, 4, 0,      10.0, 4);  // Summe
    _values[MeterValueID::PowerActiveL1Import]          = obis(0, 21, 4, 0,      10.0, 4);  // L1
    _values[MeterValueID::PowerActiveL2Import]          = obis(0, 41, 4, 0,      10.0, 4);  // L2
    _values[MeterValueID::PowerActiveL3Import]          = obis(0, 61, 4, 0,      10.0, 4);  // L3

    // Wirkleistung Bezug (Zählerstand)
    _values[MeterValueID::EnergyActiveLSumImport]       = obis(0,  1, 8, 0, 3600000.0, 8);  // Summe
    _values[MeterValueID::EnergyActiveL1Import]         = obis(0, 21, 8, 0, 3600000.0, 8);  // L1
    _values[MeterValueID::EnergyActiveL2Import]         = obis(0, 41, 8, 0, 3600000.0, 8);  // L2
    _values[MeterValueID::EnergyActiveL3Import]         = obis(0, 61, 8, 0, 3600000.0, 8);  // L3

    // Blindleistung Bezug (aktueller Mittelwert)
    _values[MeterValueID::PowerReactiveLSumInductive]   = obis(0,  3, 4, 0,      10.0, 4);  // Summe
    _values[MeterValueID::PowerReactiveL1Inductive]     = obis(0, 23, 4, 0,      10.0, 4);  // L1
    _values[MeterValueID::PowerReactiveL2Inductive]     = obis(0, 43, 4, 0,      10.0, 4);  // L2
    _values[MeterValueID::PowerReactiveL3Inductive]     = obis(0, 63, 4, 0,      10.0, 4);  // L3

    // Blindleistung Bezug (Zählerstand)
    _values[MeterValueID::EnergyReactiveLSumInductive]  = obis(0,  3, 8, 0, 3600000.0, 8);  // Summe
    _values[MeterValueID::EnergyReactiveL1Inductive]    = obis(0, 23, 8, 0, 3600000.0, 8);  // L1
    _values[MeterValueID::EnergyReactiveL2Inductive]    = obis(0, 43, 8, 0, 3600000.0, 8);  // L2
    _values[MeterValueID::EnergyReactiveL3Inductive]    = obis(0, 63, 8, 0, 3600000.0, 8);  // L3

    // Scheinleistung Bezug (aktueller Mittelwert)
    _values[MeterValueID::PowerApparentLSumImport]      = obis(0,  9, 4, 0,      10.0, 4);  // Summe
    _values[MeterValueID::PowerApparentL1Import]        = obis(0, 29, 4, 0,      10.0, 4);  // L1
    _values[MeterValueID::PowerApparentL2Import]        = obis(0, 49, 4, 0,      10.0, 4);  // L2
    _values[MeterValueID::PowerApparentL3Import]        = obis(0, 69, 4, 0,      10.0, 4);  // L3

    // Scheinleistung Bezug (Zählerstand)
    _values[MeterValueID::EnergyApparentLSumImport]     = obis(0,  9, 8, 0, 3600000.0, 8);  // Summe
    _values[MeterValueID::EnergyApparentL1Import]       = obis(0, 29, 8, 0, 3600000.0, 8);  // L1
    _values[MeterValueID::EnergyApparentL2Import]       = obis(0, 49, 8, 0, 3600000.0, 8);  // L2
    _values[MeterValueID::EnergyApparentL3Import]       = obis(0, 69, 8, 0, 3600000.0, 8);  // L3

    // Wirkleistung Einspeisung (aktueller Mittelwert)
    _values[MeterValueID::PowerActiveLSumExport]        = obis(0,  2, 4, 0,      10.0, 4);  // Summe
    _values[MeterValueID::PowerActiveL1Export]          = obis(0, 22, 4, 0,      10.0, 4);  // L1
    _values[MeterValueID::PowerActiveL2Export]          = obis(0, 42, 4, 0,      10.0, 4);  // L2
    _values[MeterValueID::PowerActiveL3Export]          = obis(0, 62, 4, 0,      10.0, 4);  // L3

    // Wirkleistung Einspeisung (Zählerstand)
    _values[MeterValueID::EnergyActiveLSumExport]       = obis(0,  2, 8, 0, 3600000.0, 8);  // Summe
    _values[MeterValueID::EnergyActiveL1Export]         = obis(0, 22, 8, 0, 3600000.0, 8);  // L1
    _values[MeterValueID::EnergyActiveL2Export]         = obis(0, 42, 8, 0, 3600000.0, 8);  // L2
    _values[MeterValueID::EnergyActiveL3Export]         = obis(0, 62, 8, 0, 3600000.0, 8);  // L3

    // Blindleistung Einspeisung (aktueller Mittelwert)
    _values[MeterValueID::PowerReactiveLSumCapacitive]  = obis(0,  4, 4, 0,      10.0, 4);  // Summe
    _values[MeterValueID::PowerReactiveL1Capacitive]    = obis(0, 24, 4, 0,      10.0, 4);  // L1
    _values[MeterValueID::PowerReactiveL2Capacitive]    = obis(0, 44, 4, 0,      10.0, 4);  // L2
    _values[MeterValueID::PowerReactiveL3Capacitive]    = obis(0, 64, 4, 0,      10.0, 4);  // L3

    // Blindleistung Einspeisung (Zählerstand)
    _values[MeterValueID::EnergyReactiveLSumCapacitive] = obis(0,  4, 8, 0, 3600000.0, 8);  // Summe
    _values[MeterValueID::EnergyReactiveL1Capacitive]   = obis(0, 24, 8, 0, 3600000.0, 8);  // L1
    _values[MeterValueID::EnergyReactiveL2Capacitive]   = obis(0, 44, 8, 0, 3600000.0, 8);  // L2
    _values[MeterValueID::EnergyReactiveL3Capacitive]   = obis(0, 64, 8, 0, 3600000.0, 8);  // L3

    // Scheinleistung Einspeisung (aktueller Mittelwert)
    _values[MeterValueID::PowerApparentLSumExport]      = obis(0, 10, 4, 0,      10.0, 4);  // Summe
    _values[MeterValueID::PowerApparentL1Export]        = obis(0, 30, 4, 0,      10.0, 4);  // L1
    _values[MeterValueID::PowerApparentL2Export]        = obis(0, 50, 4, 0,      10.0, 4);  // L2
    _values[MeterValueID::PowerApparentL3Export]        = obis(0, 70, 4, 0,      10.0, 4);  // L3

    // Scheinleistung Einspeisung (Zählerstand)
    _values[MeterValueID::EnergyApparentLSumExport]     = obis(0, 10, 8, 0, 3600000.0, 8);  // Summe
    _values[MeterValueID::EnergyApparentL1Export]       = obis(0, 30, 8, 0, 3600000.0, 8);  // L1
    _values[MeterValueID::EnergyApparentL2Export]       = obis(0, 50, 8, 0, 3600000.0, 8);  // L2
    _values[MeterValueID::EnergyApparentL3Export]       = obis(0, 70, 8, 0, 3600000.0, 8);  // L3

    // Leistungsfaktor (cos phi)
    _values[MeterValueID::PowerFactorLSum]              = obis(0, 13, 4, 0,    1000.0, 4);  // Summe
    _values[MeterValueID::PowerFactorL1]                = obis(0, 33, 4, 0,    1000.0, 4);  // L1
    _values[MeterValueID::PowerFactorL2]                = obis(0, 53, 4, 0,    1000.0, 4);  // L2
    _values[MeterValueID::PowerFactorL3]                = obis(0, 73, 4, 0,    1000.0, 4);  // L3

    // Spannung
    _values[MeterValueID::VoltageL1N]                   = obis(0, 32, 4, 0,    1000.0, 4);  // L1
    _values[MeterValueID::VoltageL2N]                   = obis(0, 52, 4, 0,    1000.0, 4);  // L2
    _values[MeterValueID::VoltageL3N]                   = obis(0, 72, 4, 0,    1000.0, 4);  // L3

    // Strom
    _values[MeterValueID::CurrentL1ImExSum]             = obis(0, 31, 4, 0,    1000.0, 4);  // L1
    _values[MeterValueID::CurrentL2ImExSum]             = obis(0, 51, 4, 0,    1000.0, 4);  // L2
    _values[MeterValueID::CurrentL3ImExSum]             = obis(0, 71, 4, 0,    1000.0, 4);  // L3

    // Netzfrequenz
    _values[MeterValueID::FrequencyLAvg]                = obis(0, 14, 4, 0,    1000.0, 4);

    if (udp.beginMulticast(mc_groupIP, mc_Port)) {
        logger.printfln("Listening for multicasts to %s:%u", mc_groupIP.toString().c_str(), mc_Port);
    } else {
        logger.printfln("Listening for multicasts to %s:%u failed", mc_groupIP.toString().c_str(), mc_Port);
        return;
    }

    size_t index = 0;
    MeterValueID valueIds[SMA_SPEEDWIRE_VALUE_COUNT + 1];
    assert(_values.size() == SMA_SPEEDWIRE_VALUE_COUNT);

    for (auto const &value : _values) {
        valueIds[index++] = value.first;
    }

    valueIds[index++] = MeterValueID::PowerActiveLSumImExDiff;
    meters.declare_value_ids(slot, valueIds, ARRAY_SIZE(valueIds));

    task_scheduler.scheduleWithFixedDelay([this]() {
        update_all_values();
    }, 0, 990);
}

void MeterSMASpeedwire::update_all_values()
{
    auto packetSize = udp.parsePacket();

    if (packetSize > 0) {
        uint8_t buf[1024];
        auto len = static_cast<size_t>(udp.read(buf, sizeof(buf)));

        if (len == SMA_PACKET_LEN) {
            size_t index = 0;
            float values[SMA_SPEEDWIRE_VALUE_COUNT + 1];

            for (auto &value : _values) {
                values[index++] = value.second.value(buf, len);
            }

            values[index++] = _values.at(MeterValueID::PowerActiveLSumImport).value(buf, len) - _values.at(MeterValueID::PowerActiveLSumExport).value(buf, len);
            meters.update_all_values(slot, values);
        }
    }
}
