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
#include "module_dependencies.h"
#include "phase_switcher.h"

#include "bindings/errors.h"

#include "event_log_prefix.h"
#include "tools.h"
#include "delay_timer.h"

extern TF_HAL hal;

void PhaseSwitcher::pre_setup()
{
    api_config = Config::Object({
        {"enabled", Config::Bool(false)},
        {"operating_mode", Config::Uint8(3)},
        {"delay_time_more_phases", Config::Uint(5 * 60, 10, 60 * 60)},
        {"delay_time_less_phases", Config::Uint(60, 10, 60 * 60)},
        {"minimum_duration", Config::Uint(15 * 60, 10, 60 * 60)},
        {"pause_time", Config::Uint(2 * 60, 10, 60 * 60)}
    });

    api_state = Config::Object({
        {"available_charging_power", Config::Uint16(0)},
        {"requested_phases", Config::Uint8(0)},
        {"requested_phases_pending", Config::Uint8(0)},
        {"active_phases", Config::Uint8(1)}, // 0 - no phase active, 1 - one phase active, 2 - two phases active, 3 = three phases active
        {"sequencer_state", Config::Uint8(0)},
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
            }, new Config{Config::Bool(false)}, 4, 4, Config::type_id<Config::ConfBool>())
        },
        {"output_channels", Config::Array({
            Config::Bool(false),
            Config::Bool(false),
            Config::Bool(false),
            Config::Bool(false),
            }, new Config{Config::Bool(false)}, 4, 4, Config::type_id<Config::ConfBool>())
        },
        {"current_on_delay_time", Config::Array({
                Config::Uint32(0),
                Config::Uint32(0),
                Config::Uint32(0),
            }, new Config{Config::Uint32(0)}, 3, 3, Config::type_id<Config::ConfUint>())
        },
        {"current_off_delay_time", Config::Array({
                Config::Uint32(0),
                Config::Uint32(0),
                Config::Uint32(0),
            }, new Config{Config::Uint32(0)}, 3, 3, Config::type_id<Config::ConfUint>())
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

    power_history.setup();

    api.restorePersistentConfig("phase_switcher/config", &api_config);
    api_config_in_use = api_config;

    enabled = api_config.get("enabled")->asBool();
    if (!enabled){
        api.callCommand("evse/external_current_update", Config::ConfUpdateObject{{
            {"current", 32000}
        }});
    }

    operating_mode = PhaseSwitcherMode(api_config_in_use.get("operating_mode")->asUint());

    if (debug){
        logger.printfln("Phase Switcher Configuration: \n  Enabled: %d\n  Operating mode: %d", enabled, operating_mode);
    }

    api.addFeature("phase_switcher");

    task_scheduler.scheduleWithFixedDelay([this](){
        this->handle_button();
        this->handle_evse();
        this->sequencer();
        this->write_outputs();
        this->contactor_check();
    }, 0, 250);

    task_scheduler.scheduleWithFixedDelay([this](){
        this->monitor_requested_phases();
    }, 100, 1000);

    task_scheduler.scheduleWithFixedDelay([this](){
        update_all_data();
    }, 150, 500);

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

    api.addCommand("phase_switcher/available_charging_power", &api_available_charging_power, {}, [this](){
        if (enabled && !quick_charging_active){
            set_available_charging_power(api_available_charging_power.get("power")->asUint());
        }
    }, false);

    api.addCommand("phase_switcher/start_quick_charging", Config::Null(), {}, [this](){
        start_quick_charging();
    }, true);

    power_history.register_urls("phase_switcher");

    server.on("/phase_switcher/start_debug", HTTP_GET, [this](WebServerRequest request) {
        task_scheduler.scheduleOnce([this](){
            logger.printfln("Enabling debug mode");
            debug = true;
        }, 0);
        return request.send(200);
    });

    server.on("/phase_switcher/stop_debug", HTTP_GET, [this](WebServerRequest request){
        task_scheduler.scheduleOnce([this](){
            logger.printfln("Disabling debug mode");
            debug = false;
        }, 0);
        return request.send(200);
    });
}

uint8_t PhaseSwitcher::get_active_phases()
{
    if (!api.hasFeature("evse")) {
        return 0;
    }

    // phase 1 is monitored via the EVSE bricklet, not via digital in bricklet
    bool channel_state_1 = (api.getState("evse/state", false)->get("contactor_state")->asUint() == 3);
    bool channel_state[4];

    int retval = tf_industrial_digital_in_4_v2_get_value(&digital_in_bricklet.device, channel_state);
    if (retval != TF_E_OK) {
        logger.printfln("Industrial digital in relay get value failed (rc %d).", retval);
        return 0;
    }

    if (channel_state_1 && channel_state[2] && channel_state[3]){
        return 3;
    } else if (channel_state_1 && channel_state[2]){
        return 2;
    } else if (channel_state_1){
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
    //     logger.printfln("  Phase switcher: get_phases_for_power w/ available_charging_power %d", available_charging_power);
    //     logger.printfln("  Phase switcher: get_phases_for_power w/ MIN_POWER_ONE_PHASE %d, MIN_POWER_TWO_PHASES %d, MIN_POWER_THREE_PHASES %d", MIN_POWER_ONE_PHASE, MIN_POWER_TWO_PHASES, MIN_POWER_THREE_PHASES);
    //     logger.printfln("  Phase switcher: get_phases_for_power w/ operating_mode %d", operating_mode);
    //     logger.printfln("  Phase switcher: max. allowed current %d", max_current);
    // }    

    switch(operating_mode){
        case one_phase_static:
            if (debug) logger.printfln("    Phase switcher: get_phases_for_power one phase static");
            if (available_charging_power >= MIN_POWER_ONE_PHASE){
                return 1;
            } else {
                return 0;
            }

        case two_phases_static:
            if (debug) logger.printfln("    Phase switcher: get_phases_for_power two phases static");
            if (available_charging_power >= MIN_POWER_TWO_PHASES){
                return 2;
            } else {
                return 0;
            }

        case three_phases_static:
            if (debug) logger.printfln("    Phase switcher: get_phases_for_power three phases static");
            if (available_charging_power >= MIN_POWER_THREE_PHASES){
                return 3;
            } else {
                return 0;
            }

        case one_two_phases_dynamic:
            if (debug) logger.printfln("    Phase switcher: get_phases_for_power one/two phases dynamic");
            if (available_charging_power >= MIN_POWER_TWO_PHASES && available_charging_power > max_power_one_phase){
                return 2;
            } else if (available_charging_power >= MIN_POWER_ONE_PHASE){
                return 1;
            } else {
                return 0;
            }

        case one_three_phases_dynamic:
            if (debug) logger.printfln("    Phase switcher: get_phases_for_power one/three phases dynamic");
            if (available_charging_power >= MIN_POWER_THREE_PHASES && available_charging_power > max_power_one_phase){
                return 3;
            } else if (available_charging_power >= MIN_POWER_ONE_PHASE){
                return 1;
            } else {
                return 0;
            }

        case one_two_three_phases_dynamic:
            if (debug) logger.printfln("    Phase switcher: get_phases_for_power one/two/three phases dynamic");
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
            if (debug) logger.printfln("    Phase switcher: get_phases_for_power default");
            return 0;
    }
}

void PhaseSwitcher::set_available_charging_power(uint16_t available_charging_power)
{
    PhaseSwitcher::available_charging_power = available_charging_power;
    requested_phases_pending = get_phases_for_power(available_charging_power);
    if (debug) logger.printfln("  Phase switcher: set_available_charging_power w/ requested_phases_pending %d, requested_phases %d", requested_phases_pending, requested_phases);
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

    api.callCommand("evse/external_current_update", Config::ConfUpdateObject{{
        {"current", requested_current}
    }});
    // if (debug) logger.printfln("Setting current for %d W charging power at %d phases to %.2f A", available_charging_power, phases, ((float)requested_current)/1000);
}

void PhaseSwitcher::handle_button()
{
    if (!api.hasFeature("evse")) {
        return;
    }

    static uint32_t button_pressed_time, button_released_time;
    static bool quick_charging_requested = false;

    bool button_state = api.getState("evse/low_level_state", false)->get("gpio")->get(0)->asBool();

    if (!button_state)
        button_pressed_time = millis();

    if (button_state)
        button_released_time = millis();

    if (deadline_elapsed(button_released_time + QUICK_CHARGE_DELAY_TIME) && quick_charging_requested){
        if (debug) logger.printfln("    Phase switcher: Button released, initiating quick charging");
        start_quick_charging();
        quick_charging_requested = false;
    }

    if (deadline_elapsed(button_pressed_time + QUICK_CHARGE_BUTTON_PRESSED_TIME)){
        if (debug) logger.printfln("    Phase switcher: Quick charging command received and stored");
        quick_charging_requested = true;
    }
}

void PhaseSwitcher::start_quick_charging()
{
    if (!enabled)
        return;

    if (sequencer_state == standby || sequencer_state == stopped_by_evse){
        logger.printfln("Quick charging requested");
        quick_charging_active = true;
        requested_phases_pending_delayed = 3;
        requested_phases = 3;
        api.callCommand("evse/external_current_update", Config::ConfUpdateObject{{
            {"current", 32000}
        }});
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
    iec61851_state = IEC61851State(api.getState("evse/state", false)->get("iec61851_state")->asUint());
    auto_start_charging = api.getState("evse/auto_start_charging", false)->get("auto_start_charging")->asBool();
}

void PhaseSwitcher::monitor_requested_phases()
{
    if (quick_charging_active){
        requested_phases_pending_delayed = 3;
    } else {
        requested_phases_pending_delayed = 0;
        for (int i=0; i<=2; i++){
            if (delayed_phase_request[i]){
                delayed_phase_request[i] = delay_timer[i].on_delay(delay_timer[i].off_delay((requested_phases_pending >= i+1), api_config_in_use.get("delay_time_less_phases")->asUint() * 1000), api_config_in_use.get("delay_time_more_phases")->asUint() * 1000);
                requested_phases_pending_delayed = i+1;
            } else {
                delayed_phase_request[i] = delay_timer[i].off_delay(delay_timer[i].on_delay((requested_phases_pending >= i+1), api_config_in_use.get("delay_time_more_phases")->asUint() * 1000), api_config_in_use.get("delay_time_less_phases")->asUint() * 1000);
            }
        }
    } 

    if (debug){
        static uint8_t sequencer_last_requested_phases_pending_delayed = requested_phases_pending_delayed;
        if (requested_phases_pending_delayed != sequencer_last_requested_phases_pending_delayed){
            logger.printfln("  Phase switcher: requested_phases_pending_delayed changed from %d to %d; requested_phases_pending: %d", sequencer_last_requested_phases_pending_delayed, requested_phases_pending_delayed, requested_phases_pending);
            sequencer_last_requested_phases_pending_delayed = requested_phases_pending_delayed;
        }
    }    
}

void PhaseSwitcher::sequencer()
{
    if (!enabled || charger_state == not_connected || charger_state == error){
        sequencer_state = inactive;
        quick_charging_active = false;
        requested_phases = 0;
        return;
    }

    switch(sequencer_state){
        case inactive:                  sequencer_state_inactive(); break;
        case standby:                   sequencer_state_standby(); break;
        case cancelling_evse_start:     sequencer_state_cancelling_evse_start(); break;
        case waiting_for_evse_start:    sequencer_state_waiting_for_evse_start(); break;
        case active:                    sequencer_state_active(); break;
        case quick_charging:            sequencer_state_quick_charging(); break;
        case waiting_for_evse_stop:     sequencer_state_waiting_for_evse_stop(); break;
        case pausing_while_switching:   sequencer_state_pausing_while_switching(); break;
        case stopped_by_evse:           sequencer_state_stopped_by_evse(); break;
    }

    static PhaseSwitcherState last_sequencer_state = inactive;
    if (last_sequencer_state != sequencer_state){
        if (debug) logger.printfln("  Phase switcher sequencer state changed to: %d", sequencer_state);
        last_state_change = millis();
        last_sequencer_state = sequencer_state;
    } 
}

void PhaseSwitcher::sequencer_state_inactive()
{
    if (charger_state == waiting_for_charge_release && (auto_start_charging || iec61851_state == b_connected)){
        logger.printfln("Vehicle connected, changing to standby state.");
        sequencer_state = standby;
    } else if (charger_state == ready_for_charging || charger_state == charging){
        if (delayed_phase_request[0]){
            logger.printfln("Charging initiated by EVSE while power is sufficient, waiting for EVSE to start charging.");
            if (!quick_charging_active){
                requested_phases = requested_phases_pending_delayed;
                set_current(api_available_charging_power.get("power")->asUint(), requested_phases);
            }
            sequencer_state = waiting_for_evse_start;
        } else {
            logger.printfln("Charging initiated by EVSE but requested power is not sufficient. Requesting EVSE to stop charging.");
            sequencer_state = cancelling_evse_start;
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
        }
        sequencer_state = waiting_for_evse_start;
    } else if (charger_state == ready_for_charging || charger_state == charging){
        logger.printfln("Charging initiated by EVSE but requested power is not sufficient. Requesting EVSE to stop charging.");
        sequencer_state = cancelling_evse_start;
    }
}

void PhaseSwitcher::sequencer_state_cancelling_evse_start()
{
    static uint32_t watchdog_start = 0;

    if (deadline_elapsed(watchdog_start + EVSE_STOP_TIMEOUT)){
        logger.printfln("Sending stop API request to EVSE.");
        api.callCommand("evse/stop_charging", nullptr);
        watchdog_start = millis();
    }

    if (charger_state != ready_for_charging && charger_state != charging){
        logger.printfln("Charging stopped by EVSE, changing to standby state.");
        watchdog_start = 0;
        sequencer_state = standby;
    }

}

void PhaseSwitcher::sequencer_state_waiting_for_evse_start()
{
    static uint32_t watchdog_start = 0;
    static uint8_t start_retries = 0;

    if (deadline_elapsed(watchdog_start + EVSE_START_TIMEOUT)){
        if (start_retries < EVSE_START_RETRIES){
            logger.printfln("Sending start API request to EVSE.");
            api.callCommand("evse/start_charging", nullptr);
            watchdog_start = millis();
            start_retries++;
        } else {
            logger.printfln("Tried to start EVSE for %d times. Aborting.", EVSE_START_RETRIES);
            start_retries = 0;
            watchdog_start = 0;
            sequencer_state = stopped_by_evse;
        }
    }

    if (charger_state == charging){
        if (quick_charging_active){
            logger.printfln("Charging started by EVSE, changing to quick charging active state.");
            sequencer_state = quick_charging;
        } else {
            logger.printfln("Charging started by EVSE, changing to active state.");
            sequencer_state = active;
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

    bool minimum_duration_elapsed = deadline_elapsed(last_state_change + api_config_in_use.get("minimum_duration")->asUint() * 1000);

    if (requested_phases_pending_delayed != last_requested_phases_pending_delayed && minimum_duration_elapsed){
        logger.printfln("Change to %d phase charging requested while charging with %d phases. Requesting EVSE to stop charging.", requested_phases_pending_delayed, last_requested_phases_pending_delayed);
        init = false;
        sequencer_state = waiting_for_evse_stop;
    }

    if (charger_state != charging){
        logger.printfln("Charging stopped by EVSE. Waiting either for disconnect or quick charge request.");
        init = false;
        quick_charging_active = false;
        requested_phases = 0;
        sequencer_state = stopped_by_evse;
    }
}

void PhaseSwitcher::sequencer_state_quick_charging()
{
    if (charger_state != charging){
        logger.printfln("Charging stopped by EVSE. Waiting for either disconnect or quick charge request.");
        set_available_charging_power(api_available_charging_power.get("power")->asUint());
        sequencer_state = stopped_by_evse;
        quick_charging_active = false;
    }
}

void PhaseSwitcher::sequencer_state_waiting_for_evse_stop()
{
    static uint32_t watchdog_start = 0;

    if (deadline_elapsed(watchdog_start + EVSE_STOP_TIMEOUT)){
        logger.printfln("Sending stop API request to EVSE.");
        api.callCommand("evse/stop_charging", nullptr);
        watchdog_start = millis();
    }

    if (charger_state != charging){
        if (requested_phases_pending_delayed >= 1 && !contactor_error){
            logger.printfln("EVSE stopped charging, waiting for pause time to elapse.");
            requested_phases = requested_phases_pending_delayed;
            sequencer_state = pausing_while_switching;
        } else {
            logger.printfln("EVSE stopped charging, waiting for car to be disconnected.");
            requested_phases = 0;
            sequencer_state = standby;
        }
    } 
}

void PhaseSwitcher::sequencer_state_pausing_while_switching()
{
    if (deadline_elapsed(last_state_change + api_config_in_use.get("pause_time")->asUint() * 1000)){
        logger.printfln("Pause time elapsed, restarting charging with %d phases.", requested_phases);
        logger.printfln("Waiting for EVSE to start charging.");
        set_current(api_available_charging_power.get("power")->asUint(), requested_phases);
        sequencer_state = waiting_for_evse_start;
    }
}

void PhaseSwitcher::sequencer_state_stopped_by_evse()
{
    if (quick_charging_active){
        logger.printfln("Quick charging active, requesting EVSE to start charging.");
        sequencer_state = waiting_for_evse_start;
    } else if (charger_state == ready_for_charging || charger_state == charging){
        if (delayed_phase_request[0]){
            if (debug) logger.printfln("  Phase switcher: charging started, requested_phases_pending_delayed %d", requested_phases_pending_delayed);
            requested_phases = requested_phases_pending_delayed;
            set_current(api_available_charging_power.get("power")->asUint(), requested_phases);
        } else {
            logger.printfln("Charging initiated by EVSE but requested power is not sufficient. Requesting EVSE to stop charging.");
            sequencer_state = cancelling_evse_start;
        }
    }
}

void PhaseSwitcher::write_outputs()
{
    if (!api.hasFeature("evse")) {
        return;
    }

    bool evse_relay_output = api.getState("evse/low_level_state", false)->get("gpio")->get(3)->asBool();
    bool channel_request[4] = {false, false, false, false};

    if (debug) {
        static bool last_evse_relay_output = false;
        if (last_evse_relay_output != evse_relay_output){
            logger.printfln("EVSE relay output changed to %d; contactor_error %d; requested_phases: %d ", evse_relay_output, contactor_error, requested_phases);
            last_evse_relay_output = evse_relay_output;
        }
    }

    if (evse_relay_output && !contactor_error){
        if (enabled){
            switch (requested_phases)
            {
            case 0:
                break;
            case 1:
                channel_request[1] = true;
                break;
            
            case 2:
                channel_request[1] = true;
                channel_request[2] = true;
                break;

            default:
                channel_request[1] = true;
                channel_request[2] = true;
                channel_request[3] = true;
                break;
            }
        } else {
            channel_request[1] = true;
            channel_request[2] = true;
            channel_request[3] = true;
        } 
    }

    int retval;
    for (int channel = 0; channel <= 3; channel++) {
        if (channel_request[channel])
            retval = tf_industrial_quad_relay_v2_set_monoflop(&quad_relay_bricklet.device, channel, true, 10000);
        else
            retval  = tf_industrial_quad_relay_v2_set_selected_value(&quad_relay_bricklet.device, channel, false);

        if (retval != TF_E_OK) {
            logger.printfln("Industrial quad relay set monoflop or value failed for channel %d (rc %d).", channel, retval);
            return;
        }
    }
}

void PhaseSwitcher::contactor_check()
{
    if (!api.hasFeature("evse")) {
        return;
    }

    static bool contactor_error[4];
    static uint32_t watchdog_start[4];
    bool input_phase[4], output_phase[4], value[4];
    int retval;

    retval = tf_industrial_digital_in_4_v2_get_value(&digital_in_bricklet.device, value);
    if (retval != TF_E_OK) {
        logger.printfln("Industrial digital in relay get value failed (rc %d).", retval);
        return;
    }
    input_phase[1] = (api.getState("evse/state", false)->get("contactor_state")->asUint() == 3);
    input_phase[2] = value[2];
    input_phase[3] = value[3];

    retval = tf_industrial_quad_relay_v2_get_value(&quad_relay_bricklet.device, value);
    if (retval != TF_E_OK) {
        logger.printfln("Industrial quad relay get value failed (rc %d).", retval);
        return;
    }
    output_phase[1] = value[1];
    output_phase[2] = value[2];
    output_phase[3] = value[3];

    for (int i = 1; i <= 3; i++){
        if (input_phase[i] == output_phase[i]) watchdog_start[i] = millis();
        if (deadline_elapsed(watchdog_start[i] + 2000)){
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

    this->contactor_error = (contactor_error[1] || contactor_error[2] || contactor_error[3]);

    if (this->contactor_error){
        switch(sequencer_state){
            case waiting_for_evse_start:
            case active:                    
            case quick_charging:
                logger.printfln("Requesting EVSE to stop charging.");
                sequencer_state = waiting_for_evse_stop;
                break;
            case waiting_for_evse_stop:
                break;
            default:
                sequencer_state = inactive; 
                break;
        }
    }
}

void PhaseSwitcher::update_all_data()
{
    // state
    // api_state.get("available_charging_power")->updateUint(api_available_charging_power.get("power")->asUint());
    api_state.get("available_charging_power")->updateUint(available_charging_power);
    api_state.get("requested_phases")->updateUint(requested_phases);
    api_state.get("requested_phases_pending")->updateUint(requested_phases_pending);
    api_state.get("active_phases")->updateUint(get_active_phases());
    api_state.get("sequencer_state")->updateUint(uint8_t(sequencer_state));
    api_state.get("time_since_state_change")->updateUint((millis() - last_state_change) / 1000);

    if (requested_phases_pending > requested_phases){
        api_state.get("delay_time")->updateUint(delay_timer[requested_phases_pending-1].current_value_on_delay / 1000);
    } else if (requested_phases_pending < requested_phases){
        api_state.get("delay_time")->updateUint(delay_timer[requested_phases-1].current_value_off_delay / 1000);
    } else {
        api_state.get("delay_time")->updateUint(0);
    }

    api_state.get("contactor_state")->updateBool(contactor_error);


    // low level state
    bool channel_state[4];
    int retval;

    retval = tf_industrial_quad_relay_v2_get_value(&quad_relay_bricklet.device, channel_state);
    if (retval != TF_E_OK) {
        logger.printfln("Industrial quad relay get value failed (rc %d).", retval);
        return;
    }
    for (int i = 0; i <= 3; ++i)
        api_low_level_state.get("output_channels")->get(i)->updateBool(channel_state[i]);

    retval = tf_industrial_digital_in_4_v2_get_value(&digital_in_bricklet.device, channel_state);
    if (retval != TF_E_OK) {
        logger.printfln("Industrial digital in relay get value failed (rc %d).", retval);
        return;
    }
    for (int i = 0; i <= 3; ++i)
        api_low_level_state.get("input_channels")->get(i)->updateBool(channel_state[i]);

    for (int i = 0; i <= 2; ++i) {
        api_low_level_state.get("current_on_delay_time")->get(i)->updateUint(delay_timer[i].current_value_on_delay / 1000);
        api_low_level_state.get("current_off_delay_time")->get(i)->updateUint(delay_timer[i].current_value_off_delay / 1000);
    }

    // chart
    int16_t actual_charging_power = 0;
    if (api.hasFeature("meters")){
        actual_charging_power = api.getState("meters/0/values", false)->get(0)->asFloat();
    }
    float samples[3] = {(float)available_charging_power, (float)actual_charging_power, (float)requested_phases_pending};
    power_history.add_sample(samples);

}

