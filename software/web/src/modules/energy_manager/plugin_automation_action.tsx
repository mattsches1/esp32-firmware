/* esp32-firmware
 * Copyright (C) 2023 Frederic Henrichs <frederic@tinkerforge.com>
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

import { h } from "preact";
import { __ } from "../../ts/translation";
import { AutomationActionID } from "../automation/automation_defs";
import { AutomationAction } from "../automation/types";
import { FormRow } from "../../ts/components/form_row";
import { InputSelect } from "../../ts/components/input_select";
import { InputFloat } from "../../ts/components/input_float";
import * as API from "../../ts/api";
import * as util from "../../ts/util";

export type EMPhaseSwitchAutomationAction = [
    AutomationActionID.EMPhaseSwitch,
    {
        phases_wanted: number;
    },
];

export type EMChargeModeSwitchAutomationAction = [
    AutomationActionID.EMChargeModeSwitch,
    {
        mode: number;
    },
];

export type EMContactorAutomationAction = [
    AutomationActionID.EMRelaySwitch,
    {
        state: boolean;
    },
];

export type EMLimitMaxCurrentAutomationAction = [
    AutomationActionID.EMLimitMaxCurrent,
    {
        current: number;
    },
];

export type EMBlockChargeAutomationAction = [
    AutomationActionID.EMBlockCharge,
    {
        slot: number;
        block: boolean;
    },
];

function get_em_phase_switch_table_children(action: EMPhaseSwitchAutomationAction) {
    return __("energy_manager.automation.automation_action_text")(action[1].phases_wanted);
}

function get_em_phase_switch_edit_children(action: EMPhaseSwitchAutomationAction, on_action: (action: AutomationAction) => void) {
    const phases: [string, string][] = [
        ['1', __("energy_manager.automation.single_phase")],
        ['3', __("energy_manager.automation.three_phase")],
    ]
    return [
        <FormRow label={__("energy_manager.automation.phases_wanted")}>
            <InputSelect
                items={phases}
                value={action[1].phases_wanted.toString()}
                onValue={(v) => {
                    on_action(util.get_updated_union(action, {phases_wanted: parseInt(v)}));
                }} />
        </FormRow>
    ];
}

function new_em_phase_switch_config(): AutomationAction {
    return [
        AutomationActionID.EMPhaseSwitch,
        {
            phases_wanted: 1,
        },
    ];
}

function get_em_charge_mode_switch_table_children(action: EMChargeModeSwitchAutomationAction) {
    return __("energy_manager.automation.charge_mode_switch_action_text")(action[1].mode, API.get("power_manager/config").default_mode);
}

function get_em_charge_mode_switch_edit_children(action: EMChargeModeSwitchAutomationAction, on_action: (action: AutomationAction) => void) {
    const modes: [string, string][] = [
        ['0', __("energy_manager.automation.fast")],
        ['1', __("energy_manager.automation.disabled")],
        ['2', __("energy_manager.automation.pv_excess")],
        ['3', __("energy_manager.automation.guaranteed_power")],
    ]

    modes.push(['4', __("energy_manager.automation.charge_mode_default") + " (" + modes[API.get("power_manager/config").default_mode][1] + ")"])

    return [
        <FormRow label={__("energy_manager.automation.charge_mode")}>
            <InputSelect
                items={modes}
                value={action[1].mode.toString()}
                onValue={(v) => {
                    on_action(util.get_updated_union(action, {mode: parseInt(v)}));
                }} />
        </FormRow>
    ]
}

function new_em_charge_mode_switch_config(): AutomationAction {
    return [
        AutomationActionID.EMChargeModeSwitch,
        {
            mode: 4,
        },
    ];
}

function get_em_contactor_table_children(action: EMContactorAutomationAction) {
    return __("energy_manager.automation.relay_action_text")(action[1].state);
}

function get_em_contactor_edit_children(action: EMContactorAutomationAction, on_action: (action: AutomationAction) => void) {
    const states: [string, string][] = [
        ['1', __("energy_manager.automation.relay_state_closed")],
        ['0', __("energy_manager.automation.relay_state_open")],
    ]

    return [
        <FormRow label={__("energy_manager.automation.relay_state")}>
            <InputSelect
                items={states}
                value={action[1].state ? '1' : '0'}
                onValue={(v) => {
                    on_action(util.get_updated_union(action, {state: v == '1'}));
                }} />
        </FormRow>
    ]
}

function new_em_contactor_config(): AutomationAction {
    return [
        AutomationActionID.EMRelaySwitch,
        {
            state: true,
        },
    ];
}

function get_em_limit_max_current_table_children(action: EMLimitMaxCurrentAutomationAction) {
    return __("energy_manager.automation.automation_limit_max_current_action_text")(action[1].current, API.get("charge_manager/config").maximum_available_current);
}

function get_em_limit_max_current_edit_children(action: EMLimitMaxCurrentAutomationAction, on_action: (action: AutomationAction) => void) {
    const items:[string, string][] = [
        ['0', __("energy_manager.automation.limit_max_current")],
        ['1', __("energy_manager.automation.reset_limit_max_current") + " (" + API.get("charge_manager/config").maximum_available_current / 1000 + "A)"]
    ]

    return [
        <FormRow label={__("energy_manager.automation.limit_mode")}>
            <InputSelect
                items={items}
                value={action[1].current === -1 ? '1' : '0'}
                onValue={(v) => {
                    on_action(util.get_updated_union(action, {current: v === '1' ? -1 : 0}));
                }} />
        </FormRow>,
        <FormRow label={__("energy_manager.automation.max_current")} hidden={action[1].current == -1}>
            <InputFloat
                value={action[1].current}
                onValue={(v) => {
                    on_action(util.get_updated_union(action, {current: v}));
                }}
                min={0}
                unit="A"
                digits={3} />
        </FormRow>
    ];
}

function new_em_limit_max_current_config(): AutomationAction {
    return [
        AutomationActionID.EMLimitMaxCurrent,
        {
            current: 0,
        },
    ];
}

function get_em_block_charge_table_children(action: EMBlockChargeAutomationAction) {
    return __("energy_manager.automation.automation_block_charge_action_text")(action[1].slot, action[1].block);
}

function get_em_block_charge_edit_children(action: EMBlockChargeAutomationAction, on_action: (action: AutomationAction) => void) {
    const items:[string, string][] = [
        ['0', __("energy_manager.automation.unblock_charge")],
        ['1', __("energy_manager.automation.block_charge")],
    ]

    const slot_items: [string, string][] = [];
    for (let i = 0; i < 4; i++) {
        slot_items.push([i.toString(), __("energy_manager.automation.slot") + ' ' + i.toString()]);
    }

    return [
        <FormRow label={__("energy_manager.automation.slot")}>
            <InputSelect
                items={slot_items}
                value={action[1].slot.toString()}
                onValue={(v) => {
                    on_action(util.get_updated_union(action, {slot: parseInt(v)}));
                }}
            />
        </FormRow>,
        <FormRow label={__("energy_manager.automation.block_mode")}>
            <InputSelect
                items={items}
                value={action[1].block ? '1' : '0'}
                onValue={(v) => {
                    on_action(util.get_updated_union(action, {block: v == '1'}));
                }} />
        </FormRow>
    ];
}

function new_em_block_charge_config(): AutomationAction {
    return [
        AutomationActionID.EMBlockCharge,
        {
            slot: 0,
            block: true,
        },
    ];
}

export function init() {
    return {
        action_components: {
            [AutomationActionID.EMPhaseSwitch]: {
                name: __("energy_manager.automation.set_phases"),
                new_config: new_em_phase_switch_config,
                clone_config: (action: AutomationAction) => [action[0], {...action[1]}] as AutomationAction,
                get_table_children: get_em_phase_switch_table_children,
                get_edit_children: get_em_phase_switch_edit_children,
            },
            [AutomationActionID.EMChargeModeSwitch]: {
                name: __("energy_manager.automation.charge_mode_switch"),
                new_config: new_em_charge_mode_switch_config,
                clone_config: (action: AutomationAction) => [action[0], {...action[1]}] as AutomationAction,
                get_table_children: get_em_charge_mode_switch_table_children,
                get_edit_children: get_em_charge_mode_switch_edit_children,
            },
            [AutomationActionID.EMRelaySwitch]: {
                name: __("energy_manager.automation.switch_relay"),
                new_config: new_em_contactor_config,
                clone_config: (action: AutomationAction) => [action[0], {...action[1]}] as AutomationAction,
                get_table_children: get_em_contactor_table_children,
                get_edit_children: get_em_contactor_edit_children,
            },
            [AutomationActionID.EMLimitMaxCurrent]: {
                name: __("energy_manager.automation.limit_max_current"),
                new_config: new_em_limit_max_current_config,
                clone_config: (action: AutomationAction) => [action[0], {...action[1]}] as AutomationAction,
                get_edit_children: get_em_limit_max_current_edit_children,
                get_table_children: get_em_limit_max_current_table_children,
            },
            [AutomationActionID.EMBlockCharge]: {
                name: __("energy_manager.automation.block_charge"),
                new_config: new_em_block_charge_config,
                clone_config: (action: AutomationAction) => [action[0], {...action[1]}] as AutomationAction,
                get_edit_children: get_em_block_charge_edit_children,
                get_table_children: get_em_block_charge_table_children,
            }
        }
    }
}
