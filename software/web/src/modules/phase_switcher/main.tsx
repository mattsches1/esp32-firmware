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

import { h, render, Fragment } from "preact";
import { translate_unchecked, __ } from "../../ts/translation";

import { ConfigComponent } from "../../ts/components/config_component";
import { ConfigForm } from "../../ts/components/config_form";
import { FormRow } from "../../ts/components/form_row";
import { InputNumber } from "../../ts/components/input_number";
import { InputSelect } from "src/ts/components/input_select";
import { InputText } from "../../ts/components/input_text";
import { Switch } from "../../ts/components/switch";
import { SubPage } from "src/ts/components/sub_page";
import { CollapsedSection } from "src/ts/components/collapsed_section";

type PhaseSwitcherConfig = API.getType['phase_switcher/config'];
type PhaseSwitcherState = API.getType['phase_switcher/state'];
type PhaseSwitcherLowLevelState = API.getType['phase_switcher/low_level_state'];

export class PhaseSwitcher extends ConfigComponent<'phase_switcher/config', {}, PhaseSwitcherConfig & PhaseSwitcherState & PhaseSwitcherLowLevelState> {
    constructor() {
        super('phase_switcher/config',
                __("phase_switcher.script.save_failed"),
                __("phase_switcher.script.reboot_content_changed"));

        util.addApiEventListener('phase_switcher/state', () => {
            this.setState({...API.get('phase_switcher/state')});
        });

        util.addApiEventListener('phase_switcher/low_level_state', () => {
            this.setState({...API.get('phase_switcher/state')});
        });

    }

    render(props: {}, state: Readonly<PhaseSwitcherConfig & PhaseSwitcherState & PhaseSwitcherLowLevelState>) {
        if (!util.render_allowed())
            return <></>
        
        return (
            <SubPage>
                <ConfigForm id="phase_switcher_config_form" 
                            title={__("phase_switcher.content.phase_switcher")} 
                            onSave={this.save} 
                            onReset={this.reset} 
                            onDirtyChange={(d) => this.ignore_updates = d}
                            isModified={this.isModified()}>
                    <FormRow label={__("phase_switcher.content.phase_switcher_enabled")}>
                        <Switch desc={__("phase_switcher.content.phase_switcher_enabled_desc")}
                                checked={state.enabled}
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
                                value={this.state.operating_mode}
                                onValue={(v) => {
                                    this.setState({operating_mode: Number(v)});
                                }}/>
                    </FormRow>

                    <FormRow label={__("phase_switcher.content.delay_time.title")} label_muted={__("phase_switcher.content.delay_time.description")}>
                        <div class="row mx-n1">
                            <div class="mb-1 col-4 px-1">
                                <InputNumber required
                                            className="col"
                                            min={10}
                                            max={3600}
                                            unit="s"
                                            value={state.delay_time_more_phases}
                                            onValue={this.set("delay_time_more_phases")}/>
                            </div>
                            <div class="mb-1 col-4 px-1">
                                <InputNumber required
                                            className="col"
                                            min={10}
                                            max={3600}
                                            unit="s"
                                            value={state.delay_time_less_phases}
                                            onValue={this.set("delay_time_less_phases")}/>
                            </div>
                            <div class="mb-1 col-4 px-1">
                                <InputText value={util.format_timespan(state.delay_time)}/>
                            </div>
                        </div>
                    </FormRow>
                </ConfigForm>
            </SubPage>
        );
    }
}

render(<PhaseSwitcher/>, $('#phase_switcher')[0])

export function add_event_listeners(source: API.APIEventTarget) {}

export function init() {}

export function update_sidebar_state(module_init: any) {
    $('#sidebar-phase_switcher').prop('hidden', !module_init.phase_switcher);
}

