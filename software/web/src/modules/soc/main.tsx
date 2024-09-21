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
import { OutputFloat } from "../../ts/components/output_float";
import { Button } from "react-bootstrap";
import { Switch } from "../../ts/components/switch";
import { SubPage } from "../../ts/components/sub_page";
import { IndicatorGroup } from "../../ts/components/indicator_group";
import { FormSeparator } from "../../ts/components/form_separator";
import { InputText } from "../../ts/components/input_text";
import { InputPassword } from "../../ts/components/input_password";
import { InputNumber } from "../../ts/components/input_number";
import { InputFloat } from "../../ts/components/input_float";
import { InputSelect } from "../../ts/components/input_select";
import uPlot from 'uplot';

// ========= CONFIG =========

type SocConfig = API.getType['soc/config'];

interface SocState {
    state: API.getType['soc/state'];
    setpoint: number;
    chart_selected: "history"|"live";
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
                    label: __("soc.script.time"),
                    value: (self: uPlot, rawValue: number) => {
                        if (rawValue !== null) {
                            if (this.props.legend_time_with_seconds) {
                                return util.timestamp_sec_to_date(rawValue)
                            }
                            else {
                                return util.timestamp_min_to_date(rawValue / 60);
                            }
                        }

                        return null;
                    },
                },
                {
                    show: true,
                    pxAlign: 0,
                    spanGaps: false,
                    label: __("soc.script.soc"),
                    value: (self: uPlot, rawValue: number) => util.hasValue(rawValue) ? util.toLocaleFixed(rawValue) + " %" : null,
                    stroke: "rgb(0, 123, 255)",
                    fill: "rgb(0, 123, 255, 0.1)",
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
                    label: __("soc.script.soc") + " [%]",
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
        // the plain div is neccessary to make the size calculation stable in safari. without this div the height continues to grow
        return <div><div ref={this.div_ref} id={props.id} class={props.class} style={`display: ${props.show ? 'block' : 'none'};`} /></div>;
    }

    set_show(show: boolean) {
        this.div_ref.current.style.display = show ? 'block' : 'none';
    }

    update_internal_data() {
        let y_min: number = this.props.y_min;
        let y_max: number = this.props.y_max;

        for (let j = 0; j < this.data.samples.length - 1; ++j){

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
    let data: UplotData = {timestamps: new Array(samples.length), samples: samples};
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
        start = now - (samples.length - 1) / samples_per_second * 1000 - offset;
        step = 1 / samples_per_second * 1000;
    }

    for(let i = 0; i < samples.length; ++i) {
        data.timestamps[i] = (start + i * step) / 1000;
    }

    return data;
}

function calculate_history_data(offset: number, samples: number[][]): UplotData {
    const HISTORY_MINUTE_INTERVAL = 4;

    let data: UplotData = {timestamps: new Array(samples.length), samples: samples};
    let now = Date.now();
    let step = HISTORY_MINUTE_INTERVAL * 60 * 1000;
    // (samples.length - 1) because step defines the gaps between two samples.
    // with N samples there are (N - 1) gaps, while the lastest/newest sample is
    // offset milliseconds old. there might be no data point on a full hour
    // interval. to get nice aligned ticks nudge the ticks by at most half of a
    // sampling interval
    let start = Math.round((now - (samples.length - 1) * step - offset) / step) * step;

    for(let i = 0; i < samples.length; ++i) {
        data.timestamps[i] = (start + i * step) / 1000;
    }

    return data;
}

function array_append<T>(a: Array<T>, b: Array<T>, tail: number): Array<T> {
    a.push(...b);

    return a.slice(-tail);
}

export class Soc extends ConfigComponent<'soc/config', {}, SocConfig & SocState> {
    timeout?: number;
    live_initialized = false;
    live_data: UplotData;
    pending_live_data: UplotData = {timestamps: [], samples: []};
    history_initialized = false;
    history_data: UplotData;
    uplot_wrapper_live_ref = createRef();
    uplot_wrapper_history_ref = createRef();

    constructor() {
        super('soc/config',
                __("soc.script.save_failed"),
                __("soc.script.reboot_content_changed"), {
                    chart_selected: "history",
              });

        this.timeout = undefined;

        util.addApiEventListener('soc/state', () => {
            this.setState({state: API.get('soc/state')});
        });

        util.addApiEventListener('soc/setpoint', () => {
            let _setpoint = API.get('soc/setpoint').setpoint;
            this.setState({setpoint: _setpoint});
        });

        util.addApiEventListener("soc/live_samples", () => {
            if (!this.live_initialized) {
                // received live_samples before live cache initialization
                this.update_live_cache();
                return;
            }

            let live = API.get("soc/live_samples");
            let live_extra = calculate_live_data(0, live.samples_per_second, live.samples);

            this.pending_live_data.timestamps.push(...live_extra.timestamps);
            this.pending_live_data.samples.push(...live_extra.samples);

            if (this.pending_live_data.timestamps.length >= 5) {
                this.live_data.timestamps = array_append(this.live_data.timestamps, this.pending_live_data.timestamps, 720);
                this.live_data.samples = array_append(this.live_data.samples, this.pending_live_data.samples, 720);

                this.pending_live_data.timestamps = [];
                this.pending_live_data.samples = [];

                if (this.state.chart_selected == "live") {
                    this.update_uplot();
                }
            }
        });

        util.addApiEventListener("soc/history_samples", () => {
            if (!this.history_initialized) {
                // received history_samples before history cache initialization
                this.update_history_cache();
                return;
            }

            let history = API.get("soc/history_samples");
            let history_samples: number[][] = []

            if (history.samples !== null) {
                history_samples = array_append(this.history_data.samples, history.samples, 720);
            }

            this.history_data = calculate_history_data(0, history_samples);

            if (this.state.chart_selected == "history") {
                this.update_uplot();
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

                this.update_uplot();
            });
    }

    async update_live_cache_async() {
        let response: string = '';

        try {
            response = await (await util.download('soc/live')).text();
        } catch (e) {
            console.log('SOC: Could not get SOC live data: ' + e);
            return false;
        }

        let payload = JSON.parse(response);

        this.live_initialized = true;
        this.live_data = calculate_live_data(payload.offset, payload.samples_per_second, payload.samples);
        this.pending_live_data = {timestamps: [], samples: []}

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

                this.update_uplot();
            });
    }

    async update_history_cache_async() {
        let response: string = '';

        try {
            response = await (await util.download('soc/history')).text();
        } catch (e) {
            console.log('SOC: Could not get SOC history data: ' + e);
            return false;
        }

        let payload = JSON.parse(response);

        this.history_initialized = true;
        this.history_data = calculate_history_data(payload.offset, payload.samples);

        return true;
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

    render(props: {}, api_data: Readonly<SocConfig & SocState>) {
        if (!util.render_allowed() || !API.hasFeature("soc"))
            return <></>

        return (
            <SubPage>
                <ConfigForm id="soc_config_form" 
                            title={__("soc.content.title")} 
                            isModified={this.isModified()}
                            isDirty={this.isDirty()}
                            onSave={this.save}
                            onReset={this.reset}
                            onDirtyChange={this.setDirty}>

                    <FormSeparator heading={__("soc.content.state")}/>

                    <FormRow label={__("soc.content.soc")}>
                        <div class="row mx-n1">
                            <div class="mb-1 col-8 px-1">
                                <OutputFloat value={api_data.state.soc} digits={0} scale={0} unit="%" maxFractionalDigitsOnPage={0} maxUnitLengthOnPage={1}/>
                            </div>
                            <div class="mb-1 col-4 px-1">
                                <Button variant="primary" className="form-control"
                                    disabled={!api_data.enabled}
                                    onClick={() =>  API.call('soc/manual_request', {}, __("soc.script.manual_request_failed"))}>
                                    {__("soc.content.refresh")}
                                </Button>                            
                            </div>
                        </div>
                    </FormRow>

                    <FormRow label={__("soc.content.sequencer_state.title")} label_muted={__("soc.content.sequencer_state.description")}>
                        <div class="row mx-n1">
                            <div class="mb-1 col-4 px-1">
                                <InputText value={translate_unchecked("soc.script.sequencer_states." + String(api_data.state.sequencer_state))}/>
                            </div>
                            <div class="mb-1 col-4 px-1">
                                <InputText value={util.format_timespan(Math.floor(api_data.state.time_since_state_change))}/>
                            </div>
                            <div class="mb-1 col-4 px-1">
                                <IndicatorGroup
                                    style="width: 100%"
                                    class="flex-wrap"
                                    value={api_data.state.last_request_status ? 0 : 1}
                                    items={[
                                        ["primary", __("soc.content.last_request_status.ok")],
                                        ["danger", __("soc.content.last_request_status.error")],
                                ]}/>
                            </div>
                        </div>
                    </FormRow>

                    <FormRow label={__("soc.content.soc_setpoint.title")} label_muted={__("soc.content.soc_setpoint.description")}>
                        <div class="row mx-n1">
                            <div class="mb-1 col-8 px-1">
                                <InputFloat min={10} max={100} digits={0} unit="%"
                                    value={api_data.setpoint}
                                    onValue={(v) => {
                                        window.clearTimeout(this.timeout);
                                        this.timeout = window.setTimeout(() => API.save('soc/setpoint', {"setpoint": v}, __("soc.script.setpoint_update_failed")), 1000);
                                        this.setState({setpoint: v})
                                    }}/>
                            </div>
                            <div class="mb-1 col-4 px-1">
                                <Switch desc={__("soc.content.ignore_once")}
                                        checked={api_data.state.ignore_soc_limit_once}
                                        onClick={() =>  API.call('soc/toggle_ignore_once', {}, __("soc.script.toggle_ignore_once_failed"))}/>                                
                            </div>
                        </div>
                    </FormRow>

                    <FormSeparator heading={__("soc.content.configuration")}/>

                    <FormRow label={__("soc.content.enabled")}>
                        <Switch desc={__("soc.content.enabled_desc")}
                                checked={api_data.enabled}
                                onClick={this.toggle('enabled')}/>
                    </FormRow>

                    <FormRow label={__("soc.content.user_name")}>
                        <InputText required
                                   value={api_data.user_name}
                                   onValue={this.set("user_name")}
                                   />
                    </FormRow>

                    <FormRow label={__("soc.content.password")}>
                        <InputPassword minLength={1} maxLength={64}
                                       value={api_data.password}
                                       onValue={this.set("password")}
                                       />
                    </FormRow>

                    <FormRow label={__("soc.content.pin")}>
                        <InputPassword minLength={4} maxLength={4}
                                       value={api_data.pin}
                                       onValue={this.set("pin")}
                                       />
                    </FormRow>

                    <FormRow label={__("soc.content.vin.title")} label_muted={__("soc.content.vin.description")}>
                        <InputText minLength={17} maxLength={17}
                                   pattern="[A-Z0-9]{17}"
                                   required
                                   value={api_data.vin}
                                   onValue={this.set("vin")}
                                   invalidFeedback={__("soc.content.vin_invalid")}
                                   />
                    </FormRow>

                    <FormRow label={__("soc.content.update_rate_when_idle.title")} label_muted={__("soc.content.update_rate_when_idle.description")}>
                        <InputNumber required
                                    min={60}
                                    max={36000}
                                    unit="s"
                                    value={api_data.update_rate_when_idle}
                                    onValue={this.set("update_rate_when_idle")}/>
                    </FormRow>


                    <FormSeparator heading={__("soc.content.chart")} first={true} colClasses={"justify-content-between align-items-center col"} extraClasses={"pr-0 pr-lg-3"} >
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
                                    ["history", __("soc.content.history")],
                                    ["live", __("soc.content.live")],
                            ]}/>
                        </div>
                    </FormSeparator>
                    <UplotWrapper ref={this.uplot_wrapper_live_ref}
                                    id="soc_chart_live"
                                    class="soc-chart"
                                    sidebar_id="soc"
                                    show={false}
                                    legend_time_with_seconds={true}
                                    x_height={30}
                                    x_include_date={false}
                                    y_diff_min={100} />
                    <UplotWrapper ref={this.uplot_wrapper_history_ref}
                                    id="soc_chart_history"
                                    class="soc-chart"
                                    sidebar_id="soc"
                                    show={true}
                                    legend_time_with_seconds={false}
                                    x_height={50}
                                    x_include_date={true}
                                    y_min={0}
                                    y_max={100} />
                </ConfigForm>
            </SubPage>
        );
    }
}

render(<Soc/>, $('#soc')[0])


// ========= STATUS =========

interface SocStatusState {
    state: API.getType['soc/state'];
}

export class SocStatus extends Component<{}, SocStatusState> {
    constructor() {
        super();

        util.addApiEventListener('soc/state', () => {
            this.setState({state: API.get('soc/state')})
        });
    }

    render(props: {}, state: Readonly<SocStatusState>) {
        if (!util.render_allowed() || !API.hasFeature("soc"))
            return <></>;

        return <>
                <FormRow label={__("soc.status.soc")} labelColClasses="col-lg-4" contentColClasses="col-lg-8 col-xl-4">
                    <OutputFloat value={state.state.soc} digits={0} scale={0} unit="%" maxFractionalDigitsOnPage={0} maxUnitLengthOnPage={1}/>
                </FormRow>
            </>;
    }

}

render(<SocStatus />, $('#status-soc')[0]);

export function add_event_listeners(source: API.APIEventTarget) {}

export function init() {}

export function update_sidebar_state(module_init: any) {
    $('#sidebar-soc').prop('hidden', !module_init.soc);
}

