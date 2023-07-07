export interface state {
    soc: number,
    sequencer_state: number,
    time_since_state_change: number,
    last_request_status: boolean
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

export interface ignore_once {
    ignore_once: boolean
}

interface VinInfo {
    vin: string,
    make: string,
    modelDescription: string,
    year: number,
    color: string
}

export type vin_info = VinInfo[] | string;