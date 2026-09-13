/* esp32-firmware
 * Copyright (C) 2026 Olaf Lüke <olaf@tinkerforge.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

import * as util from "../../ts/util";
import * as API from "../../ts/api";
import { h, Component, createRef } from "preact";
import { __, translate_unchecked } from "../../ts/translation";
import { ConfigComponent } from "../../ts/components/config_component";
import { FormRow } from "../../ts/components/form_row";
import { InputNumber } from "../../ts/components/input_number";
import { InputSelect } from "../../ts/components/input_select";
import { InputText } from "../../ts/components/input_text";
import { IndicatorGroup } from "../../ts/components/indicator_group";
import { Switch } from "../../ts/components/switch";
import { SubPage } from "../../ts/components/sub_page";
import { NavbarItem } from "../../ts/components/navbar_item";
import { UplotLoader } from "../../ts/components/uplot_loader";
import { UplotData, UplotWrapperB, UplotPath } from "../../ts/components/uplot_wrapper_2nd";
import { Activity } from "react-feather";

type PhaseSwitcherConfig = API.getType["phase_switcher/config"];
type PhaseSwitcherState = API.getType["phase_switcher/state"];
type PhaseSwitcherLowLevelState = API.getType["phase_switcher/low_level_state"];

interface PhaseSwitcherPageState {
    phase_switcher_state: PhaseSwitcherState;
    low_level_state: PhaseSwitcherLowLevelState;
    meter_power: number;
}

export function PhaseSwitcherNavbar() {
    return <NavbarItem name="phase_switcher" module="phase_switcher" title={__("phase_switcher.navbar.phase_switcher")} symbol={<Activity />} />;
}

function phases_items() {
    return [
        ["0", __("phase_switcher.status.no_phase")],
        ["1", __("phase_switcher.status.one_phase")],
        ["2", __("phase_switcher.status.two_phases")],
        ["3", __("phase_switcher.status.three_phases")],
    ] as [string, string][];
}

function format_seconds(seconds: number) {
    return util.format_timespan(Math.floor(seconds));
}

function sequencer_state_name(state: number) {
    return translate_unchecked("phase_switcher.script.sequencer_states." + String(state));
}

function phase_indicator(value: number) {
    return <IndicatorGroup
        style="width: 100%"
        class="flex-wrap"
        value={value}
        items={phases_items().map((item) => ["primary", item[1]])}
    />;
}

class PhaseSwitcherChart extends Component {
    uplot_loader_ref = createRef();
    uplot_wrapper_ref = createRef();
    mounted = false;
    update_interval: number = null;

    override componentDidMount() {
        this.mounted = true;
        this.update_uplot();
        this.update_interval = window.setInterval(() => this.update_uplot(), 10000);
    }

    override componentWillUnmount() {
        this.mounted = false;
        if (this.update_interval != null) {
            window.clearInterval(this.update_interval);
            this.update_interval = null;
        }
    }

    async update_uplot() {
        if (!this.mounted || this.uplot_wrapper_ref.current == null) {
            return;
        }

        const response = await fetch("/phase_switcher/history");

        if (!this.mounted || !response.ok) {
            return;
        }

        const history = await response.json() as {samples?: number[][]};
        const values = history.samples ?? [];
        const value_count = values[0]?.length ?? 0;

        if (values.length < 3 || value_count == 0 || values.some((series) => series.length != value_count)) {
            this.uplot_loader_ref.current.set_data(false);
            return;
        }

        const first_date = Math.floor(Date.now() / 1000) - (value_count - 1) * 60;
        const data: UplotData = {
            keys: [null, "requested_power", "charging_power", "requested_phases"],
            names: [null, __("phase_switcher.content.charging_power.title"), __("phase_switcher.content.actual_charging_power"), __("phase_switcher.content.requested_phases")],
            values: [[], [], [], []],
            stacked: [null, false, false, false],
            paths: [null, UplotPath.Line, UplotPath.Line, UplotPath.Step],
            default_visibilty: [null, true, true, true],
            y_axes: [null, "y", "y", "y2"],
        };

        for (let i = 0; i < value_count; ++i) {
            data.values[0].push(first_date + i * 60);
            data.values[1].push(values[0][i]);
            data.values[2].push(values[1][i]);
            data.values[3].push(values[2][i]);
        }

        this.uplot_loader_ref.current.set_data(true);
        this.uplot_wrapper_ref.current.set_data(data);
    }

    render() {
        return <div class="card">
            <div style="position: relative;">
                <UplotLoader
                    ref={this.uplot_loader_ref}
                    show
                    marker_class="h4"
                    no_data={__("phase_switcher.status.no_phase")}
                    loading={__("phase_switcher.content.state")}
                >
                    <UplotWrapperB
                        ref={this.uplot_wrapper_ref}
                        class="phase-switcher-chart"
                        sub_page="phase_switcher"
                        color_cache_group="phase_switcher.default"
                        show
                        on_mount={() => this.update_uplot()}
                        legend_time_label={__("phase_switcher.script.time")}
                        legend_time_with_minutes
                        aspect_ratio={3}
                        x_format={{hour: "2-digit", minute: "2-digit"}}
                        x_padding_factor={0}
                        x_include_date
                        y_unit="W"
                        y_label={__("phase_switcher.script.power")}
                        y_digits={0}
                        y2_enable
                        y2_min={0}
                        y2_max={3}
                        y2_unit=""
                        y2_label={__("phase_switcher.content.requested_phases")}
                        y2_digits={0}
                        padding={[30, 15, null, 5]}
                    />
                </UplotLoader>
            </div>
        </div>;
    }
}

export class PhaseSwitcher extends ConfigComponent<"phase_switcher/config", {}, PhaseSwitcherPageState> {
    constructor() {
        super("phase_switcher/config", () => __("phase_switcher.script.save_failed"), () => __("phase_switcher.script.reboot_content_changed"));

        this.state = {
            ...(API.get("phase_switcher/config") as PhaseSwitcherConfig),
            phase_switcher_state: API.get("phase_switcher/state"),
            low_level_state: API.get("phase_switcher/low_level_state"),
            meter_power: API.get("meter/values").power,
            internal_isDirty: false,
        } as any;

        util.addApiEventListener("phase_switcher/state", () => this.setState({phase_switcher_state: API.get("phase_switcher/state")}));
        util.addApiEventListener("phase_switcher/low_level_state", () => this.setState({low_level_state: API.get("phase_switcher/low_level_state")}));
        util.addApiEventListener("meter/values", () => this.setState({meter_power: API.get("meter/values").power}));
    }

    render(props: {}, state: PhaseSwitcherConfig & PhaseSwitcherPageState) {
        if (!util.render_allowed()) {
            return <SubPage name="phase_switcher" />;
        }

        const phase_state = state.phase_switcher_state;
        const low_level_state = state.low_level_state;
        const minimum_duration = phase_state.sequencer_state == 20 ? phase_state.time_since_state_change : 0;
        const pause_time = phase_state.sequencer_state == 40 ? phase_state.time_since_state_change : 0;

        return <SubPage name="phase_switcher" title={__("phase_switcher.content.phase_switcher")} colClasses="col-xl-10">
            <SubPage.Status collapsed={!state.enabled}>
                <FormRow label={__("phase_switcher.content.state")}>
                    <InputText value={sequencer_state_name(phase_state.sequencer_state)} />
                </FormRow>
                <FormRow label={__("phase_switcher.content.charging_power.title")} label_muted={__("phase_switcher.content.charging_power.description")}>
                    <div class="row gx-2 gy-1">
                        <div class="col-md-6"><InputText value={phase_state.available_charging_power + " W"} /></div>
                        <div class="col-md-6"><InputText value={util.toLocaleFixed(state.meter_power, 0) + " W"} /></div>
                    </div>
                </FormRow>
                <FormRow label={__("phase_switcher.content.requested_phases")}>
                    {phase_indicator(phase_state.requested_phases_pending)}
                </FormRow>
                <FormRow label={__("phase_switcher.status.active_phases")}>
                    {phase_indicator(phase_state.active_phases)}
                </FormRow>
                <FormRow label={__("phase_switcher.content.contactor_state")}>
                    <IndicatorGroup
                        style="width: 100%"
                        class="flex-wrap"
                        value={phase_state.contactor_state ? 0 : 1}
                        items={[["success", __("phase_switcher.content.contactor_state_ok")], ["danger", __("phase_switcher.content.contactor_state_error")]]}
                    />
                </FormRow>
                <FormRow label={__("phase_switcher.content.delay_time.title")}>
                    <InputText value={format_seconds(phase_state.delay_time)} />
                </FormRow>
                <FormRow label={__("phase_switcher.content.minimum_duration.title")}>
                    <InputText value={format_seconds(minimum_duration)} />
                </FormRow>
                <FormRow label={__("phase_switcher.content.pause_time.title")}>
                    <InputText value={format_seconds(pause_time)} />
                </FormRow>
                <FormRow label={__("phase_switcher.content.meter")}>
                    <PhaseSwitcherChart />
                </FormRow>
            </SubPage.Status>

            <SubPage.Config id="phase_switcher_config_form" isDirty={this.isDirty()} onSave={this.save} onDirtyChange={this.setDirty}>
                <FormRow label={__("phase_switcher.content.phase_switcher_enabled")}>
                    <Switch desc={__("phase_switcher.content.phase_switcher_enabled_desc")} checked={state.enabled} onClick={this.toggle("enabled")} />
                </FormRow>
                <FormRow label={__("phase_switcher.content.operating_mode")}>
                    <InputSelect
                        value={state.operating_mode.toString()}
                        onValue={(value) => this.set("operating_mode")(parseInt(value))}
                        items={[
                            ["1", __("phase_switcher.content.one_phase_static")],
                            ["2", __("phase_switcher.content.two_phases_static")],
                            ["3", __("phase_switcher.content.three_phases_static")],
                            ["12", __("phase_switcher.content.one_two_phases_dynamic")],
                            ["13", __("phase_switcher.content.one_three_phases_dynamic")],
                            ["123", __("phase_switcher.content.one_two_three_phases_dynamic")],
                        ]}
                    />
                </FormRow>
                <FormRow label={__("phase_switcher.content.delay_time.title")} label_muted={__("phase_switcher.content.delay_time.description")}>
                    <div class="row gx-2 gy-1">
                        <div class="col-md-4"><InputNumber min={10} max={3600} value={state.delay_time_more_phases} unit="s" onValue={this.set("delay_time_more_phases")} /></div>
                        <div class="col-md-4"><InputNumber min={10} max={3600} value={state.delay_time_less_phases} unit="s" onValue={this.set("delay_time_less_phases")} /></div>
                        <div class="col-md-4"><InputText value={format_seconds(phase_state.delay_time)} /></div>
                    </div>
                </FormRow>
                <FormRow label={__("phase_switcher.content.minimum_duration.title")} label_muted={__("phase_switcher.content.minimum_duration.description")}>
                    <InputNumber min={10} max={3600} value={state.minimum_duration} unit="s" onValue={this.set("minimum_duration")} />
                </FormRow>
                <FormRow label={__("phase_switcher.content.pause_time.title")} label_muted={__("phase_switcher.content.pause_time.description")}>
                    <InputNumber min={10} max={3600} value={state.pause_time} unit="s" onValue={this.set("pause_time")} />
                </FormRow>
                <FormRow label={__("phase_switcher.content.low_level_state")}>
                    <div class="row gx-2 gy-1">
                        {low_level_state.output_channels.map((value, index) => <InputText key={"output" + index} value={(value ? __("phase_switcher.content.channel_high") : __("phase_switcher.content.channel_low"))} />)}
                        {low_level_state.input_channels.map((value, index) => <InputText key={"input" + index} value={(value ? __("phase_switcher.content.channel_high") : __("phase_switcher.content.channel_low"))} />)}
                    </div>
                </FormRow>
                <FormRow label={__("phase_switcher.content.on_delay_values.title")}>
                    <InputText value={Array.from(low_level_state.current_on_delay_time, (value) => value + " s").join(" / ")} />
                </FormRow>
                <FormRow label={__("phase_switcher.content.off_delay_values.title")}>
                    <InputText value={Array.from(low_level_state.current_off_delay_time, (value) => value + " s").join(" / ")} />
                </FormRow>
            </SubPage.Config>
        </SubPage>;
    }
}

export function pre_init() {
}

export function init() {
}