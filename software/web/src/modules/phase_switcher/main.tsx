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

import $ from "../../ts/jq";

import * as util from "../../ts/util";
import * as API from "../../ts/api";

import { h, render, createRef, Fragment, Component, ComponentChild } from "preact";
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
import uPlot from 'uplot';
import { FormSeparator } from "../../ts/components/form_separator";

// ========= CONFIG =========

type PhaseSwitcherConfig = API.getType['phase_switcher/config'];

interface PhaseSwitcherState {
    state: API.getType['phase_switcher/state'];
    low_level_state: API.getType['phase_switcher/low_level_state'];
    chart_selected: "history"|"live";
}

interface MeterValues {
    meter: API.getType["meter/values"];
}


interface UplotData {
    timestamps: number[];
    samples: number[][];
}

interface UplotWrapperProps {
    id: string;
    class: string;
    sidebar_id: string;
    show: boolean;
    legend_time_with_seconds: boolean;
    x_height: number;
    x_include_date: boolean;
    y_min?: number;
    y_max?: number;
    y_diff_min?: number;
}

class UplotWrapper extends Component<UplotWrapperProps, {}> {
    uplot: uPlot;
    data: UplotData;
    pending_data: UplotData;
    visible: boolean = false;
    div_ref = createRef();
    observer: ResizeObserver;
    y_min: number = 0;
    y_max: number = 0;

    shouldComponentUpdate() {
        return false;
    }

    componentDidMount() {
        if (this.uplot) {
            return;
        }

        // FIXME: special hack for status page that is visible by default
        //        and doesn't receive an initial shown event because of that
        this.visible = this.props.sidebar_id === "status";

        // We have to use jquery here or else the events don't fire?
        // This can be removed once the sidebar is ported to preact.
        $(`#sidebar-${this.props.sidebar_id}`).on('shown.bs.tab', () => {
            this.visible = true;

            if (this.pending_data !== undefined) {
                this.set_data(this.pending_data);
            }
        });

        $(`#sidebar-${this.props.sidebar_id}`).on('hidden.bs.tab', () => {
            this.visible = false;
        });

        let get_size = () => {
            let div = this.div_ref.current;
            let aspect_ratio = parseFloat(getComputedStyle(div).aspectRatio);

            if (isNaN(aspect_ratio)) {
                aspect_ratio = 2;
            }

            return {
                width: div.clientWidth,
                height: Math.floor((div.clientWidth + (window.innerWidth - document.documentElement.clientWidth)) / aspect_ratio),
            }
        }

        let options = {
            ...get_size(),
            pxAlign: 0,
            cursor: {
                drag: {
                    x: false, // disable zoom
                },
            },
            series: [
                {
                    label: __("phase_switcher.script.time"),
                    value: (self: uPlot, rawValue: number) => {
                        if (rawValue !== null) {
                            if (this.props.legend_time_with_seconds) {
                                return util.timestamp_sec_to_date(rawValue)
                            }
                            else {
                                return util.timestamp_min_to_date((rawValue / 60), '???');
                            }
                        }

                        return null;
                    },
                },
                {
                    show: true,
                    pxAlign: 0,
                    spanGaps: false,
                    label: __("phase_switcher.status.available_charging_power"),
                    value: (self: uPlot, rawValue: number) => util.hasValue(rawValue) ? util.toLocaleFixed(rawValue) + " W" : null,
                    stroke: "rgb(0, 123, 0)",
                    fill: "rgb(0, 123, 0, 0.1)",
                    width: 2,
                    points: {
                        show: false,
                    },
                },
                {
                    show: true,
                    pxAlign: 0,
                    spanGaps: false,
                    label: __("phase_switcher.content.actual_charging_power"),
                    value: (self: uPlot, rawValue: number) => util.hasValue(rawValue) ? util.toLocaleFixed(rawValue) + " W" : null,
                    stroke: "rgb(0, 123, 255)",
                    fill: "rgb(0, 123, 255, 0.1)",
                    width: 2,
                    points: {
                        show: false,
                    },
                },
                {
                    show: true,
                    pxAlign: 0,
                    spanGaps: false,
                    label: __("phase_switcher.content.requested_phases"),
                    value: (self: uPlot, rawValue: number) => util.hasValue(rawValue) ? util.toLocaleFixed(rawValue) : null,
                    stroke: "rgb(0, 0, 0)",
                    fill: "rgb(0, 0, 0, 0.1)",
                    width: 2,
                    points: {
                        show: false,
                    },
                },
            ],
            axes: [
                {
                    size: this.props.x_height,
                    incrs: [
                        60,
                        60 * 2,
                        3600,
                        3600 * 2,
                        3600 * 4,
                        3600 * 6,
                        3600 * 8,
                        3600 * 12,
                        3600 * 24,
                    ],
                    values: (self: uPlot, splits: number[], axisIdx: number, foundSpace: number, foundIncr: number) => {
                        let values: string[] = new Array(splits.length);
                        let last_year: string = null;
                        let last_month_and_day: string = null;

                        for (let i = 0; i < splits.length; ++i) {
                            let date = new Date(splits[i] * 1000);
                            let value = date.toLocaleString([], {hour: '2-digit', minute: '2-digit'});

                            if (this.props.x_include_date && foundIncr >= 3600) {
                                let year = date.toLocaleString([], {year: 'numeric'});
                                let month_and_day = date.toLocaleString([], {month: '2-digit', day: '2-digit'});

                                if (year != last_year) {
                                    value += '\n' + date.toLocaleString([], {year: 'numeric', month: '2-digit', day: '2-digit'});
                                    last_year = year;
                                    last_month_and_day = month_and_day;
                                }

                                if (month_and_day != last_month_and_day) {
                                    value += '\n' + date.toLocaleString([], {month: '2-digit', day: '2-digit'});
                                    last_month_and_day = month_and_day;
                                }
                            }

                            values[i] = value;
                        }

                        return values;
                    },
                },
                {
                    label: __("phase_switcher.script.power") + " [Watt]",
                    labelSize: 20,
                    labelGap: 2,
                    labelFont: 'bold 14px system-ui, -apple-system, "Segoe UI", Roboto, "Helvetica Neue", Arial, "Noto Sans", sans-serif, "Apple Color Emoji", "Segoe UI Emoji", "Segoe UI Symbol", "Noto Color Emoji"',
                    size: (self: uPlot, values: string[], axisIdx: number, cycleNum: number): number => {
                        let size = 0;

                        if (values) {
                            self.ctx.save();
                            self.ctx.font = self.axes[axisIdx].font;

                            for (let i = 0; i < values.length; ++i) {
                                size = Math.max(size, self.ctx.measureText(values[i]).width);
                            }

                            self.ctx.restore();
                        }

                        return Math.ceil(size / devicePixelRatio) + 20;
                    },
                    values: (self: uPlot, splits: number[]) => {
                        let values: string[] = new Array(splits.length);

                        for (let digits = 0; digits <= 3; ++digits) {
                            let last_value: string = null;
                            let unique = true;

                            for (let i = 0; i < splits.length; ++i) {
                                values[i] = util.toLocaleFixed(splits[i], digits);

                                if (last_value == values[i]) {
                                    unique = false;
                                }

                                last_value = values[i];
                            }

                            if (unique) {
                                break;
                            }
                        }

                        return values;
                    },
                }
            ],
            scales: {
                y: {
                    range: (self: uPlot, initMin: number, initMax: number, scaleKey: string): uPlot.Range.MinMax => {
                        return uPlot.rangeNum(this.y_min, this.y_max, {min: {}, max: {}});
                    }
                },
            },
            padding: [null, 20, null, 5] as uPlot.Padding,
            plugins: [
                {
                    hooks: {
                        setSeries: (self: uPlot, seriesIdx: number, opts: uPlot.Series) => {
                            this.update_internal_data();
                        },
                        drawAxes: [
                            (self: uPlot) => {
                                let ctx = self.ctx;

                                ctx.save();

                                let s  = self.series[0];
                                let xd = self.data[0];
                                let [i0, i1] = s.idxs;
                                let x0 = self.valToPos(xd[i0], 'x', true) - self.axes[0].ticks.size;
                                let y0 = self.valToPos(0, 'y', true);
                                let x1 = self.valToPos(xd[i1], 'x', true);
                                let y1 = self.valToPos(0, 'y', true);

                                const lineWidth = 2;
                                const offset = (lineWidth % 2) / 2;

                                ctx.translate(offset, offset);

                                ctx.beginPath();
                                ctx.lineWidth = lineWidth;
                                ctx.strokeStyle = 'rgb(0,0,0,0.2)';
                                ctx.moveTo(x0, y0);
                                ctx.lineTo(x1, y1);
                                ctx.stroke();

                                ctx.translate(-offset, -offset);

                                ctx.restore();
                            }
                        ],
                    },
                },
            ],
        };

        let div = this.div_ref.current;
        this.uplot = new uPlot(options, [], div);

        let resize = () => {
            let size = get_size();

            if (size.width == 0 || size.height == 0) {
                return;
            }

            this.uplot.setSize(size);
        };

        try {
            this.observer = new ResizeObserver(() => {
                resize();
            });

            this.observer.observe(div);
        } catch (e) {
            setInterval(() => {
                resize();
            }, 500);

            window.addEventListener("resize", e => {
                resize();
            });
        }

        if (this.pending_data !== undefined) {
            this.set_data(this.pending_data);
        }
    }

    render(props?: UplotWrapperProps, state?: Readonly<{}>, context?: any): ComponentChild {
        // the pl ain div is neccessary to make the size calculation stable in safari. without this div the height continues to grow
        return <div><div ref={this.div_ref} id={props.id} class={props.class} style={`display: ${props.show ? 'block' : 'none'};`} /></div>;
    }

    set_show(show: boolean) {
        this.div_ref.current.style.display = show ? 'block' : 'none';
    }

    update_internal_data() {
        let y_min: number = this.props.y_min;
        let y_max: number = this.props.y_max;

        for (let j = 0; j < this.data.samples.length; ++j){

            for (let i = 0; i < this.data.samples[j].length; ++i) {

                let value = this.data.samples[j][i];

                if (value !== null) {
                    if (y_min === undefined || value < y_min) {
                        y_min = value;
                    }

                    if (y_max === undefined || value > y_max) {
                        y_max = value;
                    }
                }
            }
        }

        if (y_min === undefined && y_max === undefined) {
            y_min = 0;
            y_max = 0;
        }
        else if (y_min === undefined) {
            y_min = y_max;
        }
        else if (y_max === undefined) {
            y_max = y_min;
        }

        let y_diff_min = this.props.y_diff_min;

        if (y_diff_min !== undefined) {
            let y_diff = y_max - y_min;

            if (y_diff < y_diff_min) {
                let y_center = y_min + y_diff / 2;

                let new_y_min = Math.floor(y_center - y_diff_min / 2);
                let new_y_max = Math.ceil(y_center + y_diff_min / 2);

                if (new_y_min < 0 && y_min >= 0) {
                    // avoid negative range, if actual minimum is positive
                    y_min = 0;
                    y_max = y_diff_min;
                } else {
                    y_min = new_y_min;
                    y_max = new_y_max;
                }
            }
        }

        y_min = 0;
        this.y_min = y_min;
        this.y_max = y_max;
       
        this.uplot.setData([this.data.timestamps, ...this.data.samples]);
    }

    set_data(data: UplotData) {
        if (!this.uplot || !this.visible) {
            this.pending_data = data;
            return;
        }

        this.data = data;
        this.pending_data = undefined;

        this.update_internal_data();
    }
}

function calculate_live_data(offset: number, samples_per_second: number, samples: number[][]): UplotData {
    let data: UplotData = {timestamps: new Array(samples[0].length), samples: samples};
    let now = Date.now();
    let start;
    let step;
    
    if (samples_per_second == 0) { // implies samples.length == 1
        start = now - offset;
        step = 0;
    } else {
        // (samples.length - 1) because samples_per_second defines the gaps between
        // two samples. with N samples there are (N - 1) gaps, while the lastest/newest
        // sample is offset milliseconds old
        start = now - (samples[0].length - 1) / samples_per_second * 1000 - offset;
        step = 1 / samples_per_second * 1000;
    }

    for(let i = 0; i < samples[0].length; ++i) {
        data.timestamps[i] = (start + i * step) / 1000;
    }

    return data;
}

function calculate_history_data(offset: number, samples: number[][]): UplotData {
    const HISTORY_MINUTE_INTERVAL = 3;

    let data: UplotData = {timestamps: new Array(samples[0].length), samples: samples};
    let now = Date.now();
    let step = HISTORY_MINUTE_INTERVAL * 60 * 1000;
    // (samples[0].length - 1) because step defines the gaps between two samples.
    // with N samples there are (N - 1) gaps, while the lastest/newest sample is
    // offset milliseconds old. there might be no data point on a full hour
    // interval. to get nice aligned ticks nudge the ticks by at most half of a
    // sampling interval
    let start = Math.round((now - (samples[0].length - 1) * step - offset) / step) * step;

    for(let i = 0; i < samples[0].length; ++i) {
        data.timestamps[i] = (start + i * step) / 1000;
    }

    return data;
}

function array_append<T>(a: Array<T>, b: Array<T>, tail: number): Array<T> {
    a.push(...b);

    return a.slice(-tail);
}

export class PhaseSwitcher extends ConfigComponent<'phase_switcher/config', {}, PhaseSwitcherConfig & PhaseSwitcherState & MeterValues> {
    live_data: UplotData;
    pending_live_data: UplotData = {timestamps: [], samples: [[], [], []]};
    history_data: UplotData;
    uplot_wrapper_live_ref = createRef();
    uplot_wrapper_history_ref = createRef();

    constructor() {
        super('phase_switcher/config',
                __("phase_switcher.script.save_failed"),
                __("phase_switcher.script.reboot_content_changed"));

        util.addApiEventListener('phase_switcher/state', () => {
            this.setState({state: API.get('phase_switcher/state')});
        });

        util.addApiEventListener('phase_switcher/low_level_state', () => {
            this.setState({low_level_state: API.get('phase_switcher/low_level_state')});
        });

        util.addApiEventListener('meter/values', () => {
            this.setState({meter: API.get('meter/values')});
        });

        util.addApiEventListener("phase_switcher/live", () => {
            let live = API.get("phase_switcher/live");

            this.live_data = calculate_live_data(live.offset, live.samples_per_second, live.samples);
            this.pending_live_data = {timestamps: [], samples: [[],[],[]]};

            if (this.state.chart_selected == "live") {
                this.update_uplot();
            }
        });

        util.addApiEventListener("phase_switcher/live_samples", () => {
            let live = API.get("phase_switcher/live_samples");
            let live_extra = calculate_live_data(0, live.samples_per_second, live.samples);

            this.pending_live_data.timestamps.push(...live_extra.timestamps);

            for(let i = 0; i < this.pending_live_data.samples.length; ++i){
                this.pending_live_data.samples[i].push(...live_extra.samples[i]);
            }

            if (this.pending_live_data.samples[0].length >= 5) {
                this.live_data.timestamps = array_append(this.live_data.timestamps, this.pending_live_data.timestamps, 720);
                for(let i = 0; i < this.live_data.samples.length; ++i){
                    this.live_data.samples[i] = array_append(this.live_data.samples[i], this.pending_live_data.samples[i], 720);
                }

                this.pending_live_data.timestamps = [];
                this.pending_live_data.samples = [[], [], []];

                if (this.state.chart_selected == "live") {
                    this.update_uplot();
                }
            }
        });

        util.addApiEventListener("phase_switcher/history", () => {
            let history = API.get("phase_switcher/history");

            this.history_data = calculate_history_data(history.offset, history.samples);

            if (this.state.chart_selected == "history") {
                this.update_uplot();
            }
        });

        util.addApiEventListener("phase_switcher/history_samples", () => {
            let history = API.get("phase_switcher/history_samples");
            let samples: number[][] = [[], [], []];

            for(let value_index = 0; value_index < history.samples.length; ++value_index){
                samples[value_index] = array_append(this.history_data.samples[value_index], history.samples[value_index], 720);
            }
            this.history_data = calculate_history_data(0, samples);

            if (this.state.chart_selected == "history") {
                this.update_uplot();
            }
        });

        this.state = {
            chart_selected: "history",
        } as any;
    }

    update_uplot() {
        if (this.state.chart_selected == 'live') {
            if (this.uplot_wrapper_live_ref && this.uplot_wrapper_live_ref.current) {
                this.uplot_wrapper_live_ref.current.set_data(this.live_data);
            }
        }
        else {
            if (this.uplot_wrapper_history_ref && this.uplot_wrapper_history_ref.current) {
                this.uplot_wrapper_history_ref.current.set_data(this.history_data);
            }
        }
    }

    render(props: {}, api_data: Readonly<PhaseSwitcherConfig & PhaseSwitcherState & MeterValues>) {
        if (!util.render_allowed() || !API.hasFeature("phase_switcher") || !API.hasFeature("meter"))
            return <></>

        return (
            <SubPage>
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
                                {/* <InputText value={api_data.state.available_charging_power.toString() + " W"}/> */}
                            </div>
                            <div class="mb-1 col-6 px-1">
                                <OutputFloat value={api_data.meter.power} digits={0} scale={0} unit="W" maxFractionalDigitsOnPage={0} maxUnitLengthOnPage={1}/>
                                {/* <InputText value={api_data.meter.power.toString() + " W"}/> */}
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

                    <FormSeparator heading={__("phase_switcher.content.meter")} first={true} colClasses={"justify-content-between align-items-center col"} extraClasses={"pr-0 pr-lg-3"} >
                        <div class="mb-2">
                            <InputSelect value={this.state.chart_selected} onValue={(v) => {
                                    let chart_selected: "live"|"history" = v as any;

                                    this.setState({chart_selected: chart_selected}, () => {
                                        if (chart_selected == 'live') {
                                            this.uplot_wrapper_live_ref.current.set_show(true);
                                            this.uplot_wrapper_history_ref.current.set_show(false);
                                        }
                                        else {
                                            this.uplot_wrapper_history_ref.current.set_show(true);
                                            this.uplot_wrapper_live_ref.current.set_show(false);
                                        }

                                        this.update_uplot();
                                    });
                                }}
                                items={[
                                    ["history", __("phase_switcher.content.history")],
                                    ["live", __("phase_switcher.content.live")],
                            ]}/>
                        </div>
                    </FormSeparator>
                    <UplotWrapper ref={this.uplot_wrapper_live_ref}
                                    id="phase_switcher_chart_live"
                                    class="phase_switcher-chart"
                                    sidebar_id="phase_switcher"
                                    show={false}
                                    legend_time_with_seconds={true}
                                    x_height={30}
                                    x_include_date={false}
                                    y_diff_min={100} />
                    <UplotWrapper ref={this.uplot_wrapper_history_ref}
                                    id="phase_switcher_chart_history"
                                    class="phase_switcher-chart"
                                    sidebar_id="phase_switcher"
                                    show={true}
                                    legend_time_with_seconds={false}
                                    x_height={50}
                                    x_include_date={true}
                                    y_min={0}
                                    y_max={1500} />

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

render(<PhaseSwitcher/>, $('#phase_switcher')[0])


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

render(<PhaseSwitcherStatus />, $('#status-phase_switcher')[0]);


export function add_event_listeners(source: API.APIEventTarget) {}

export function init() {}

export function update_sidebar_state(module_init: any) {
    $('#sidebar-phase_switcher').prop('hidden', !module_init.phase_switcher);
}

