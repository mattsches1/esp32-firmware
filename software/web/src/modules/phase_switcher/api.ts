//APIPath:phase_switcher/
export interface state {
    available_charging_power: number,
    requested_phases: number,
    requested_phases_pending: number,
    active_phases: number,
    sequencer_state: number,
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

export interface live {
    offset: number,
    samples_per_second: number,
    samples: number[],
}

export interface live_samples {
    samples_per_second: number,
    samples: number[],
}

export interface history {
    offset: number,
    samples: number[],
}

export interface history_samples {
    samples: number[],
}
