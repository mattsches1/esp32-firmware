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
import { h, Component, RefObject } from "preact";
import { __, translate_unchecked } from "../../ts/translation";
import { ConfigComponent } from "../../ts/components/config_component";
import { FormRow } from "../../ts/components/form_row";
import { InputNumber } from "../../ts/components/input_number";
import { InputSelect } from "../../ts/components/input_select";
import { InputText } from "../../ts/components/input_text";
import { IndicatorGroup } from "../../ts/components/indicator_group";
import { CollapsedSection } from "../../ts/components/collapsed_section";
import { Switch } from "../../ts/components/switch";
import { SubPage } from "../../ts/components/sub_page";
import { NavbarItem } from "../../ts/components/navbar_item";
import { StatusSection } from "../../ts/components/status_section";
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

function get_meter_power(): number | null {
    return API.get_unchecked("meters/0/values")?.[0] ?? null;
}

function phase_indicator(value: number) {
    return <IndicatorGroup
        style="width: 100%"
        class="flex-wrap"
        value={value}
        items={phases_items().map((item) => ["primary", item[1]])}
    />;
}

interface PhaseSwitcherStatusState {
    state: PhaseSwitcherState;
}

export class PhaseSwitcherStatus extends Component<{}, PhaseSwitcherStatusState> {
    constructor() {
        super();

        this.state = {
            state: API.get("phase_switcher/state"),
        };

        util.addApiEventListener("phase_switcher/state", () => {
            this.setState({state: API.get("phase_switcher/state")});
        });
    }

    render(props: {}, state: PhaseSwitcherStatusState) {
        if (!util.render_allowed() || !API.hasModule("phase_switcher")) {
            return <StatusSection name="phase_switcher" />;
        }

        return <StatusSection name="phase_switcher">
            <FormRow label={__("phase_switcher.status.available_charging_power")}>
                <InputText value={state.state.available_charging_power + " W"} />
            </FormRow>
            <FormRow label={__("phase_switcher.status.active_phases")}>
                {phase_indicator(state.state.active_phases)}
            </FormRow>
        </StatusSection>;
    }
}

export class PhaseSwitcher extends ConfigComponent<"phase_switcher/config", {status_ref: RefObject<PhaseSwitcherStatus>}, PhaseSwitcherPageState> {
    constructor() {
        super("phase_switcher/config", () => __("phase_switcher.script.save_failed"), () => __("phase_switcher.script.reboot_content_changed"));

        this.state = {
            ...(API.get("phase_switcher/config") as PhaseSwitcherConfig),
            phase_switcher_state: API.get("phase_switcher/state"),
            low_level_state: API.get("phase_switcher/low_level_state"),
            meter_power: get_meter_power(),
            internal_isDirty: false,
        } as any;

        util.addApiEventListener("phase_switcher/state", () => this.setState({phase_switcher_state: API.get("phase_switcher/state")}));
        util.addApiEventListener("phase_switcher/low_level_state", () => this.setState({low_level_state: API.get("phase_switcher/low_level_state")}));
        util.addApiEventListener_unchecked("meters/0/values", () => this.setState({meter_power: get_meter_power()}));
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
                        value={phase_state.contactor_state ? 1 : 0}
                        items={[
                            ["success", __("phase_switcher.content.contactor_state_ok")], 
                            ["danger", __("phase_switcher.content.contactor_state_error")]
                        ]}
                    />
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
                    <div class="row gx-2 gy-1">
                        <div class="col-md-6"><InputNumber min={10} max={3600} value={state.minimum_duration} unit="s" onValue={this.set("minimum_duration")} /></div>
                        <div class="col-md-6"><InputText value={format_seconds(minimum_duration)} /></div>
                    </div>
                </FormRow>
                <FormRow label={__("phase_switcher.content.pause_time.title")} label_muted={__("phase_switcher.content.pause_time.description")}>
                    <div class="row gx-2 gy-1">
                        <div class="col-md-6"><InputNumber min={10} max={3600} value={state.pause_time} unit="s" onValue={this.set("pause_time")} /></div>
                        <div class="col-md-6"><InputText value={format_seconds(pause_time)} /></div>
                    </div>
                </FormRow>
                <CollapsedSection heading={__("phase_switcher.content.details")}>
                    <FormRow label={__("phase_switcher.content.channel_states.title")} label_muted={__("phase_switcher.content.channel_states.description")}>
                        <div class="row mx-n1">
                            <div class="mb-1 col-6 px-1">
                                {low_level_state.output_channels.map((value, index) => (
                                    <IndicatorGroup
                                        vertical
                                        key={"output" + index}
                                        class="col"
                                            value={value ? 0 : 1} //intentionally inverted: the high button is the first
                                        items={[
                                            ["primary", __("phase_switcher.content.channel_high")],
                                            ["secondary", __("phase_switcher.content.channel_low")]
                                        ]}
                                    />
                                ))}
                            </div>
                            <div class="mb-1 col-6 px-1">
                                {low_level_state.input_channels.map((value, index) => (
                                    <IndicatorGroup
                                        vertical
                                        key={"input" + index}
                                        class="col"
                                        value={value ? 0 : 1} //intentionally inverted: the high button is the first
                                        items={[
                                            ["primary", __("phase_switcher.content.channel_high")],
                                            ["secondary", __("phase_switcher.content.channel_low")]
                                        ]}
                                    />
                                ))}
                            </div>
                        </div>
                    </FormRow>
                    <FormRow label={__("phase_switcher.content.on_delay_values.title")} label_muted={__("phase_switcher.content.on_delay_values.description")}>
                        <div class="row gx-2 gy-1">
                            {[0, 1, 2].map((index) => (
                                <div class="col-md-4" key={"on_delay_" + index}>
                                    <InputText value={(low_level_state.current_on_delay_time[index] ?? 0) + " s"} />
                                </div>
                            ))}
                        </div>
                    </FormRow>
                    <FormRow label={__("phase_switcher.content.off_delay_values.title")} label_muted={__("phase_switcher.content.off_delay_values.description")}>
                        <div class="row gx-2 gy-1">
                            {[0, 1, 2].map((index) => (
                                <div class="col-md-4" key={"off_delay_" + index}>
                                    <InputText value={(low_level_state.current_off_delay_time[index] ?? 0) + " s"} />
                                </div>
                            ))}
                        </div>
                    </FormRow>
                </CollapsedSection>
            </SubPage.Config>
        </SubPage>;
    }
}

export function pre_init() {
}

export function init() {
}