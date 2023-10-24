/* esp32-firmware
 * Copyright (C) 2023 Matthias Bolte <matthias@tinkerforge.com>
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

import * as API from "../../ts/api";
import * as util from "../../ts/util";
import { h, Fragment, Component } from "preact";
import { __, translate_unchecked } from "../../ts/translation";
import { MeterClassID } from "../meters/meters_defs";
import { MeterConfig } from "../meters/types";
import { TableModalRow } from "../../ts/components/table";
import { InputText } from "../../ts/components/input_text";
import { InputNumber } from "../../ts/components/input_number";
import { InputSelect } from "../../ts/components/input_select";
import { FormRow } from "../../ts/components/form_row";
import { OutputTextarea } from "../../ts/components/output_textarea";
import { Button, ListGroup, ListGroupItem, Spinner } from "react-bootstrap";
import { SUN_SPEC_MODEL_IDS } from "./sun_spec_model_id";

export type SunSpecMetersConfig = [
    MeterClassID.SunSpec,
    {
        display_name: string
        host: string,
        port: number,
        device_address: number,
        model_id: number,
    },
];

interface DeviceScannerResult {
    unique_id: string
    manufacturer_name: string
    model_name: string
    display_name: string
    options: string
    version: string
    serial_number: string
    device_address: number
    model_id: number
}

interface DeviceScannerProps {
    host: string
    port: number
    onResultSelected: (result: DeviceScannerResult) => void
}

interface DeviceScannerState {
    scan_host: string
    scan_port: number
    scan_running: boolean
    scan_progress: number
    scan_log: string
    scan_results: DeviceScannerResult[]
}

class DeviceScanner extends Component<DeviceScannerProps, DeviceScannerState> {
    constructor() {
        super();

        this.state = {
            scan_host: '',
            scan_port: 0,
            scan_running: false,
            scan_progress: 0,
            scan_log: '',
            scan_results: [],
        } as any;

        util.addApiEventListener('meters_sun_spec/scan_log', (e) => {
            if (e.data == '<<<clear_scan_log>>>') {
                this.setState({scan_log: ''})
            }
            else {
                this.setState({scan_log: this.state.scan_log + e.data + '\n'})
            }
        });

        util.addApiEventListener('meters_sun_spec/scan_progress', (e) => {
            let scan_progress = API.get('meters_sun_spec/scan_progress');

            this.setState({scan_progress: scan_progress.progress});
        });

        util.addApiEventListener('meters_sun_spec/scan_result', () => {
            let scan_result = API.get('meters_sun_spec/scan_result');

            if (scan_result.host === this.props.host && scan_result.port === this.props.port) {
                // this combination must be unique according to sunspec specification
                let unique_id = scan_result.manufacturer_name + scan_result.model_name + scan_result.serial_number;

                if (this.state.scan_results.filter((other) => other.unique_id == unique_id && other.model_id == scan_result.model_id).length == 0) {
                    this.setState({scan_results: this.state.scan_results.concat({
                        unique_id: unique_id,
                        manufacturer_name: scan_result.manufacturer_name,
                        model_name: scan_result.model_name,
                        display_name: scan_result.model_name.startsWith(scan_result.manufacturer_name) ? scan_result.model_name : scan_result.manufacturer_name + ' ' + scan_result.model_name,
                        options: scan_result.options,
                        version: scan_result.version,
                        serial_number: scan_result.serial_number,
                        device_address: scan_result.device_address,
                        model_id: scan_result.model_id,
                    })});
                }
            }
        });

        util.addApiEventListener('meters_sun_spec/scan_done', () => {
            let scan_done = API.get('meters_sun_spec/scan_done');

            if (scan_done.host === this.state.scan_host && scan_done.port === this.state.scan_port) {
                this.setState({scan_host: "", scan_port: 0, scan_running: false});
            }
        });
    }

    render() {
        return <>
            <FormRow label="">
                <Button variant="primary"
                        className="form-control"
                        onClick={async () => {
                            this.setState({scan_host: this.props.host, scan_port: this.props.port, scan_running: true, scan_progress: 0, scan_results: []});
                            try {
                                await API.call('meters_sun_spec/scan',
                                               {host: this.props.host, port: this.props.port},
                                               __("meters_sun_spec.content.scan_failed"));
                            } catch {
                                this.setState({scan_host: "", scan_port: 0, scan_running: false, scan_results: []});
                            }
                        }}
                        disabled={this.props.host.trim().length == 0 || !util.hasValue(this.props.port) || this.state.scan_running}>
                    {__("meters_sun_spec.content.scan")} <Spinner animation="border" size="sm" as="span" className="ml-2" hidden={!this.state.scan_running}/>
                </Button>
            </FormRow>

            {this.state.scan_running ?
                <FormRow label="">
                    <div class="form-progress">
                        <div class="progress-bar form-control progress-bar-no-transition"
                            role="progressbar" style={"padding: 0; width: " + this.state.scan_progress + "%"} aria-valuenow={this.state.scan_progress} aria-valuemin={0}
                            aria-valuemax={100}></div>
                    </div>
                </FormRow>
                : undefined}

            <FormRow label="">
                <OutputTextarea moreClass="mb-1" value={this.state.scan_log} />
            </FormRow>

            {this.state.scan_results.length > 0 ?
                <FormRow label="">
                    <ListGroup>
                        {this.state.scan_results.map(scan_result =>
                            <ListGroupItem action type="button" onClick={() => {this.props.onResultSelected(scan_result)}}>
                                <h5 class="mb-1 pr-2">{scan_result.display_name}</h5>
                                <div class="d-flex w-100 justify-content-between">
                                    <span>{__("meters_sun_spec.content.config_device_address")}: {scan_result.device_address}</span>
                                    <span>{__("meters_sun_spec.content.config_model_id") + ": " + translate_unchecked(`meters_sun_spec.content.model_${scan_result.model_id}`)}</span>
                                </div>
                            </ListGroupItem>)}
                    </ListGroup>
                </FormRow>
                : undefined}
        </>
    }
}

export function init() {
    return {
        [MeterClassID.SunSpec]: {
            name: __("meters_sun_spec.content.meter_class"),
            init: () => [MeterClassID.SunSpec, {display_name: "", host: "", port: 502, device_address: null, model_id: null}] as MeterConfig,
            clone: (config: MeterConfig) => [config[0], {...config[1]}] as MeterConfig,
            get_edit_rows: (config: SunSpecMetersConfig, on_value: (config: SunSpecMetersConfig) => void): TableModalRow[] => {
                let model_ids: [string, string][] = [];

                for (let id of SUN_SPEC_MODEL_IDS) {
                    model_ids.push([id.toString(), translate_unchecked(`meters_sun_spec.content.model_${id}`)]);
                }

                return [
                    {
                        name: __("meters_sun_spec.content.config_host"),
                        value: <InputText
                            required
                            maxLength={64}
                            pattern="^[a-zA-Z0-9\-\.]+$"
                            value={config[1].host}
                            onValue={(v) => {
                                config[1].host = v;
                                on_value(config);
                            }}
                            invalidFeedback={__("meters_sun_spec.content.config_host_invalid")}/>
                    },
                    {
                        name: __("meters_sun_spec.content.config_port"),
                        value: <InputNumber
                            required
                            min={1}
                            max={65535}
                            value={config[1].port}
                            onValue={(v) => {
                                config[1].port = v;
                                on_value(config);
                            }}/>
                    },
                    {
                        name: null,
                        value: <>
                            <hr/>
                            <DeviceScanner host={config[1].host} port={config[1].port} onResultSelected={(result: DeviceScannerResult) => {
                                config[1].display_name = result.display_name;
                                config[1].device_address = result.device_address;
                                config[1].model_id = result.model_id;
                                on_value(config);
                            }} />
                            <hr/>
                        </>
                    },
                    {
                        name: __("meters_sun_spec.content.config_display_name"),
                        value: <InputText
                            required
                            maxLength={32}
                            value={config[1].display_name}
                            onValue={(v) => {
                                config[1].display_name = v;
                                on_value(config);
                            }}/>
                    },
                    {
                        name: __("meters_sun_spec.content.config_device_address"),
                        value: <InputNumber
                            required
                            min={1}
                            max={247}
                            value={config[1].device_address}
                            onValue={(v) => {
                                config[1].device_address = v;
                                on_value(config);
                            }}/>
                    },
                    {
                        name: __("meters_sun_spec.content.config_model_id"),
                        value: <InputSelect
                            required
                            items={model_ids}
                            placeholder={__("meters_sun_spec.content.config_model_id_select")}
                            value={util.hasValue(config[1].model_id) ? config[1].model_id.toString() : config[1].model_id}
                            onValue={(v) => {
                                config[1].model_id = parseInt(v);
                                on_value(config);
                            }}/>
                    },
                ];
            },
        },
    };
}
