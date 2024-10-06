/* esp32-firmware
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

import * as util from "../../ts/util";
import * as API from "../../ts/api";
import { h, createRef, Fragment, Component, ComponentChild, RefObject} from "preact";
import { translate_unchecked, __ } from "../../ts/translation";
import { ConfigComponent } from "../../ts/components/config_component";
import { ConfigForm } from "../../ts/components/config_form";
import { FormRow } from "../../ts/components/form_row";
import { InputNumber } from "../../ts/components/input_number";
import { InputSelect } from "../../ts/components/input_select";
import { InputText } from "../../ts/components/input_text";
import { Switch } from "../../ts/components/switch";
import { SubPage } from "../../ts/components/sub_page";
import { CollapsedSection } from "../../ts/components/collapsed_section";
import { OutputFloat } from "../../ts/components/output_float";
import { IndicatorGroup } from "../../ts/components/indicator_group";
import { Button } from "react-bootstrap";
import { UplotLoader } from "../../ts/components/uplot_loader";
import { UplotWrapper, UplotData } from "../../ts/components/uplot_wrapper";
import { FormSeparator } from "../../ts/components/form_separator";
import { MeterValueID, METER_VALUE_IDS, METER_VALUE_INFOS, METER_VALUE_ORDER } from "../meters/meter_value_id";
import { NavbarItem } from "../../ts/components/navbar_item";
import { Zap } from "react-feather";

export function PhaseSwitcherNavbar() {
    return <NavbarItem name="phase_switcher" module="phase_switcher" title={__("phase_switcher.navbar.phase_switcher")} symbol={<Zap/>} />;
}

// ========= CONFIG =========

type PhaseSwitcherConfig = API.getType['phase_switcher/config'];

interface PhaseSwitcherState {
    state: API.getType['phase_switcher/state'];
    low_level_state: API.getType['phase_switcher/low_level_state'];
    chart_selected: "history_48"|"history_24"|"history_12"|"history_6"|"history_3"|"live";
}

interface CachedData {
    timestamps: number[];
    samples: number[][];
}

function calculate_live_data(offset: number, samples_per_second: number, samples: number[/*channel index*/][]): CachedData {
    let timestamp_slot_count: number = 0;

    if (samples_per_second == 0) { // implies atmost one sample
        timestamp_slot_count = 1;
    } else {
        for (let channel_index = 0; channel_index < samples.length; ++channel_index) {
            if (samples[channel_index] !== null) {
                timestamp_slot_count = Math.max(timestamp_slot_count, samples[channel_index].length);
            }
        }
    }

    let data: CachedData = {timestamps: new Array(timestamp_slot_count), samples: new Array(samples.length)};
    let now = Date.now();
    let start: number;
    let step: number;

    if (samples_per_second == 0) {
        start = now - offset;
        step = 0;
    } else {
        // (timestamp_slot_count - 1) because samples_per_second defines the gaps between
        // two samples. with N samples there are (N - 1) gaps, while the lastest/newest
        // sample is offset milliseconds old
        start = now - (timestamp_slot_count - 1) / samples_per_second * 1000 - offset;
        step = 1 / samples_per_second * 1000;
    }

    for (let timestamp_slot = 0; timestamp_slot < timestamp_slot_count; ++timestamp_slot) {
        data.timestamps[timestamp_slot] = (start + timestamp_slot * step) / 1000;
    }

    for (let channel_index = 0; channel_index < samples.length; ++channel_index) {
        if (samples[channel_index] === null) {
            data.samples[channel_index] = [];
        }
        else {
            data.samples[channel_index] = samples[channel_index];
        }
    }

    return data;
}

function calculate_history_data(offset: number, samples: number[/*channel_index*/][]): CachedData {
    const HISTORY_MINUTE_INTERVAL = 4;

    let timestamp_slot_count: number = 0;

    for (let channel_index = 0; channel_index < samples.length; ++channel_index) {
        if (samples[channel_index] !== null) {
            timestamp_slot_count = Math.max(timestamp_slot_count, samples[channel_index].length);
        }
    }

    let data: CachedData = {timestamps: new Array(timestamp_slot_count), samples: new Array(samples.length-1)};
    let now = Date.now();
    let step = HISTORY_MINUTE_INTERVAL * 60 * 1000;

    // (timestamp_slot_count - 1) because step defines the gaps between two samples.
    // with N samples there are (N - 1) gaps, while the lastest/newest sample is
    // offset milliseconds old. there might be no data point on a full hour
    // interval. to get nice aligned ticks nudge the ticks by at most half of a
    // sampling interval
    let start = Math.round((now - (timestamp_slot_count - 1) * step - offset) / step) * step;

    for (let timestamp_slot = 0; timestamp_slot < timestamp_slot_count; ++timestamp_slot) {
        data.timestamps[timestamp_slot] = (start + timestamp_slot * step) / 1000;
    }

    for (let channel_index = 0; channel_index < samples.length; ++channel_index) {
        if (samples[channel_index] === null) {
            data.samples[channel_index] = [];
        }
        else {
            data.samples[channel_index] = samples[channel_index];
        }
    }

    return data;
}

function array_append<T>(a: Array<T>, b: Array<T>, tail: number): Array<T> {
    a.push(...b);

    return a.slice(-tail);
}

// export class PhaseSwitcher extends ConfigComponent<'phase_switcher/config', {}, PhaseSwitcherConfig & PhaseSwitcherState> {
export class PhaseSwitcher extends ConfigComponent<'phase_switcher/config', {status_ref?: RefObject<PhaseSwitcherStatus>},PhaseSwitcherConfig & PhaseSwitcherState> {
    live_initialized = false;
    live_data: CachedData = {timestamps: [], samples: []};
    pending_live_data: CachedData;
    history_initialized = false;
    history_data: CachedData = {timestamps: [], samples: []};
    uplot_loader_live_ref = createRef();
    uplot_loader_history_ref = createRef();
    uplot_wrapper_live_ref = createRef();
    uplot_wrapper_history_ref = createRef();

    constructor() {
        super('phase_switcher/config',
                __("phase_switcher.script.save_failed"),
                __("phase_switcher.script.reboot_content_changed"), {
                    chart_selected: "history_48",
                });

        util.addApiEventListener('phase_switcher/state', () => {
            this.setState({state: API.get('phase_switcher/state')});
        });

        util.addApiEventListener('phase_switcher/low_level_state', () => {
            this.setState({low_level_state: API.get('phase_switcher/low_level_state')});
        });

        util.addApiEventListener("phase_switcher/live_samples", () => {
            if (!this.live_initialized) {
                // received live_samples before live cache initialization
                this.update_live_cache();
                return;
            }

            let live = API.get("phase_switcher/live_samples");
            let live_extra = calculate_live_data(0, live.samples_per_second, live.samples);

            this.pending_live_data.timestamps.push(...live_extra.timestamps);

            for(let i = 0; i < this.pending_live_data.samples.length; ++i){
                this.pending_live_data.samples[i].push(...live_extra.samples[i]);
            }

            if (this.pending_live_data.timestamps.length >= 5) {
                this.live_data.timestamps = array_append(this.live_data.timestamps, this.pending_live_data.timestamps, 720);
                
                for(let i = 0; i < this.live_data.samples.length; ++i){
                    this.live_data.samples[i] = array_append(this.live_data.samples[i], this.pending_live_data.samples[i], 720);
                }

                this.pending_live_data.timestamps = [];
                this.pending_live_data.samples = [[], [], []];

                if (this.state.chart_selected == "live") {
                    this.update_live_uplot();
                }
            }
        });

        util.addApiEventListener("phase_switcher/history_samples", () => {
            if (!this.history_initialized) {
                // received history_samples before history cache initialization
                this.update_history_cache();
                return;
            }

            let history = API.get("phase_switcher/history_samples");
            let history_samples: number[][] = [[], [], []];

            for(let value_index = 0; value_index < history.samples.length; ++value_index){
                if (history.samples[value_index] !== null) {
                    history_samples[value_index] = array_append(this.history_data.samples[value_index], history.samples[value_index], 720);
                }
            }
            this.history_data = calculate_history_data(0, history_samples);

            if (this.state.chart_selected.startsWith("history")) {
                this.update_history_uplot();
            }
        });
    }

    update_live_cache() {
        this.update_live_cache_async()
            .then((success: boolean) => {
                if (!success) {
                    window.setTimeout(() => {
                        this.update_live_cache();
                    }, 100);

                    return;
                }

                this.update_live_uplot();
            });
    }

    async update_live_cache_async() {
        let response: string = '';

        try {
            response = await (await util.download('phase_switcher/live')).text();
        } catch (e) {
            console.log('Phase switcher: Could not get live data: ' + e);
            return false;
        }

        let payload = JSON.parse(response);

        this.live_initialized = true;
        this.live_data = calculate_live_data(payload.offset, payload.samples_per_second, payload.samples);
        this.pending_live_data = {timestamps: [], samples: [[],[],[]]};

        return true;
    }

    update_history_cache() {
        this.update_history_cache_async()
            .then((success: boolean) => {
                if (!success) {
                    window.setTimeout(() => {
                        this.update_history_cache();
                    }, 100);

                    return;
                }

                this.update_history_uplot();
            });
    }

    async update_history_cache_async() {
        let response: string = '';

        try {
            response = await (await util.download('phase_switcher/history')).text();
        } catch (e) {
            console.log('Phase switcher: Could not get history data: ' + e);
            return false;
        }

        let payload = JSON.parse(response);

        this.history_initialized = true;
        this.history_data = calculate_history_data(payload.offset, payload.samples);

        return true;
    }

    update_live_uplot() {
        if (this.live_initialized && this.uplot_loader_live_ref.current && this.uplot_wrapper_live_ref.current) {
            let live_data: UplotData = {
                keys: [null],
                names: [null],
                values: [this.live_data.timestamps],
            };

            for(let channel_index = 0; channel_index < this.pending_live_data.samples.length; ++channel_index){
                if (this.live_data.samples[channel_index].length > 0) {
                    live_data.keys.push('phase_switcher_' + channel_index);
                    live_data.names.push(get_chart_channel_name(channel_index));
                    live_data.values.push(this.live_data.samples[channel_index]);
                }
            }
            this.uplot_loader_live_ref.current.set_data(live_data.keys.length > 1);
            this.uplot_wrapper_live_ref.current.set_data(live_data);
        }
    }

    update_history_uplot() {
        if (this.history_initialized && this.uplot_loader_history_ref.current && this.uplot_wrapper_history_ref.current) {
            let history_tail = 720; // history_48

            if (this.state.chart_selected == 'history_24') {
                history_tail = 360;
            }
            else if (this.state.chart_selected == 'history_12') {
                history_tail = 180;
            }
            else if (this.state.chart_selected == 'history_6') {
                history_tail = 90;
            }
            else if (this.state.chart_selected == 'history_3') {
                history_tail = 45;
            }

            let history_data: UplotData = {
                keys: [null],
                names: [null],
                values: [this.history_data.timestamps.slice(-history_tail)],
            };

            for(let channel_index = 0; channel_index < this.history_data.samples.length; ++channel_index){
                if (this.history_data.samples[channel_index].length > 0) {
                    history_data.keys.push('phase_switcher_' + channel_index);
                    history_data.names.push(get_chart_channel_name(channel_index));
                    history_data.values.push(this.history_data.samples[channel_index].slice(-history_tail));
                }
            }

            this.uplot_loader_history_ref.current.set_data(history_data.keys.length > 1);
            this.uplot_wrapper_history_ref.current.set_data(history_data);
        }
    }

    render(props: {}, api_data: Readonly<PhaseSwitcherConfig & PhaseSwitcherState>) {
        if (!util.render_allowed() || !API.hasFeature("phase_switcher") || !API.hasFeature("meters")) {
            return <SubPage name="phase_switcher" />;
        }

        let value_ids = API.get_unchecked(`meters/0/value_ids`);
        let values = API.get_unchecked(`meters/0/values`);
        let power = 0;

        if (value_ids && value_ids.length > 0 && values && values.length > 0) {
            let idx = value_ids.indexOf(MeterValueID.PowerActiveLSumImExDiff);

            if (idx >= 0) {
                power = values[idx];
            }
        }
    
        return (
            <SubPage name="phase_switcher">
                <ConfigForm id="phase_switcher_config_form" 
                            title={__("phase_switcher.content.phase_switcher")} 
                            isModified={this.isModified()}
                            isDirty={this.isDirty()}
                            onSave={this.save}
                            onReset={this.reset}
                            onDirtyChange={this.setDirty}>

                    <FormSeparator heading={__("phase_switcher.content.state")}/>

                    <FormRow label={__("phase_switcher.content.sequencer_state")}>
                        <div class="row mx-n1">
                            <div class="mb-1 col-6 px-1">
                                <InputText value={translate_unchecked("phase_switcher.script.sequencer_states." + String(api_data.state.sequencer_state))}/>
                            </div>
                            <div class="mb-1 col-6 px-1">
                                <InputText value={util.format_timespan(Math.floor(api_data.state.time_since_state_change))}/>
                            </div>
                        </div>
                    </FormRow>

                    <FormRow label={__("phase_switcher.content.charging_power.title")} label_muted={__("phase_switcher.content.charging_power.description")}>
                        <div class="row mx-n1">
                            <div class="mb-1 col-6 px-1">
                                <OutputFloat value={api_data.state.available_charging_power} digits={0} scale={0} unit="W" maxFractionalDigitsOnPage={0} maxUnitLengthOnPage={1}/>
                            </div>
                            <div class="mb-1 col-6 px-1">
                                <OutputFloat value={power} digits={0} scale={0} unit="W" maxFractionalDigitsOnPage={0} maxUnitLengthOnPage={1}/>
                            </div>
                        </div>
                    </FormRow>

                    <FormRow label={__("phase_switcher.content.requested_phases")}>
                        <IndicatorGroup
                            style="width: 100%"
                            class="flex-wrap"
                            value={api_data.state.requested_phases_pending}
                            items={[
                                ["primary", __("phase_switcher.status.no_phase")],
                                ["primary", __("phase_switcher.status.one_phase")],
                                ["primary", __("phase_switcher.status.two_phases")],
                                ["primary", __("phase_switcher.status.three_phases")]
                            ]}/>
                    </FormRow>

                    <FormRow label={__("phase_switcher.status.active_phases")}>
                        <IndicatorGroup
                            style="width: 100%"
                            class="flex-wrap"
                            value={api_data.state.active_phases}
                            items={[
                                ["primary", __("phase_switcher.status.no_phase")],
                                ["primary", __("phase_switcher.status.one_phase")],
                                ["primary", __("phase_switcher.status.two_phases")],
                                ["primary", __("phase_switcher.status.three_phases")]
                            ]}/>
                    </FormRow>

                    <FormRow label={__("phase_switcher.content.contactor_state")}>
                        <IndicatorGroup
                            style="width: 100%"
                            class="flex-wrap"
                            value={api_data.state.contactor_state ? 1 : 0}
                            items={[
                                ["primary", __("phase_switcher.content.contactor_state_ok")],
                                ["danger", __("phase_switcher.content.contactor_state_error")]
                            ]}/>
                    </FormRow>


                    <FormSeparator heading={__("phase_switcher.content.configuration")}/>

                    <FormRow label={__("phase_switcher.content.phase_switcher_enabled")}>
                        <Switch desc={__("phase_switcher.content.phase_switcher_enabled_desc")}
                                checked={api_data.enabled}
                                onClick={this.toggle('enabled')}/>
                    </FormRow>

                    <FormRow label={__("phase_switcher.content.operating_mode")}>
                            <InputSelect
                                items={[
                                    ["1", __("phase_switcher.content.one_phase_static")],
                                    ["2", __("phase_switcher.content.two_phases_static")],
                                    ["3", __("phase_switcher.content.three_phases_static")],
                                    ["12", __("phase_switcher.content.one_two_phases_dynamic")],
                                    ["13", __("phase_switcher.content.one_three_phases_dynamic")],
                                    ["123", __("phase_switcher.content.one_two_three_phases_dynamic")],
                                ]}
                                value={api_data.operating_mode}
                                onValue={(v) => {
                                    this.setState({operating_mode: Number(v)});
                                }}/>
                    </FormRow>

                    <FormRow label={__("phase_switcher.content.delay_time.title")} label_muted={__("phase_switcher.content.delay_time.description")}>
                        <div class="row mx-n1">
                            <div class="mb-1 col-4 px-1">
                                <InputNumber required
                                            min={10}
                                            max={3600}
                                            unit="s"
                                            value={api_data.delay_time_more_phases}
                                            onValue={this.set("delay_time_more_phases")}/>
                            </div>
                            <div class="mb-1 col-4 px-1">
                                <InputNumber required
                                            min={10}
                                            max={3600}
                                            unit="s"
                                            value={api_data.delay_time_less_phases}
                                            onValue={this.set("delay_time_less_phases")}/>
                            </div>
                            <div class="mb-1 col-4 px-1">
                                <InputText value={util.format_timespan(api_data.state.delay_time)}/>
                            </div>
                        </div>
                    </FormRow>

                    <FormRow label={__("phase_switcher.content.minimum_duration.title")} label_muted={__("phase_switcher.content.minimum_duration.description")}>
                        <div class="row mx-n1">
                            <div class="mb-1 col-6 px-1">
                                <InputNumber required
                                            min={10}
                                            max={3600}
                                            unit="s"
                                            value={api_data.minimum_duration}
                                            onValue={this.set("minimum_duration")}/>
                            </div>
                            <div class="mb-1 col-6 px-1">
                                <InputText value={util.format_timespan(api_data.state.sequencer_state == 20 ? api_data.state.time_since_state_change : 0)}/>
                            </div>
                        </div>
                    </FormRow>

                    <FormRow label={__("phase_switcher.content.pause_time.title")} label_muted={__("phase_switcher.content.pause_time.description")}>
                        <div class="row mx-n1">
                            <div class="mb-1 col-6 px-1">
                                <InputNumber required
                                            min={10}
                                            max={3600}
                                            unit="s"
                                            value={api_data.pause_time}
                                            onValue={this.set("pause_time")}/>
                            </div>
                            <div class="mb-1 col-6 px-1">
                                <InputText value={util.format_timespan(api_data.state.sequencer_state == 40 ? api_data.state.time_since_state_change : 0)}/>
                            </div>
                        </div>
                    </FormRow>

                    {/* Chart */}                                
                    <FormSeparator heading={__("phase_switcher.content.meter")} first={true} colClasses={"justify-content-between align-items-center col"} extraClasses={"pr-0 pr-lg-3"} >
                        <div class="mb-2">
                            <InputSelect value={this.state.chart_selected} onValue={(v) => {
                                let chart_selected: "history_48"|"history_24"|"history_12"|"history_6"|"history_3"|"live" = v as any;

                                this.setState({chart_selected: chart_selected}, () => {
                                    if (chart_selected == 'live') {
                                        this.uplot_loader_live_ref.current.set_show(true);
                                        this.uplot_wrapper_live_ref.current.set_show(true);
                                        this.uplot_loader_history_ref.current.set_show(false);
                                        this.uplot_wrapper_history_ref.current.set_show(false);
                                            this.update_live_uplot();
                                    }
                                    else {
                                        this.uplot_loader_history_ref.current.set_show(true);
                                        this.uplot_wrapper_history_ref.current.set_show(true);
                                        this.uplot_loader_live_ref.current.set_show(false);
                                        this.uplot_wrapper_live_ref.current.set_show(false);
                                        this.update_history_uplot();
                                    }
                                });
                            }}
                            items={[
                                ["history_48", __("phase_switcher.content.history_48")],
                                ["history_24", __("phase_switcher.content.history_24")],
                                ["history_12", __("phase_switcher.content.history_12")],
                                ["history_6", __("phase_switcher.content.history_6")],
                                ["history_3", __("phase_switcher.content.history_3")],
                                ["live", __("phase_switcher.content.live")],
                            ]}/>
                        </div>
                    </FormSeparator>
                    <div style="position: relative;"> {/* this plain div is neccessary to make the size calculation stable in safari. without this div the height continues to grow */}
                        <UplotLoader ref={this.uplot_loader_live_ref}
                                        show={false}
                                        marker_class={'h3'}
                                        no_data={__("phase_switcher.content.no_data")}
                                        loading={__("phase_switcher.content.loading")} >
                            <UplotWrapper ref={this.uplot_wrapper_live_ref}
                                            class="phase_switcher-chart pb-3"
                                            sub_page="phase_switcher"
                                            color_cache_group="phase_switcher.default"
                                            show={false}
                                            on_mount={() => this.update_live_uplot()}
                                            legend_time_label={__("phase_switcher.script.time")}
                                            legend_time_with_seconds={true}
                                            aspect_ratio={3}
                                            x_height={30}
                                            x_padding_factor={0}
                                            x_include_date={false}
                                            y_diff_min={100}
                                            y_unit="W"
                                            y_label={__("phase_switcher.script.power") + " [Watt]"}
                                            y_digits={0} />
                        </UplotLoader>
                        <UplotLoader ref={this.uplot_loader_history_ref}
                                        show={true}
                                        marker_class={'h3'}
                                        no_data={__("phase_switcher.content.no_data")}
                                        loading={__("phase_switcher.content.loading")} >
                            <UplotWrapper ref={this.uplot_wrapper_history_ref}
                                            class="phase_switcher-chart pb-3"
                                            sub_page="phase_switcher"
                                            color_cache_group="phase_switcher.default"
                                            show={true}
                                            on_mount={() => this.update_history_uplot()}
                                            legend_time_label={__("phase_switcher.script.time")}
                                            legend_time_with_seconds={false}
                                            aspect_ratio={3}
                                            x_height={50}
                                            x_padding_factor={0}
                                            x_include_date={true}
                                            y_min={0}
                                            y_max={1500}
                                            y_unit="W"
                                            y_label={__("phase_switcher.script.power") + " [Watt]"}
                                            y_digits={0} />
                        </UplotLoader>
                    </div>

                    {/* Low Level State */}
                    <CollapsedSection label={__("phase_switcher.content.low_level_state")}>
                        <FormRow label={__("phase_switcher.content.channel_states.title")} label_muted={__("phase_switcher.content.channel_states.description")}>
                            <div class="row mx-n1">
                                <div class="mb-1 col-6 px-1">
                                    {api_data.low_level_state.output_channels.map((x, j) => (
                                        <IndicatorGroup vertical key={j} class="mb-1 col-3 px-1"
                                            value={x ? 0 : 1} //intentionally inverted: the high button is the first
                                            items={[
                                                ["primary", __("phase_switcher.content.channel_high")],
                                                ["secondary",   __("phase_switcher.content.channel_low")]
                                            ]}/>
                                    ))}
                                </div>
                                <div class="mb-1 col-6 px-1">
                                    {api_data.low_level_state.input_channels.map((x, j) => (
                                        <IndicatorGroup vertical key={j} class="mb-1 col-3 px-1"
                                            value={x ? 0 : 1} //intentionally inverted: the high button is the first
                                            items={[
                                                ["primary", __("phase_switcher.content.channel_high")],
                                                ["secondary",   __("phase_switcher.content.channel_low")]
                                            ]}/>
                                    ))}
                                </div>
                            </div>
                        </FormRow>

                        <FormRow label={__("phase_switcher.content.on_delay_values.title")} label_muted={__("phase_switcher.content.on_delay_values.description")}>
                            <div class="row mx-n1">
                                <div class="mb-1 col-4 px-1">
                                    <InputText value={api_data.low_level_state.current_on_delay_time[0] + " s"}/>
                                </div>
                                <div class="mb-1 col-4 px-1">
                                    <InputText value={api_data.low_level_state.current_on_delay_time[1] + " s"}/>
                                </div>
                                <div class="mb-1 col-4 px-1">
                                    <InputText value={api_data.low_level_state.current_on_delay_time[2] + " s"}/>
                                </div>
                            </div>
                        </FormRow>

                        <FormRow label={__("phase_switcher.content.off_delay_values.title")} label_muted={__("phase_switcher.content.off_delay_values.description")}>
                            <div class="row mx-n1">
                                <div class="mb-1 col-4 px-1">
                                    <InputText value={api_data.low_level_state.current_off_delay_time[0] + " s"}/>
                                </div>
                                <div class="mb-1 col-4 px-1">
                                    <InputText value={api_data.low_level_state.current_off_delay_time[1] + " s"}/>
                                </div>
                                <div class="mb-1 col-4 px-1">
                                    <InputText value={api_data.low_level_state.current_off_delay_time[2] + " s"}/>
                                </div>
                            </div>
                        </FormRow>

                    </CollapsedSection>
                </ConfigForm>
            </SubPage>
        );
    }
}

function get_chart_channel_name(channel_index: number) {
    switch(channel_index) { 
        case 0: { 
           return __("phase_switcher.status.available_charging_power"); 
        } 
        case 1: { 
           return __("phase_switcher.content.actual_charging_power"); 
        } 
        case 2: { 
            return __("phase_switcher.status.active_phases"); 
        } 
         default: { 
           return "undefined"; 
        }
     } 
}

// ========= STATUS =========

interface PhaseSwitcherStatusState {
    state: API.getType['phase_switcher/state'];
}

export class PhaseSwitcherStatus extends Component<{}, PhaseSwitcherStatusState> {
    constructor() {
        super();

        util.addApiEventListener('phase_switcher/state', () => {
            this.setState({state: API.get('phase_switcher/state')})
        });
    }

    render(props: {}, state: Readonly<PhaseSwitcherStatusState>) {
        if (!util.render_allowed() || !API.hasFeature("phase_switcher"))
            return <></>;

        return <>
                <FormRow label={__("phase_switcher.status.available_charging_power")} labelColClasses="col-lg-4" contentColClasses="col-lg-8 col-xl-4">
                    <OutputFloat value={state.state.available_charging_power} digits={0} scale={0} unit="W" maxFractionalDigitsOnPage={0} maxUnitLengthOnPage={1}/>
                </FormRow>

                <FormRow label={__("phase_switcher.status.quick_charging")} labelColClasses="col-lg-4" contentColClasses="col-lg-8 col-xl-4">
                    <Button variant="primary" className="form-control"
                        disabled={!(state.state.sequencer_state == 1 || state.state.sequencer_state == 50)}
                        onClick={() =>  API.call('phase_switcher/start_quick_charging', {}, __("phase_switcher.script.start_quick_charging_failed"))}>
                        {__("phase_switcher.status.start_quick_charging")}
                    </Button>                            
                </FormRow>

                <FormRow label={__("phase_switcher.status.active_phases")} labelColClasses="col-lg-4" contentColClasses="col-lg-8 col-xl-4">
                    <IndicatorGroup
                        style="width: 100%"
                        class="flex-wrap"
                        value={state.state.active_phases}
                        items={[
                            ["primary", __("phase_switcher.status.no_phase")],
                            ["primary", __("phase_switcher.status.one_phase")],
                            ["primary", __("phase_switcher.status.two_phases")],
                            ["primary", __("phase_switcher.status.three_phases")]
                        ]}/>
                </FormRow>
            </>;
    }
}

// export function add_event_listeners(source: API.APIEventTarget) {}

export function init() {
}

