/* phase switcher for warp-charger
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
#include "phase_switcher.h"

#include "bindings/errors.h"

#include "event_log_prefix.h"
#include "generated/module_dependencies.h"
#include "tools.h"
#include "modules/web_server/web_server.h"
#include "modules.h"
#include "delay_timer.h"

extern TF_HAL hal;
extern WebServer server;

static constexpr millis_t RELAY_MONOFLOP_REFRESH_INTERVAL = 1_s;

void PhaseSwitcher::pre_setup()
{
    api_config = Config::Object({
        {"operating_mode", Config::Uint8(3)},
        {"delay_time_more_phases", Config::Uint(5 * 60, 10, 60 * 60)},
        {"delay_time_less_phases", Config::Uint(60, 10, 60 * 60)},
        {"minimum_duration", Config::Uint(15 * 60, 10, 60 * 60)},
        {"pause_time", Config::Uint(2 * 60, 10, 60 * 60)},
        {"cp_disconnect_enabled", Config::Bool(false)}
    });

    api_state = Config::Object({
        {"operating_mode", Config::Uint8(3)},
        {"available_charging_power", Config::Uint16(0)},
        {"requested_phases", Config::Uint8(0)},
        {"requested_phases_pending", Config::Uint8(0)},
        {"active_phases", Config::Uint8(1)}, // 0 - no phase active, 1 - one phase active, 2 - two phases active, 3 = three phases active
        {"standalone_sequencer_state", Config::Uint8(0)},
        {"emulate_energy_manager_state", Config::Uint8(0)},
        {"time_since_state_change", Config::Uint32(0)},
        {"delay_time", Config::Uint32(0)},
        {"contactor_state", Config::Bool(false)}
    });

    api_available_charging_power = Config::Object({
        {"power", Config::Uint16(0)}
    });

    api_low_level_state = Config::Object({
        {"input_channels", Config::Array({
            Config::Bool(false),
            Config::Bool(false),
            Config::Bool(false),
            Config::Bool(false),
            }, Config::get_prototype_bool_false(), 4, 4, Config::type_id<Config::ConfBool>())
        },
        {"output_channels", Config::Array({
            Config::Bool(false),
            Config::Bool(false),
            Config::Bool(false),
            Config::Bool(false),
            }, Config::get_prototype_bool_false(), 4, 4, Config::type_id<Config::ConfBool>())
        },
        {"current_on_delay_time", Config::Array({
                Config::Uint32(0),
                Config::Uint32(0),
                Config::Uint32(0),
            }, Config::get_prototype_uint32_0(), 3, 3, Config::type_id<Config::ConfUint>())
        },
        {"current_off_delay_time", Config::Array({
                Config::Uint32(0),
                Config::Uint32(0),
                Config::Uint32(0),
            }, Config::get_prototype_uint32_0(), 3, 3, Config::type_id<Config::ConfUint>())
        }
    });


}

void PhaseSwitcher::setup()
{
    if (!setup_bricklets()){
        return;
    }

    if (!meters.initialized){
        logger.printfln("Energy meter not available. Disabling phase switcher module.");
        return;
    }

    api.restorePersistentConfig("phase_switcher/config", &api_config);
    api_config_in_use = api_config;

    operating_mode = PhaseSwitcherMode(api_config_in_use.get("operating_mode")->asUint());
    enabled = operating_mode != PhaseSwitcherMode::disabled;
    cp_disconnect_enabled = api_config_in_use.get("cp_disconnect_enabled")->asBool();

    if (debug){
        logger.printfln("Phase Switcher Configuration: \n  Enabled: %d\n  Operating mode: %d", enabled, operating_mode);
    }

    api.addFeature("phase_switcher");
    if (operating_mode == emulate_energy_manager) {
        api.addFeature("cp_disconnect");
    }

#if MODULE_POWER_MANAGER_AVAILABLE()
    if (operating_mode == emulate_energy_manager) {
        power_manager.register_phase_switcher_backend(this);
    }
#endif

    io_scheduler.driveUncancelable(
        [this]() { return this->prepare_io(); },
        [this]() {
            this->fetch_all_data();
            this->write_outputs();
        },
        [this]() { this->publish_all_data(); },
        0_ms, 250_ms);

    task_scheduler.scheduleUncancelable([this](){
        update_all_data();
    }, 150_ms, 1_s);

    initialized = true;
}

bool PhaseSwitcher::setup_bricklets()
{   
    bool ret_value[4];
    int result;

    // setup quad relay bricklet:
    if (!quad_relay_bricklet.setup_device()){
        return false;
    };
    result = tf_industrial_quad_relay_v2_get_value(&quad_relay_bricklet.device, ret_value);
    if (result != TF_E_OK) {
        logger.printfln("Industrial quad relay get value failed (rc %d). Disabling phase switcher support.", result);
        return false;
    }

    // setup digital in bricklet:
    if (!digital_in_bricklet.setup_device()){
        return false;
    };
    result = tf_industrial_digital_in_4_v2_get_value(&digital_in_bricklet.device, ret_value);
    if (result != TF_E_OK) {
        logger.printfln("Industrial digital in get value failed (rc %d). Disabling phase switcher support.", result);
        return false;
    }

    // initialize input channel states based on the digital in bricklet's current values
    for (size_t channel = 0; channel < ARRAY_SIZE(io_input_values); ++channel) {
        io_input_values[channel] = ret_value[channel];
    }
    input_channels[0] = ret_value[0];
    input_channels[2] = ret_value[2];
    input_channels[3] = ret_value[3];

    return true;
}

uint16_t PhaseSwitcher::evse_get_max_current()
{
    if (api.hasFeature("evse")) {
        uint16_t max_current_supply_cable = api.getState("evse/slots")->get(0)->get("max_current")->asUint();
        uint16_t max_current_charging_cable = api.getState("evse/slots")->get(1)->get("max_current")->asUint();
        uint16_t max_current_configuration = api.getState("evse/slots")->get(5)->get("max_current")->asUint();

        return(min(min(max_current_supply_cable, max_current_charging_cable), max_current_configuration));
    } else {
        logger.printfln("Failed to retrieve max. current from EVSE bricklet. Using 6 A.");
        return(6000);
    }
}

void PhaseSwitcher::register_urls()
{
    if (!initialized)
        return;

    api.addPersistentConfig("phase_switcher/config", &api_config);

    api.addState("phase_switcher/state", &api_state);
 
    api.addState("phase_switcher/low_level_state", &api_low_level_state);

    api.addCommand("phase_switcher/available_charging_power", &api_available_charging_power, {}, [this](Language /*language*/, String &/*errmsg*/){
        if (enabled && operating_mode != emulate_energy_manager && !quick_charging_active){
            set_available_charging_power(api_available_charging_power.get("power")->asUint());
        }
    }, false);

    api.addCommand("phase_switcher/start_quick_charging", Config::Null(), {}, [this](Language /*language*/, String &/*errmsg*/){
        start_quick_charging();
    }, true);

    server.on("/phase_switcher/start_debug", HTTP_GET, [this](WebServerRequest request) {
        task_scheduler.scheduleOnce([this](){
            logger.printfln("Enabling debug mode");
            debug = true;
        }, 0_ms);
        return request.send_plain(200);
    });

    server.on("/phase_switcher/stop_debug", HTTP_GET, [this](WebServerRequest request){
        task_scheduler.scheduleOnce([this](){
            logger.printfln("Disabling debug mode");
            debug = false;
        }, 0_ms);
        return request.send_plain(200);
    });
}

bool PhaseSwitcher::phase_switching_capable()
{
    return operating_mode == emulate_energy_manager;
}

uint32_t PhaseSwitcher::get_phases()
{
    return requested_phases;
}

PhaseSwitcherBackend::SwitchingState PhaseSwitcher::get_phase_switching_state()
{
    if (contactor_error || get_emulate_energy_manager_phases() == 0) {
        return PhaseSwitcherBackend::SwitchingState::Error;
    }

    if (switching_state != SwitchingState::Idle) {
        return PhaseSwitcherBackend::SwitchingState::Busy;
    }

    return PhaseSwitcherBackend::SwitchingState::Ready;
}

bool PhaseSwitcher::switch_phases(uint32_t /*phases_wanted*/)
{
    return false;
}

bool PhaseSwitcher::is_external_control_allowed()
{
    return false;
}

uint8_t PhaseSwitcher::get_emulate_energy_manager_phases()
{
    if (input_channels[2] != input_channels[3]) {
        return 0;
    }

    return input_channels[2] ? 3 : 1;
}

void PhaseSwitcher::filter_command_packet(cm_command_packet *command_packet)
{
    if (!initialized || operating_mode != emulate_energy_manager ||
        CM_COMMAND_FLAGS_IGNORE_ALLOCATION_IS_SET(command_packet->v1.command_flags)) {
        return;
    }

    cp_disconnect_requested = CM_COMMAND_FLAGS_CPDISC_IS_SET(command_packet->v1.command_flags);

    requested_phases_pending = command_packet->header.version >= 2 ? static_cast<uint8_t>(command_packet->v2.allocated_phases) : 1;

    if (debug) {
        static uint8_t last_requested_phases_pending = 1;
        if (requested_phases_pending != last_requested_phases_pending) {
            logger.printfln("  Filter cm command packet: requested_phases_pending: %u, cp disconnect requested: %d", requested_phases_pending, cp_disconnect_requested);
        }
        last_requested_phases_pending = requested_phases_pending;
    }

    if (contactor_error) {
        command_packet->v1.allocated_current = 0;
        command_packet->v2.allocated_phases = 0;
        return;
    } else if (requested_phases_pending > 3) {
        logger.printfln("Received unsupported allocated phases in command packet: %i", command_packet->v2.allocated_phases);
        requested_phases_pending = 1;
    }

    const uint8_t active_phases = get_active_phases();

    SwitchingState old_state = switching_state;

    switch(switching_state) {
        case SwitchingState::Idle: {
            if (requested_phases_pending != active_phases) {
                if (allocated_current_after_last_disconnect) {
                    skip_cp_disconnect = false;
                    command_packet->v1.allocated_current = 0;
                    switching_state = SwitchingState::Stopping;
                    logger.printfln("Switching to %u phases with CP disconnect", requested_phases_pending);
                } else {
                    skip_cp_disconnect = true;
                    command_packet->v1.allocated_current = 0;
                    switching_state = SwitchingState::WaitUntilSwitched;
                    logger.printfln("Switching %u phases without CP disconnect", requested_phases_pending);
                }
            }
            break;
        }
        case SwitchingState::Stopping: {
            command_packet->v1.allocated_current = 0;
            break;
        }
        case SwitchingState::DisconnectingCP:
        case SwitchingState::DisconnectingCPSettle: {
            command_packet->v1.allocated_current = 0;
            break;
        }
        case SwitchingState::WaitUntilSwitched: {
            command_packet->v1.allocated_current = 0;
            if (deadline_elapsed(last_state_change + seconds_t{api_config_in_use.get("pause_time")->asUint()})) {
                if (skip_cp_disconnect) {
                    switching_state = SwitchingState::Idle;
                } else {
                    switching_state = SwitchingState::WaitUntilCPReconnect;
                }
            }
            break;
        }
        case SwitchingState::WaitUntilCPReconnect: {
            if (deadline_elapsed(last_state_change + 5_s) || command_packet->v2.allocated_phases == 0) {
                switching_state = SwitchingState::Idle;
            } else {
                command_packet->v1.allocated_current = 0;
            }
            break;
        }
        default:
            logger.printfln("Unexpected switching state for cmd: %hhu", static_cast<uint8_t>(switching_state));
            break;
    }

    if (last_charger_state == CHARGER_STATE_NOT_PLUGGED_IN && command_packet->v1.allocated_current == 0) {
        // Not plugged in and no current allocated -> safe
        allocated_current_after_last_disconnect = false;
    } else {
        if ((old_state == SwitchingState::Idle && last_charger_state >= CHARGER_STATE_READY_TO_CHARGE) || command_packet->v1.allocated_current != 0) {
            // Charger ready or charging or allocating current -> not safe
            allocated_current_after_last_disconnect = true;
        }
    }

    if (switching_state != old_state) {
        logger.printfln("Now in state %hhu (command)", static_cast<uint8_t>(switching_state));
        last_state_change = now_us();
    }
}

void PhaseSwitcher::filter_state_packet(cm_state_packet *state_packet)
{
    if (!initialized || operating_mode != emulate_energy_manager) {
        return;
    }

    last_state_packet = now_us();

    state_packet->v1.feature_flags |= CM_FEATURE_FLAGS_PHASE_SWITCH_MASK; // Fake phase-switching support
    state_packet->v3.phases = static_cast<uint8_t>((static_cast<uint32_t>(state_packet->v3.phases) & ~CM_STATE_V3_PHASES_CONNECTED_MASK) | get_active_phases() | CM_STATE_V3_CAN_PHASE_SWITCH_MASK);

    return; // FIXME

    // const uint16_t previous_allowed_current = emulate_energy_manager_allowed_current;
    // const bool previous_zero_current_confirmed = emulate_energy_manager_zero_current_confirmed;
    // emulate_energy_manager_allowed_current = state_packet->v1.allowed_charging_current;
    // if (emulate_energy_manager_state == waiting_for_zero_current) {
    //     emulate_energy_manager_zero_current_confirmed = emulate_energy_manager_allowed_current == 0;
    // }
    // if (debug && emulate_energy_manager_state == waiting_for_zero_current &&
    //     (previous_allowed_current != emulate_energy_manager_allowed_current ||
    //      previous_zero_current_confirmed != emulate_energy_manager_zero_current_confirmed)) {
    //     bool evse_relay_output = false;
    //     const bool evse_relay_output_available = get_evse_relay_output(evse_relay_output);
    //     logger.printfln("  Emulate Energy Manager current status: allowed %u, zero confirmed %d; GPIO 3 %d; pending %u, applied %u",
    //                     emulate_energy_manager_allowed_current, emulate_energy_manager_zero_current_confirmed,
    //             evse_relay_output_available ? static_cast<int>(evse_relay_output) : -1,
    //             requested_phases_pending, requested_phases);
    // }

    // const uint8_t active_phases = requested_phases;
    // uint8_t phase_flags = active_phases & CM_STATE_V3_PHASES_CONNECTED_MASK;
    // if (emulate_energy_manager_state == idle && !contactor_error && active_phases != 0) {
    //     phase_flags |= CM_STATE_V3_CAN_PHASE_SWITCH_MASK;
    // }
    // if (emulate_energy_manager_state != idle) {
    //     phase_flags |= CM_STATE_V3_CURRENTLY_SWITCHING_MASK;
    // }

    // state_packet->v1.feature_flags |= CM_FEATURE_FLAGS_PHASE_SWITCH_MASK;
    // if (emulate_energy_manager_cp_disconnected) {
    //     state_packet->v1.state_flags |= CM_STATE_FLAGS_CP_DISCONNECTED_MASK;
    // } else {
    //     state_packet->v1.state_flags &= ~CM_STATE_FLAGS_CP_DISCONNECTED_MASK;
    // }
    // state_packet->v3.phases = static_cast<uint8_t>(
    //     (state_packet->v3.phases & ~(CM_STATE_V3_PHASES_CONNECTED_MASK |
    //                                  CM_STATE_V3_CAN_PHASE_SWITCH_MASK |
    //                                  CM_STATE_V3_CURRENTLY_SWITCHING_MASK)) |
    //     phase_flags);
}

void PhaseSwitcher::emulate_energy_manager_sequencer(bool evse_relay_output)
{
    if (operating_mode != emulate_energy_manager) {
        switching_state = SwitchingState::Idle;
        return;
    }

    // const uint8_t feedback_phases = get_emulate_energy_manager_phases();
    // static bool have_debug_snapshot = false;
    // static EmulateEnergyManagerState last_debug_state = idle;
    // static bool last_debug_gpio3 = false;
    // static uint16_t last_debug_allowed_current = 0;
    // static bool last_debug_zero_confirmed = false;
    // static uint8_t last_debug_pending_phases = 0;
    // static uint8_t last_debug_applied_phases = 0;
    // static uint8_t last_debug_feedback_phases = 0;
    // static bool last_debug_contactor_error = false;

    // if (debug) {
    //     if (!have_debug_snapshot || last_debug_state != emulate_energy_manager_state ||
    //         last_debug_gpio3 != evse_relay_output ||
    //         last_debug_allowed_current != emulate_energy_manager_allowed_current ||
    //         last_debug_zero_confirmed != emulate_energy_manager_zero_current_confirmed ||
    //         last_debug_pending_phases != requested_phases_pending ||
    //         last_debug_applied_phases != requested_phases ||
    //         last_debug_feedback_phases != feedback_phases ||
    //         last_debug_contactor_error != contactor_error) {
    //         logger.printfln("  Emulate Energy Manager detail: state %u, GPIO 3 %d, allowed current %u, zero confirmed %d, pending %u, applied %u, feedback %u, contactor error %d",
    //                         static_cast<unsigned>(emulate_energy_manager_state), evse_relay_output,
    //                         emulate_energy_manager_allowed_current, emulate_energy_manager_zero_current_confirmed,
    //                         requested_phases_pending, requested_phases, feedback_phases, contactor_error);
    //         last_debug_state = emulate_energy_manager_state;
    //         last_debug_gpio3 = evse_relay_output;
    //         last_debug_allowed_current = emulate_energy_manager_allowed_current;
    //         last_debug_zero_confirmed = emulate_energy_manager_zero_current_confirmed;
    //         last_debug_pending_phases = requested_phases_pending;
    //         last_debug_applied_phases = requested_phases;
    //         last_debug_feedback_phases = feedback_phases;
    //         last_debug_contactor_error = contactor_error;
    //         have_debug_snapshot = true;
    //     }
    // } else {
    //     have_debug_snapshot = false;
    // }

    // switch (emulate_energy_manager_state) {
    //     case idle:
    //         break;
    //     case waiting_for_zero_current:
    //         if (requested_phases_pending == requested_phases) {
    //             emulate_energy_manager_state = idle;
    //             emulate_energy_manager_zero_current_confirmed = false;
    //         } else if (!contactor_error && emulate_energy_manager_zero_current_confirmed &&
    //                    emulate_energy_manager_allowed_current == 0 && !evse_relay_output) {
    //             if (debug) {
    //                 logger.printfln("  Emulate Energy Manager applying %u phase selection.", requested_phases);
    //             }
    //             emulate_energy_manager_state = switching_contactors;
    //             emulate_energy_manager_zero_current_confirmed = false;
    //         }
    //         break;
    //     case switching_contactors:
    //     {
    //         if (now_us() - last_state_change >= seconds_t{api_config_in_use.get("pause_time")->asUint()}){
    //             requested_phases = requested_phases_pending;
    //             logger.printfln("Pause time elapsed, switching to %d phases.", requested_phases);
    //             emulate_energy_manager_state = idle;
    //         }
    //         break;
    //     }
    // }

    // static EmulateEnergyManagerState last_emulate_energy_manager_state = idle;
    // if (last_emulate_energy_manager_state != emulate_energy_manager_state) {
    //     if (debug) {
    //         logger.printfln("  Emulate Energy Manager state changed to: %u",
    //                         static_cast<unsigned>(emulate_energy_manager_state));
    //     }
    //     last_state_change = now_us();
    //     last_emulate_energy_manager_state = emulate_energy_manager_state;
    // }
}

bool PhaseSwitcher::get_evse_relay_output(bool &relay_output)
{
    if (!api.hasFeature("evse")) {
        return false;
    }

    const Config *low_level_state = api.getState("evse/low_level_state", false);
    if (!low_level_state) {
        return false;
    }

    const auto gpio = low_level_state->get("gpio");
    if (!static_cast<const Config *>(gpio)) {
        return false;
    }

    const auto gpio3 = gpio->get(3);
    if (!static_cast<const Config *>(gpio3)) {
        return false;
    }

    relay_output = gpio3->asBool();
    return true;
}

uint8_t PhaseSwitcher::get_active_phases()
{
    if (input_channels[1] && input_channels[2] && input_channels[3]){
        return 3;
    } else if (input_channels[1] && input_channels[2]){
        return 2;
    } else if (input_channels[1]){
        return 1;
    } else {
        return 0;
    }
}

uint8_t PhaseSwitcher::get_phases_for_power(uint16_t available_charging_power)
{
    uint16_t max_current = evse_get_max_current();

    uint16_t max_power_one_phase = (max_current * 230 / 1000);
    uint16_t max_power_two_phases = (max_current * 230 * 2 / 1000);

    // if (debug){
    //     logger.printfln("  get_phases_for_power w/ available_charging_power %d", available_charging_power);
    //     logger.printfln("  get_phases_for_power w/ MIN_POWER_ONE_PHASE %d, MIN_POWER_TWO_PHASES %d, MIN_POWER_THREE_PHASES %d", MIN_POWER_ONE_PHASE, MIN_POWER_TWO_PHASES, MIN_POWER_THREE_PHASES);
    //     logger.printfln("  get_phases_for_power w/ operating_mode %d", operating_mode);
    //     logger.printfln("  max. allowed current %d", max_current);
    // }    

    switch(operating_mode){
        case one_phase_static:
            if (available_charging_power >= MIN_POWER_ONE_PHASE){
                return 1;
            } else {
                return 0;
            }

        case two_phases_static:
            if (available_charging_power >= MIN_POWER_TWO_PHASES){
                return 2;
            } else {
                return 0;
            }

        case three_phases_static:
            if (available_charging_power >= MIN_POWER_THREE_PHASES){
                return 3;
            } else {
                return 0;
            }

        case one_two_phases_dynamic:
            if (available_charging_power >= MIN_POWER_TWO_PHASES && available_charging_power > max_power_one_phase){
                return 2;
            } else if (available_charging_power >= MIN_POWER_ONE_PHASE){
                return 1;
            } else {
                return 0;
            }

        case one_three_phases_dynamic:
            if (available_charging_power >= MIN_POWER_THREE_PHASES && available_charging_power > max_power_one_phase){
                return 3;
            } else if (available_charging_power >= MIN_POWER_ONE_PHASE){
                return 1;
            } else {
                return 0;
            }

        case one_two_three_phases_dynamic:
            if (available_charging_power >= MIN_POWER_THREE_PHASES && available_charging_power > max_power_two_phases){
                return 3;
            } else if (available_charging_power >= MIN_POWER_TWO_PHASES && available_charging_power > max_power_one_phase){
                return 2;
            } else if (available_charging_power >= MIN_POWER_ONE_PHASE){
                return 1;
            } else {
                return 0;
            }
        
        default:
            return 0;
    }
}

void PhaseSwitcher::set_available_charging_power(uint16_t available_charging_power)
{
    PhaseSwitcher::available_charging_power = available_charging_power;
    requested_phases_pending = get_phases_for_power(available_charging_power);
    // if (debug) logger.printfln("  set_available_charging_power w/ requested_phases_pending %d, requested_phases %d", requested_phases_pending, requested_phases);
    set_current(available_charging_power, requested_phases);
}

void PhaseSwitcher::set_current(uint16_t available_charging_power, uint8_t phases)
{
    uint32_t requested_current;
    
    if (phases != 0){
        requested_current = max(min(available_charging_power * 1000 / 230 / phases, 32000), 6000);
    } else {
        requested_current = 32000;    
    }

    String error = api.callCommand("evse/external_current_update", Config::ConfUpdateObject{{
        {"current", requested_current}
    }});
    if (!error.isEmpty()) {
        logger.printfln("External_current_update failed; current=%lu; error=%s",
                        static_cast<unsigned long>(requested_current),
                        error.c_str());
    }
    // if (debug) logger.printfln("Setting current for %d W charging power at %d phases to %.2f A", available_charging_power, phases, ((float)requested_current)/1000);
}

void PhaseSwitcher::handle_button()
{
    if (!api.hasFeature("evse"))
        return;

    if (!enabled || operating_mode == emulate_energy_manager)
        return;

    static uint32_t button_pressed_time, button_released_time;
    static bool quick_charging_requested = false;

    bool button_state = api.getState("evse/low_level_state", false)->get("gpio")->get(0)->asBool();

    if (!button_state)
        button_pressed_time = millis();

    if (button_state)
        button_released_time = millis();

    if (millis_t{millis() - button_released_time} >= QUICK_CHARGE_DELAY_TIME && quick_charging_requested){
        if (debug) logger.printfln("    Button released, initiating quick charging");
        start_quick_charging();
        quick_charging_requested = false;
    }

    if (millis_t{millis() - button_pressed_time} >= QUICK_CHARGE_BUTTON_PRESSED_TIME){
        if (debug) logger.printfln("    Quick charging command received and stored");
        quick_charging_requested = true;
    }
}

void PhaseSwitcher::start_quick_charging()
{
    if (!enabled || operating_mode == emulate_energy_manager)
        return;

    if (standalone_sequencer_state == standby || standalone_sequencer_state == stopped_by_evse){
        logger.printfln("Quick charging requested");
        quick_charging_active = true;
        requested_phases_pending_delayed = 3;
        requested_phases = 3;
        String error = api.callCommand("evse/external_current_update", Config::ConfUpdateObject{{
            {"current", 32000}
        }});
        if (!error.isEmpty()) {
            logger.printfln("Quick-charge current update failed; error=%s", error.c_str());
        }
    } else {
        logger.printfln("Quick charging request ignored because sequencer is not in standby state");
    }

}

void PhaseSwitcher::handle_evse()
{
    if (!api.hasFeature("evse")) {
        if (debug) logger.printfln("Phase switcher handle_evse: API says EVSE module is not supported");
        return;
    }

    charger_state = ChargerState(api.getState("evse/state", false)->get("charger_state")->asUint());
    static ChargerState last_charger_state = charger_state;
    if (debug && charger_state != last_charger_state){
        logger.printfln("  Phase switcher handle_evse: charger_state changed from %d to %d", last_charger_state, charger_state);
        last_charger_state = charger_state;
    }

    iec61851_state = IEC61851State(api.getState("evse/state", false)->get("iec61851_state")->asUint());
    auto_start_charging = api.getState("evse/auto_start_charging", false)->get("auto_start_charging")->asBool();
}

void PhaseSwitcher::monitor_requested_phases()
{
    if (quick_charging_active){
        requested_phases_pending_delayed = 3;
    } else {
        for (int i=0; i<=2; i++){
            if (delayed_phase_request[i]){
                delayed_phase_request[i] = delay_timer[i].on_delay(delay_timer[i].off_delay((requested_phases_pending >= i+1), seconds_t{api_config_in_use.get("delay_time_less_phases")->asUint()}), seconds_t{api_config_in_use.get("delay_time_more_phases")->asUint()});
            } else {
                delayed_phase_request[i] = delay_timer[i].off_delay(delay_timer[i].on_delay((requested_phases_pending >= i+1), seconds_t{api_config_in_use.get("delay_time_more_phases")->asUint()}), seconds_t{api_config_in_use.get("delay_time_less_phases")->asUint()});
            }
        }

        requested_phases_pending_delayed = 0;
        for (int i=0; i<=2; i++){
            if (delayed_phase_request[i]){
                requested_phases_pending_delayed = i+1;
            }
        }
    } 

    if (debug){
        static uint8_t sequencer_last_requested_phases_pending_delayed = requested_phases_pending_delayed;
        if (requested_phases_pending_delayed != sequencer_last_requested_phases_pending_delayed){
            logger.printfln("  requested_phases_pending_delayed changed from %d to %d; requested_phases_pending: %d; requested_phases: %d", sequencer_last_requested_phases_pending_delayed, requested_phases_pending_delayed, requested_phases_pending, requested_phases);
            sequencer_last_requested_phases_pending_delayed = requested_phases_pending_delayed;
        }
    }    
}

void PhaseSwitcher::standalone_sequencer()
{
    if (operating_mode == emulate_energy_manager) {
        standalone_sequencer_state = inactive;
        return;
    }

    if (!enabled || charger_state == not_connected || charger_state == error){
        standalone_sequencer_state = inactive;
        quick_charging_active = false;
        requested_phases = 0;
        return;
    }

    switch(standalone_sequencer_state){
        case inactive:                      sequencer_state_inactive(); break;
        case standby:                       sequencer_state_standby(); break;
        case cancelling_evse_start:         sequencer_state_cancelling_evse_start(); break;
        case sending_start_command_to_evse: sequencer_state_sending_start_command_to_evse(); break;
        case waiting_for_evse_start:        sequencer_state_waiting_for_evse_start(); break;
        case active:                        sequencer_state_active(); break;
        case quick_charging:                sequencer_state_quick_charging(); break;
        case waiting_for_evse_stop:         sequencer_state_waiting_for_evse_stop(); break;
        case pausing_while_switching:       sequencer_state_pausing_while_switching(); break;
        case stopped_by_evse:               sequencer_state_stopped_by_evse(); break;
    }

    static PhaseSwitcherState last_sequencer_state = inactive;
    if (last_sequencer_state != standalone_sequencer_state){
        if (debug) logger.printfln("  Sequencer state changed to: %d", standalone_sequencer_state);
        last_state_change = now_us();
        last_sequencer_state = standalone_sequencer_state;
    } 
}

void PhaseSwitcher::sequencer_state_inactive()
{
    if (charger_state == waiting_for_charge_release && (auto_start_charging || iec61851_state == b_connected)){
        logger.printfln("Vehicle connected, changing to standby state.");
        standalone_sequencer_state = standby;
    } else if (charger_state == ready_for_charging || charger_state == charging){
        if (delayed_phase_request[0]){
            logger.printfln("Charging initiated by EVSE while power is sufficient, waiting for EVSE to start charging.");
            if (!quick_charging_active){
                requested_phases = requested_phases_pending_delayed;
                set_current(api_available_charging_power.get("power")->asUint(), requested_phases);
            }
            standalone_sequencer_state = waiting_for_evse_start;
        } else {
            logger.printfln("Charging initiated by EVSE but requested power is not sufficient. Requesting EVSE to stop charging.");
            standalone_sequencer_state = cancelling_evse_start;
        }
    } 
}

void PhaseSwitcher::sequencer_state_standby()
{
    if (delayed_phase_request[0] || quick_charging_active){
        logger.printfln("Requesting EVSE to start charging.");
        if (!quick_charging_active){
            requested_phases = requested_phases_pending_delayed;
            set_current(api_available_charging_power.get("power")->asUint(), requested_phases);
            if (debug) logger.printfln("  Sequencer state standby; delayed_phase_request[0]: %d; requested_phases: %d", delayed_phase_request[0], requested_phases);
        }
        standalone_sequencer_state = sending_start_command_to_evse;
    } else if (charger_state == ready_for_charging || charger_state == charging){
        logger.printfln("Charging initiated by EVSE but requested power is not sufficient. Requesting EVSE to stop charging.");
        standalone_sequencer_state = cancelling_evse_start;
    }
}

void PhaseSwitcher::sequencer_state_cancelling_evse_start()
{
    static uint32_t watchdog_start = 0;

    if (millis_t{millis() - watchdog_start} >= EVSE_STOP_TIMEOUT){
        logger.printfln("Sending stop API request to EVSE.");
        String error = api.callCommand("evse/stop_charging", nullptr);
        if (!error.isEmpty()) {
            logger.printfln("Stop_charging failed; error=%s", error.c_str());
        }
        watchdog_start = millis();
    }

    if (charger_state != ready_for_charging && charger_state != charging){
        logger.printfln("Charging stopped by EVSE, changing to standby state.");
        watchdog_start = 0;
        standalone_sequencer_state = standby;
    }

}

void PhaseSwitcher::sequencer_state_sending_start_command_to_evse()
{
    logger.printfln("Sending start command to EVSE.");
    String error = api.callCommand("evse/start_charging", nullptr);
    if (!error.isEmpty()) {
        logger.printfln("Start_charging failed; error=%s", error.c_str());
    }
    standalone_sequencer_state = waiting_for_evse_start;
}

void PhaseSwitcher::sequencer_state_waiting_for_evse_start()
{
    static uint32_t watchdog_start = 0;
    static uint8_t start_retries = 0;

    if (watchdog_start == 0){
        watchdog_start = millis();
    }

    if (millis_t{millis() - watchdog_start} >= EVSE_START_TIMEOUT){
        if (start_retries < EVSE_START_RETRIES){
            logger.printfln("Waiting for EVSE timed out, retrying attempt %d.", start_retries + 1);
            standalone_sequencer_state = sending_start_command_to_evse;
            watchdog_start = millis();
            start_retries++;
        } else {
            logger.printfln("Waited for EVSE to start charging for %d times. Aborting.", EVSE_START_RETRIES);
            start_retries = 0;
            watchdog_start = 0;
            standalone_sequencer_state = stopped_by_evse;
        }
    }

    if (charger_state == charging){
        if (quick_charging_active){
            logger.printfln("Charging started by EVSE, changing to quick charging active state.");
            standalone_sequencer_state = quick_charging;
        } else {
            logger.printfln("Charging started by EVSE, changing to active state.");
            standalone_sequencer_state = active;
        }
        watchdog_start = 0;
        start_retries = 0;
    }
}

void PhaseSwitcher::sequencer_state_active()
{
    static uint8_t last_requested_phases_pending_delayed;
    static bool init = false;

    if (!init) {
        last_requested_phases_pending_delayed = requested_phases_pending_delayed;
        init = true;
    }

    const bool minimum_duration_elapsed = now_us() - last_state_change >= seconds_t{api_config_in_use.get("minimum_duration")->asUint()};

    if (requested_phases_pending_delayed != last_requested_phases_pending_delayed && minimum_duration_elapsed){
        logger.printfln("Change to %d phase charging requested while charging with %d phases. Requesting EVSE to stop charging.", requested_phases_pending_delayed, last_requested_phases_pending_delayed);
        init = false;
        standalone_sequencer_state = waiting_for_evse_stop;
    }

    if (charger_state != charging){
        logger.printfln("Charging stopped by EVSE. Waiting either for disconnect or quick charge request.");
        init = false;
        quick_charging_active = false;
        requested_phases = 0;
        standalone_sequencer_state = stopped_by_evse;
    }
}

void PhaseSwitcher::sequencer_state_quick_charging()
{
    if (charger_state != charging){
        logger.printfln("Charging stopped by EVSE. Waiting for either disconnect or quick charge request.");
        set_available_charging_power(api_available_charging_power.get("power")->asUint());
        standalone_sequencer_state = stopped_by_evse;
        quick_charging_active = false;
    }
}

void PhaseSwitcher::sequencer_state_waiting_for_evse_stop()
{
    static uint32_t watchdog_start = 0;

    if (millis_t{millis() - watchdog_start} >= EVSE_STOP_TIMEOUT){
        logger.printfln("Sending stop API request to EVSE.");
        String error = api.callCommand("evse/stop_charging", nullptr);
        if (!error.isEmpty()) {
            logger.printfln("Stop_charging failed; error=%s", error.c_str());
        }
        watchdog_start = millis();
    }

    if (charger_state != charging){
        if (requested_phases_pending_delayed >= 1 && !contactor_error){
            logger.printfln("EVSE stopped charging, waiting for pause time to elapse.");
            requested_phases = requested_phases_pending_delayed;
            standalone_sequencer_state = pausing_while_switching;
        } else {
            logger.printfln("EVSE stopped charging, waiting for car to be disconnected.");
            requested_phases = 0;
            standalone_sequencer_state = standby;
        }
    } 
}

void PhaseSwitcher::sequencer_state_pausing_while_switching()
{
    if (now_us() - last_state_change >= seconds_t{api_config_in_use.get("pause_time")->asUint()}){
        logger.printfln("Pause time elapsed, restarting charging with %d phases.", requested_phases);
        logger.printfln("Waiting for EVSE to start charging.");
        set_current(api_available_charging_power.get("power")->asUint(), requested_phases);
        standalone_sequencer_state = waiting_for_evse_start;
    }
}

void PhaseSwitcher::sequencer_state_stopped_by_evse()
{
    if (quick_charging_active){
        logger.printfln("Requesting EVSE to start charging.");
        standalone_sequencer_state = sending_start_command_to_evse;
    } else if (charger_state == ready_for_charging || charger_state == charging){
        if (delayed_phase_request[0]){
            requested_phases = requested_phases_pending_delayed;
            set_current(api_available_charging_power.get("power")->asUint(), requested_phases);
            standalone_sequencer_state = waiting_for_evse_start;
            if (debug){
                static bool last_phase_requesed = false;
                if (delayed_phase_request[0] != last_phase_requesed){
                    logger.printfln("  Charger state changed to %d, requested_phases %d", charger_state, requested_phases);
                    last_phase_requesed = delayed_phase_request[0];
                }
            }    
        } else {
            logger.printfln("Charging initiated by EVSE but requested power is not sufficient. Requesting EVSE to stop charging.");
            standalone_sequencer_state = cancelling_evse_start;
        }
    }
}

bool PhaseSwitcher::prepare_io()
{
    bool evse_relay_output = false;
    if (!api.hasFeature("evse") ||
        !get_evse_relay_output(evse_relay_output) ||
        !api.getState("evse/state", false) ||
        !api.getState("evse/auto_start_charging", false)) {
        return false;
    }

    if (operating_mode == emulate_energy_manager && !emulate_energy_manager_phase_initialized) {
        if (evse_relay_output) {
            const uint8_t feedback_phases = get_emulate_energy_manager_phases();
            if (feedback_phases != 1 && feedback_phases != 3) {
                return false;
            }
            requested_phases = feedback_phases;
        }
        emulate_energy_manager_phase_initialized = true;
    }

    contactor_check();
    if (operating_mode != emulate_energy_manager) {
        handle_button();
    }
    handle_evse();
    if (operating_mode != emulate_energy_manager) {
        monitor_requested_phases();
    }
    standalone_sequencer();
    emulate_energy_manager_sequencer(evse_relay_output);

    update_outputs(evse_relay_output);

    const uint32_t now_ms = millis();
    for (size_t channel = 0; channel < ARRAY_SIZE(output_channels); ++channel) {
        io_output_targets[channel] = output_channels[channel];
        io_output_update_needed[channel] = !relay_state_known[channel]
                                        || relay_commanded_state[channel] != io_output_targets[channel]
                                        || (io_output_targets[channel]
                                         && millis_t{now_ms - relay_last_refresh_ms[channel]} >= RELAY_MONOFLOP_REFRESH_INTERVAL);
        io_output_attempted[channel] = false;
    }

    return true;
}

void PhaseSwitcher::fetch_all_data()
{
    io_input_rc = tf_industrial_digital_in_4_v2_get_value(&digital_in_bricklet.device, io_input_values);
}

void PhaseSwitcher::update_outputs(bool evse_relay_output)
{
    if (debug) {
        static bool last_evse_relay_output = false;
        if (last_evse_relay_output != evse_relay_output){
            logger.printfln("  EVSE relay output changed to %d; contactor_error %d; requested_phases: %d ", evse_relay_output, contactor_error, requested_phases);
            last_evse_relay_output = evse_relay_output;
        }
    }

    output_channels[0] = !(
        switching_state == SwitchingState::DisconnectingCP ||
        switching_state == SwitchingState::DisconnectingCPSettle ||
        switching_state == SwitchingState::WaitUntilSwitched && !skip_cp_disconnect ||        
        // FIXME: Add support for cp disconnection config
        standalone_sequencer_state != PausingWhileSwitching);

    output_channels[1] = output_channels[2] = output_channels[3] = false;

    if (evse_relay_output){
        if (enabled){
            output_channels[1] = true;
            switch (requested_phases)
            {
            case 0:
                break;
            case 1:
                break;
            case 2:
                output_channels[2] = true;
                break;
            default:
                output_channels[2] = output_channels[3] = true;
                break;
            }
        } else {
            output_channels[1] = output_channels[2] = output_channels[3] = true;
        }
    }

    if (debug && operating_mode == emulate_energy_manager) {
        static bool have_last_values = false;
        static bool last_targets[4] = {};
        static bool last_feedback[4] = {};
        for (size_t channel = 1; channel < ARRAY_SIZE(output_channels); ++channel) {
            if (!have_last_values || last_targets[channel] != output_channels[channel] ||
                last_feedback[channel] != input_channels[channel]) {
                logger.printfln("  Emulate Energy Manager relay %u: target %d, feedback %d, GPIO 3 %d, selected phases %u",
                                static_cast<unsigned>(channel), output_channels[channel],
                                input_channels[channel], evse_relay_output, requested_phases);
                last_targets[channel] = output_channels[channel];
                last_feedback[channel] = input_channels[channel];
            }
        }
        have_last_values = true;
    }
}

void PhaseSwitcher::write_outputs()
{
    for (size_t channel = 0; channel < ARRAY_SIZE(io_output_targets); ++channel) {
        if (!io_output_update_needed[channel]) {
            continue;
        }

        io_output_attempted[channel] = true;
        if (io_output_targets[channel])
            io_output_rc[channel] = tf_industrial_quad_relay_v2_set_monoflop(&quad_relay_bricklet.device, channel, true, 10000);
        else
            io_output_rc[channel] = tf_industrial_quad_relay_v2_set_selected_value(&quad_relay_bricklet.device, channel, false);

        if (io_output_rc[channel] != TF_E_OK) {
            break;
        }
    }
}

void PhaseSwitcher::publish_all_data()
{
    if (io_input_rc != TF_E_OK) {
        logger.printfln("Industrial digital in relay get value failed (rc %d).", io_input_rc);
        input_channels[0] = input_channels[1] = input_channels[2] = input_channels[3] = false;
    } else {
        for (size_t channel = 0; channel < ARRAY_SIZE(input_channels); ++channel) {
            input_channels[channel] = io_input_values[channel];
        }
        input_channels[1] = (api.getState("evse/state", false)->get("contactor_state")->asUint() == 3); // Phase 1 feedback is read by EVSE bricklet
    }

    for (size_t channel = 0; channel < ARRAY_SIZE(io_output_attempted); ++channel) {
        if (!io_output_attempted[channel]) {
            continue;
        }

        if (io_output_rc[channel] != TF_E_OK) {
            logger.printfln("Industrial quad relay set monoflop or value failed for channel %u (rc %d).",
                            static_cast<unsigned>(channel), io_output_rc[channel]);
            continue;
        }

        relay_state_known[channel] = true;
        relay_commanded_state[channel] = io_output_targets[channel];
        relay_last_refresh_ms[channel] = io_output_targets[channel] ? millis() : 0;
    }
}

void PhaseSwitcher::contactor_check()
{
    if (!api.hasFeature("evse")) {
        return;
    }

    static bool contactor_error[4];
    static uint32_t watchdog_start[4];

    for (int i = 1; i <= 3; i++){
        if (input_channels[i] == output_channels[i]) watchdog_start[i] = millis();
        if (millis() - watchdog_start[i] >= 2000){
            if (!contactor_error[i]){
                logger.printfln("Contactor error phase %d set", i);
                contactor_error[i] = true;
            }
        } else {
            if (contactor_error[i]){
                logger.printfln("Contactor error phase %d cleared", i);
                contactor_error[i] = false;
            }
        }
    }
    
    this->contactor_error = (contactor_error[0] || contactor_error[1] || contactor_error[2] || contactor_error[3]);

    if (this->contactor_error && operating_mode != emulate_energy_manager){
        switch(standalone_sequencer_state){
            case waiting_for_evse_start:
            case active:                    
            case quick_charging:
                logger.printfln("Requesting EVSE to stop charging.");
                standalone_sequencer_state = waiting_for_evse_stop;
                break;
            case waiting_for_evse_stop:
                break;
            default:
                standalone_sequencer_state = inactive; 
                break;
        }
    }
}

void PhaseSwitcher::update_all_data()
{
    // state
    api_state.get("operating_mode")->updateUint(uint8_t(operating_mode));
    api_state.get("available_charging_power")->updateUint(available_charging_power);
    api_state.get("requested_phases")->updateUint(requested_phases);
    api_state.get("requested_phases_pending")->updateUint(requested_phases_pending);
    api_state.get("active_phases")->updateUint(get_active_phases());
    api_state.get("standalone_sequencer_state")->updateUint(uint8_t(standalone_sequencer_state));
    api_state.get("emulate_energy_manager_state")->updateUint(uint8_t(emulate_energy_manager_state));
    api_state.get("time_since_state_change")->updateUint((now_us() - last_state_change).to<seconds_t>().as<uint32_t>());

    if (requested_phases_pending > requested_phases){
        api_state.get("delay_time")->updateUint(delay_timer[requested_phases_pending-1].current_value_on_delay);
    } else if (requested_phases_pending < requested_phases){
        api_state.get("delay_time")->updateUint(delay_timer[requested_phases-1].current_value_off_delay);
    } else {
        api_state.get("delay_time")->updateUint(0);
    }

    api_state.get("contactor_state")->updateBool(contactor_error);


    // low level state
    for (int i = 0; i <= 3; ++i)
        api_low_level_state.get("output_channels")->get(i)->updateBool(output_channels[i]);

    for (int i = 0; i <= 3; ++i)
        api_low_level_state.get("input_channels")->get(i)->updateBool(input_channels[i]);

    for (int i = 0; i <= 2; ++i) {
        api_low_level_state.get("current_on_delay_time")->get(i)->updateUint(delay_timer[i].current_value_on_delay);
        api_low_level_state.get("current_off_delay_time")->get(i)->updateUint(delay_timer[i].current_value_off_delay);
    }
}

