export interface state {
    operating_mode: number,
    available_charging_power: number,
    requested_phases: number,
    requested_phases_pending: number,
    active_phases: number,
    standalone_sequencer_state: number,
    emulate_energy_manager_state: number,
    time_since_state_change: number,
    delay_time: number,
    contactor_state: boolean
}

export interface config {
    enabled: boolean,
    operating_mode: number,
    delay_time_more_phases: number,
    delay_time_less_phases: number,
    minimum_duration: number,
    pause_time: number
}

export interface low_level_state {
    input_channels: boolean[],
    output_channels: boolean[],
    current_on_delay_time: Uint32Array,
    current_off_delay_time: Uint32Array
}
