/* esp32-firmware
 * Copyright (C) 2024 Olaf Lüke <olaf@tinkerforge.com>
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
import { h, Fragment, createRef, Component, RefObject } from "preact";
import { __ } from "../../ts/translation";
import { METERS_SLOTS } from "../../build";
import { Switch } from "../../ts/components/switch";
import { SwitchableInputNumber } from "../../ts/components/switchable_input_number";
import { ConfigComponent } from "../../ts/components/config_component";
import { ConfigForm } from "../../ts/components/config_form";
import { FormRow } from "../../ts/components/form_row";
import { FormSeparator } from "../../ts/components/form_separator";
import { InputNumber } from "../../ts/components/input_number";
import { SubPage } from "../../ts/components/sub_page";
import { NavbarItem } from "../../ts/components/navbar_item";
import { Thermometer } from "react-feather";
import { InputTime } from "../../ts/components/input_time";
import { Collapse } from "react-bootstrap";
import { InputSelect } from "../../ts/components/input_select";
import { MeterValueID    } from "../meters/meter_value_id";
import { get_noninternal_meter_slots, NoninternalMeterSelector } from "../power_manager/main";
import { UplotLoader } from "../../ts/components/uplot_loader";
import { UplotData, UplotWrapper, UplotPath } from "../../ts/components/uplot_wrapper_2nd";
import { InputText } from "../../ts/components/input_text";
import { SOLAR_FORECAST_PLANES, get_kwh_today, get_kwh_tomorrow } from  "../solar_forecast/main";
import { get_average_price_today, get_average_price_tomorrow, get_price_from_index } from "../day_ahead_prices/main";
import { StatusSection } from "../../ts/components/status_section";

export function HeatingNavbar() {
    return <NavbarItem name="heating" title={__("heating.navbar.heating")} symbol={<Thermometer />} hidden={false} />;
}

type HeatingConfig = API.getType["heating/config"];

interface HeatingState {
    heating_state: API.getType["heating/state"];
    dap_config: API.getType["day_ahead_prices/config"];
}

export class Heating extends ConfigComponent<'heating/config', {status_ref?: RefObject<HeatingStatus>}, HeatingState> {
    uplot_loader_ref        = createRef();
    uplot_wrapper_ref       = createRef();

    summer_start_day:   number;
    summer_start_month: number;
    summer_end_day:     number;
    summer_end_month:   number;

    static days(): [string, string][] {
        return [...Array(31).keys()].map((i) => [
            (i+1).toString(),
            (i+1).toString()
        ]);
    }

    static months(): [string, string][] {
        return [
            ["1",  __("heating.content.january")],
            ["2",  __("heating.content.february")],
            ["3",  __("heating.content.march")],
            ["4",  __("heating.content.april")],
            ["5",  __("heating.content.may")],
            ["6",  __("heating.content.june")],
            ["7",  __("heating.content.july")],
            ["8",  __("heating.content.august")],
            ["9",  __("heating.content.september")],
            ["10", __("heating.content.october")],
            ["11", __("heating.content.november")],
            ["12", __("heating.content.december")]
        ];
    }

    constructor() {
        super('heating/config',
              __("heating.script.save_failed"));

        util.addApiEventListener("heating/state", () => {
            this.setState({heating_state: API.get("heating/state")});
        });

        util.addApiEventListener("day_ahead_prices/prices", () => {
            // Update chart every time new price data comes in
            this.update_uplot();
        });
    }

    get_price_timeframe() {
        const dap_prices = API.get("day_ahead_prices/prices");

        let time = new Date();
        let s = ""
        if(dap_prices.resolution == 0) {
            time.setMilliseconds(Math.floor(time.getMilliseconds() / 1000) * 1000);
            time.setSeconds(Math.floor(time.getSeconds() / 60) * 60);
            time.setMinutes(Math.floor(time.getMinutes() / 15) * 15);
            s += time.toLocaleTimeString() + '-';
            time.setMinutes(time.getMinutes() + 15);
            s += time.toLocaleTimeString()
        } else {
            time.setMilliseconds(Math.floor(time.getMilliseconds() / 1000) * 1000);
            time.setSeconds(Math.floor(time.getSeconds() / 60) * 60);
            time.setMinutes(Math.floor(time.getMinutes() / 60) * 60);
            s += time.toLocaleTimeString() + '-';
            time.setMinutes(time.getMinutes() + 60);
            s += time.toLocaleTimeString()
        }

        return s
    }

    update_uplot() {
        if (this.uplot_wrapper_ref.current == null) {
            return;
        }

        const dap_prices = API.get("day_ahead_prices/prices");
        const dap_config = API.get("day_ahead_prices/config");
        let data: UplotData;

        // If we have not got any prices yet, use empty data
        if (dap_prices.prices.length == 0) {
            data = {
                keys: [null],
                names: [null],
                values: [null],
                stacked: [null],
                paths: [null],
            }
        // Else fill with time and the three different prices we want to show
        } else {
            data = {
                keys: [null, 'price'],
                names: [null, __("day_ahead_prices.content.electricity_price")],
                values: [[], [], [], []],
                stacked: [null, true],
                paths: [null, UplotPath.Step],
                // Only enable the electricity price by default.
                // The chart with only electricity price is the most useful in most cases.
                default_visibilty: [null, true],
                lines_vertical: []
            }
            const resolution_multiplier = dap_prices.resolution == 0 ? 15 : 60
            const grid_costs_and_taxes_and_supplier_markup = dap_config.grid_costs_and_taxes / 1000.0 + dap_config.supplier_markup / 1000.0;
            for (let i = 0; i < dap_prices.prices.length; i++) {
                data.values[0].push(dap_prices.first_date * 60 + i * 60 * resolution_multiplier);
                data.values[1].push(get_price_from_index(i) / 1000.0 + grid_costs_and_taxes_and_supplier_markup);
            }

            data.values[0].push(dap_prices.first_date * 60 + dap_prices.prices.length * 60 * resolution_multiplier - 1);
            data.values[1].push(get_price_from_index(dap_prices.prices.length - 1) / 1000.0 + grid_costs_and_taxes_and_supplier_markup);

            const solar_forecast_today     = get_kwh_today();
            const solar_forecast_tomorrow  = get_kwh_tomorrow();
            const solar_forecast_threshold = this.state.summer_yield_forecast_threshold;
            const current_month = new Date().getMonth();
            const current_day   = new Date().getDate();
            const is_summer = ((current_month == this.state.summer_start_month-1) && (current_day   >= this.state.summer_start_day  )) ||
                              ((current_month == this.state.summer_end_month-1  ) && (current_day   <= this.state.summer_end_day    )) ||
                              ((current_month >  this.state.summer_start_month-1) && (current_month <  this.state.summer_end_month-1));

            const num_per_day   = 24*60/resolution_multiplier;
            const active_active = this.state.summer_active_time_active;
            const active_start  = this.state.summer_active_time_start/resolution_multiplier;
            const active_end    = this.state.summer_active_time_end/resolution_multiplier;

            const active_today    = active_active && (!this.state.summer_yield_forecast_active || (is_summer && (solar_forecast_today >= solar_forecast_threshold)));
            const active_tomorrow = active_active && (!this.state.summer_yield_forecast_active || (is_summer && (solar_forecast_tomorrow >= solar_forecast_threshold)));

            if (dap_prices.prices.length >= num_per_day) {
                const avg_price_day1 = dap_prices.prices.slice(0, num_per_day).reduce((a, b) => a + b, 0) / num_per_day;
                for (let i = 0; i < num_per_day; i++) {
                    if (((i < active_start) || (i >= active_end)) && active_today) {
                        //data.lines_vertical.push({'index': i, 'text': '', 'color': [196, 196, 196, 0.5]});
                    } else if (dap_prices.prices[i] < avg_price_day1*this.state.dpc_extended_threshold/100) {
                        if (this.state.dpc_extended_active) {
                            data.lines_vertical.push({'index': i, 'text': '', 'color': [40, 167, 69, 0.5]});
                        }
                    } else if(dap_prices.prices[i] > avg_price_day1*this.state.dpc_blocking_threshold/100) {
                        if (this.state.dpc_blocking_active) {
                            data.lines_vertical.push({'index': i, 'text': '', 'color': [220, 53, 69, 0.5]});
                        }
                    }
                }
            }
            if (dap_prices.prices.length >= num_per_day*2) {
                const avg_price_day2 = dap_prices.prices.slice(num_per_day).reduce((a, b) => a + b, 0) / num_per_day;
                for (let i = num_per_day; i < num_per_day*2; i++) {
                    if ((((i-num_per_day) < active_start) || ((i-num_per_day) >= active_end)) && active_tomorrow) {
                        //data.lines_vertical.push({'index': i, 'text': '', 'color': [196, 196, 196, 0.5]});
                    } else if (dap_prices.prices[i] < avg_price_day2*this.state.dpc_extended_threshold/100) {
                        if (this.state.dpc_extended_active) {
                            data.lines_vertical.push({'index': i, 'text': '', 'color': [40, 167, 69, 0.5]});
                        }
                    } else if(dap_prices.prices[i] > avg_price_day2*this.state.dpc_blocking_threshold/100) {
                        if (this.state.dpc_blocking_active) {
                            data.lines_vertical.push({'index': i, 'text': '', 'color': [220, 53, 69, 0.5]});
                        }
                    }
                }
            }

            // Add vertical line at current time
            const resolution_divisor = dap_prices.resolution == 0 ? 15 : 60;
            const diff = Math.floor(Date.now() / 60000) - dap_prices.first_date;
            const index = Math.floor(diff / resolution_divisor);
            data.lines_vertical.push({'index': index, 'text': __("day_ahead_prices.content.now"), 'color': [64, 64, 64, 0.2]});
        }

        // Show loader or data depending on the availability of data
        this.uplot_loader_ref.current.set_data(data && data.keys.length > 1);
        this.uplot_wrapper_ref.current.set_data(data);
    }

    get_date_from_minutes(minutes: number) {
        const h = Math.floor(minutes / 60);
        const m = minutes - h * 60;
        return new Date(0, 0, 1, h, m);
    }

    get_minutes_from_date(date: Date) {
        return date.getMinutes() + date.getHours()*60;
    }

    month_to_days(month: number): [string, string][] {
        switch(month) {
            case 1: case 3: case 5: case 7: case 8: case 10: case 12: return Heating.days().slice(0, 31);
            case 4: case 6: case 9: case 11:                          return Heating.days().slice(0, 30);
            case 2:                                                   return Heating.days().slice(0, 28);
            default: console.log("Invalid month: " + month);
        }
        return Heating.days().slice(0, 31);
    }

    render(props: {}, state: HeatingState & HeatingConfig) {
        if (!util.render_allowed())
            return <SubPage name="heating" />;

        let days_summer_start = this.month_to_days(state.summer_start_month);
        let days_summer_end = this.month_to_days(state.summer_end_month);

        const meter_slots = get_noninternal_meter_slots([MeterValueID.PowerActiveLSumImExDiff], NoninternalMeterSelector.AllValues, __("power_manager.content.meter_slot_grid_power_missing_value"));

        return (
            <SubPage name="heating">
                <ConfigForm id="heating_config_form"
                            title={__("heating.content.heating")}
                            isModified={false}
                            isDirty={this.isDirty()}
                            onSave={this.save}
                            onReset={this.reset}
                            onDirtyChange={this.setDirty}>
                    <FormRow label={__("heating.content.meter_slot_grid_power")} label_muted={__("heating.content.meter_slot_grid_power_muted")}>
                        <InputSelect
                            placeholder={meter_slots.length > 0 ? __("heating.content.meter_slot_grid_power_select") : __("heating.content.meter_slot_grid_power_none")}
                            items={meter_slots}
                            value={state.meter_slot_grid_power}
                            onValue={(v) => this.setState({meter_slot_grid_power: parseInt(v)})}
                        />
                    </FormRow>
                    <FormRow label={__("heating.content.minimum_holding_time")} label_muted={__("heating.content.minimum_holding_time_muted")}>
                        <InputNumber
                            unit={__("heating.content.minutes")}
                            value={state.minimum_control_holding_time}
                            onValue={this.set("minimum_control_holding_time")}
                            min={0}
                            max={60}
                        />
                    </FormRow>
                    <FormRow label={__("heating.content.sg_ready_output") + " 1"} label_muted={__("heating.content.sg_ready_output1_muted")}>
                        <InputSelect
                            items={[
                                ["0", __("heating.content.closed")],
                                ["1", __("heating.content.opened")]
                            ]}
                            value={state.sg_ready_blocking_active_type}
                            onValue={(v) => this.setState({sg_ready_blocking_active_type: parseInt(v)})}
                        />
                    </FormRow>
                    <FormRow label={__("heating.content.sg_ready_output") + " 2"} label_muted={__("heating.content.sg_ready_output2_muted")}>
                        <InputSelect
                            items={[
                                ["0", __("heating.content.closed")],
                                ["1", __("heating.content.opened")]
                            ]}
                            value={state.sg_ready_extended_active_type}
                            onValue={(v) => this.setState({sg_ready_extended_active_type: parseInt(v)})}
                        />
                    </FormRow>
                    <FormRow label={__("heating.content.extended_logging")} label_muted={__("heating.content.extended_logging_description")}>
                        <Switch desc={__("heating.content.extended_logging_activate")}
                                checked={state.extended_logging_active}
                                onClick={this.toggle('extended_logging_active')}
                        />
                    </FormRow>

                    <FormSeparator heading={__("heating.content.summer_settings")}/>
                    <FormRow label={__("heating.content.summer_start")} label_muted="">
                        <div class="row no-gutters">
                            <div class="col-md-6">
                                <div class="input-group">
                                    <div class="input-group-prepend heating-input-group-prepend"><span class="heating-fixed-size input-group-text">{__("heating.content.month")}</span></div>
                                    <InputSelect
                                        className="heating-input-group-prepend"
                                        items={Heating.months()}
                                        value={state.summer_start_month}
                                        onValue={(v) => {
                                            this.setState({summer_start_month: parseInt(v)});
                                            days_summer_start = this.month_to_days(parseInt(v));
                                        }}
                                    />
                                </div>
                            </div>
                            <div class="col-md-6">
                                <div class="input-group">
                                    <div class="input-group-prepend heating-input-group-append"><span class="heating-fixed-size input-group-text">{__("heating.content.day")}</span></div>
                                    <InputSelect
                                        className="heating-input-group-prepend"
                                        items={days_summer_start}
                                        value={state.summer_start_day}
                                        onValue={(v) => {
                                            this.setState({summer_start_day: parseInt(v)})
                                        }}
                                    />
                                </div>
                            </div>
                        </div>
                    </FormRow>
                    <FormRow label={__("heating.content.summer_end")} label_muted="">
                        <div class="row no-gutters">
                            <div class="col-md-6">
                                <div class="input-group">
                                    <div class="input-group-prepend heating-input-group-prepand"><span class="heating-fixed-size input-group-text">{__("heating.content.month")}</span></div>
                                    <InputSelect
                                        className="heating-input-group-prepend"
                                        items={Heating.months()}
                                        value={state.summer_end_month}
                                        onValue={(v) => {
                                            this.setState({summer_end_month: parseInt(v)});
                                            days_summer_end = this.month_to_days(parseInt(v));
                                        }}
                                    />
                                </div>
                            </div>
                            <div class="col-md-6">
                                <div class="input-group">
                                    <div class="input-group-prepend heating-input-group-append"><span class="heating-fixed-size input-group-text">{__("heating.content.day")}</span></div>
                                    <InputSelect
                                        className="heating-input-group-prepend"
                                        items={days_summer_end}
                                        value={state.summer_end_day}
                                        onValue={(v) => {
                                            this.setState({summer_end_day: parseInt(v)})
                                        }}
                                    />
                                </div>
                            </div>
                        </div>
                    </FormRow>
                    <FormRow label={__("heating.content.active_time")} help={__("heating.content.active_time_help")}>
                        <Switch desc={__("heating.content.active_time_desc")}
                                checked={state.summer_active_time_active}
                                onClick={this.toggle('summer_active_time_active', this.update_uplot)}
                        />
                        <div class="row no-gutters">
                            <div class="col-md-6">
                                <div class="input-group">
                                    <div class="input-group-prepend heating-input-group-prepend"><span class="heating-fixed-size input-group-text">{__("heating.content.from")}</span></div>
                                    <InputTime
                                        className={"form-control-md heating-input-group-prepend"}
                                        date={this.get_date_from_minutes(state.summer_active_time_start)}
                                        showSeconds={false}
                                        onDate={(d: Date) => this.setState({summer_active_time_start: this.get_minutes_from_date(d)}, this.update_uplot)}
                                    />
                                </div>
                            </div>
                            <div class="col-md-6">
                                <div class="input-group">
                                    <div class="input-group-prepend heating-input-group-append"><span class="heating-fixed-size input-group-text">{__("heating.content.to")}</span></div>
                                    <InputTime
                                        className={"form-control-md heating-input-group-prepend"}
                                        date={this.get_date_from_minutes(state.summer_active_time_end)}
                                        showSeconds={false}
                                        onDate={(d: Date) => this.setState({summer_active_time_end: this.get_minutes_from_date(d)}, this.update_uplot)}
                                    />
                                </div>
                            </div>
                        </div>
                    </FormRow>
                    <FormRow label={__("heating.content.pv_yield_forecast")} label_muted={__("heating.content.pv_yield_forecast_muted")} help={__("heating.content.pv_yield_forecast_help")}>
                        <SwitchableInputNumber
                            switch_label_active="Aktiv"
                            switch_label_inactive="Inaktiv"
                            unit="kWh"
                            checked={state.summer_yield_forecast_active}
                            onClick={this.toggle('summer_yield_forecast_active', this.update_uplot)}
                            value={state.summer_yield_forecast_threshold}
                            onValue={this.set("summer_yield_forecast_threshold", this.update_uplot)}
                            min={0}
                            max={1000}
                            switch_label_min_width="100px"
                        />
                    </FormRow>

                    <FormSeparator heading={__("heating.content.general_settings")}/>
                    <FormRow label={__("heating.content.pv_excess_control")} help={__("heating.content.pv_excess_control_help")}>
                        <SwitchableInputNumber
                            switch_label_active={__("heating.content.active")}
                            switch_label_inactive={__("heating.content.inactive")}
                            unit={__("heating.content.watt")}
                            checked={state.pv_excess_control_active}
                            onClick={this.toggle('pv_excess_control_active')}
                            value={state.pv_excess_control_threshold}
                            onValue={this.set("pv_excess_control_threshold")}
                            min={0}
                            max={100000}
                            switch_label_min_width="100px"
                        />
                    </FormRow>
                    <FormRow label={__("heating.content.dpc_low")} label_muted={__("heating.content.dpc_low_muted")} help={__("heating.content.dpc_extended_help")}>
                        <SwitchableInputNumber
                            switch_label_active={__("heating.content.active")}
                            switch_label_inactive={__("heating.content.inactive")}
                            unit="%"
                            checked={state.dpc_extended_active}
                            onClick={this.toggle('dpc_extended_active', this.update_uplot)}
                            value={state.dpc_extended_threshold}
                            onValue={(v) => {this.setState({dpc_extended_threshold: v}, this.update_uplot)}}
                            min={0}
                            max={100}
                            switch_label_min_width="100px"
                        />
                    </FormRow>
                    <FormRow label={__("heating.content.dpc_high")} label_muted={__("heating.content.dpc_high_muted")} help={__("heating.content.dpc_blocking_help")}>
                        <SwitchableInputNumber
                            switch_label_active={__("heating.content.active")}
                            switch_label_inactive={__("heating.content.inactive")}
                            unit="%"
                            checked={state.dpc_blocking_active}
                            onClick={this.toggle('dpc_blocking_active', this.update_uplot)}
                            value={state.dpc_blocking_threshold}
                            onValue={(v) => {this.setState({dpc_blocking_threshold: v}, this.update_uplot)}}
                            min={100}
                            max={1000}
                            switch_label_min_width="100px"
                        />
                    </FormRow>

                    <FormSeparator heading={__("heating.content.status")} />
                    <FormRow label={__("heating.content.price_based_heating_plan")} label_muted={__("heating.content.price_based_heating_plan_muted")}>
                    <div class="card pl-1 pb-1">
                        <div style="position: relative;"> {/* this plain div is neccessary to make the size calculation stable in safari. without this div the height continues to grow */}
                            <UplotLoader
                                ref={this.uplot_loader_ref}
                                show={true}
                                marker_class={'h4'}
                                no_data={__("day_ahead_prices.content.no_data")}
                                loading={__("day_ahead_prices.content.loading")}>
                                <UplotWrapper
                                    ref={this.uplot_wrapper_ref}
                                    class="heating-chart pt-3"
                                    sub_page="heating"
                                    color_cache_group="heating.default"
                                    show={true}
                                    on_mount={() => this.update_uplot()}
                                    legend_time_label={__("day_ahead_prices.content.time")}
                                    legend_time_with_minutes={true}
                                    aspect_ratio={3}
                                    x_height={50}
                                    x_format={{hour: '2-digit', minute: '2-digit'}}
                                    x_padding_factor={0}
                                    x_include_date={true}
                                    y_unit={"ct/kWh"}
                                    y_label={__("day_ahead_prices.content.price_ct_per_kwh")}
                                    y_digits={3}
                                    only_show_visible={true}
                                    padding={[20, 20, null, 5]}
                                />
                            </UplotLoader>
                        </div>
                    </div>
                    </FormRow>
                    <FormRow label={__("heating.content.average_price")}>
                        <div class="row mx-n1">
                            <div class="col-md-6 px-1">
                                <div class="input-group">
                                    <div class="input-group-prepend"><span class="heating-fixed-size input-group-text">{__("today")}</span></div>
                                    <InputText
                                        value={util.get_value_with_unit(get_average_price_today(), "ct/kWh", 2, 1000)}
                                    />
                                </div>
                            </div>
                            <div class="col-md-6 px-1">
                                <div class="input-group">
                                    <div class="input-group-prepend"><span class="heating-fixed-size input-group-text">{__("tomorrow")}</span></div>
                                    <InputText
                                        value={util.get_value_with_unit(get_average_price_tomorrow(), "ct/kWh", 2, 1000)}
                                    />
                                </div>
                            </div>
                        </div>
                    </FormRow>
                    <FormRow label={__("heating.content.solar_forecast")}>
                        <div class="row mx-n1">
                            <div class="col-md-6 px-1">
                                <div class="input-group">
                                    <div class="input-group-prepend"><span class="heating-fixed-size input-group-text">{__("today")}</span></div>
                                    <InputText
                                        value={util.get_value_with_unit(get_kwh_today(), "kWh", 2)}
                                    />
                                </div>
                            </div>
                            <div class="col-md-6 px-1">
                                <div class="input-group">
                                    <div class="input-group-prepend"><span class="heating-fixed-size input-group-text">{__("tomorrow")}</span></div>
                                    <InputText
                                        value={util.get_value_with_unit(get_kwh_tomorrow(), "kWh", 2)}
                                    />
                                </div>
                            </div>
                        </div>
                    </FormRow>
                    <FormRow label="SG Ready">
                        <div class="row mx-n1">
                            <div class="col-md-6 px-1">
                                <div class="input-group">
                                    <div class="input-group-prepend"><span class="heating-fixed-size input-group-text">{__("heating.content.output") + " 1"}</span></div>
                                    <InputText
                                        value={state.heating_state.sg_ready_blocking_active ? __("heating.content.active") : __("heating.content.inactive")}
                                    />
                                </div>
                            </div>
                            <div class="col-md-6 px-1">
                                <div class="input-group">
                                    <div class="input-group-prepend"><span class="heating-fixed-size input-group-text">{__("heating.content.output") + " 2"}</span></div>
                                    <InputText
                                        value={state.heating_state.sg_ready_extended_active ? __("heating.content.active") : __("heating.content.inactive")}
                                    />
                                </div>
                            </div>
                        </div>
                    </FormRow>

                    <FormSeparator heading="§14 EnWG"/>
                    <FormRow label="§14 EnWG">
                        <Switch desc={__("heating.content.p14_enwg_control_activate")}
                                checked={state.p14enwg_active}
                                onClick={this.toggle('p14enwg_active')}
                        />
                    </FormRow>
                    <FormRow label={__("heating.content.input")}>
                        <InputSelect
                            items={[
                                ["0", __("heating.content.input") + " 1"],
                                ["1", __("heating.content.input") + " 2"],
                                ["2", __("heating.content.input") + " 3"],
                                ["3", __("heating.content.input") + " 4"],
                            ]}
                            value={state.p14enwg_input}
                            onValue={(v) => this.setState({p14enwg_input: parseInt(v)})}
                        />
                    </FormRow>
                    <FormRow label={__("heating.content.throttled_if_input")}>
                        <InputSelect
                            items={[
                                ["0", __("heating.content.closed")],
                                ["1", __("heating.content.opened")]
                            ]}
                            value={state.p14enwg_active_type}
                            onValue={(v) => this.setState({p14enwg_active_type: parseInt(v)})}
                        />
                    </FormRow>
                </ConfigForm>
            </SubPage>
        );
    }
}

export class HeatingStatus extends Component
{
    render() {
        const config = API.get('heating/config')
        if (!util.render_allowed() || (!config.summer_active_time_active && !config.summer_yield_forecast_active && !config.dpc_extended_active && !config.pv_excess_control_active))
            return <StatusSection name="heating" />

        const state = API.get('heating/state')

        return <StatusSection name="heating">
            <FormRow label="SG Ready" label_muted={__("heating.content.sg_ready_muted")}>
                <div class="row mx-n1">
                    <div class="col-md-6 px-1">
                        <div class="input-group">
                            <div class="input-group-prepend"><span class="heating-fixed-size input-group-text">{__("heating.content.blocked")}</span></div>
                            <InputText
                                value={state.sg_ready_blocking_active ? __("heating.content.active") : __("heating.content.inactive")}
                            />
                        </div>
                    </div>
                    <div class="col-md-6 px-1">
                        <div class="input-group">
                            <div class="input-group-prepend"><span class="heating-fixed-size input-group-text">{__("heating.content.extended")}</span></div>
                            <InputText
                                value={state.sg_ready_extended_active ? __("heating.content.active") : __("heating.content.inactive")}
                            />
                        </div>
                    </div>
                </div>
            </FormRow>
        </StatusSection>;
    }
}

export function init() {
}
