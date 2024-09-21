export interface state {
    soc: number,
    sequencer_state: number,
    time_since_state_change: number,
    last_request_status: boolean,
    ignore_soc_limit_once: boolean
}

export interface config {
    enabled: boolean,
    user_name: string,
    password: string,
    pin: string,
    vin: string,
    setpoint: number,
    update_rate_when_charging: number,
    update_rate_when_idle: number
}

export interface manual_request {

}

export interface setpoint {
    setpoint: number
}

export interface toggle_ignore_once {

}

interface VinInfo {
    vin: string,
    make: string,
    modelDescription: string,
    year: number,
    color: string
}

export type vin_info = VinInfo[] | string;

export interface live {
    offset: number,
    samples_per_second: number,
    samples: number[][]
}

export interface live_samples {
    samples_per_second: number,
    samples: number[][]
}

export interface history {
    offset: number,
    samples: number[][]
}

export interface history_samples {
    samples: number[][]
}