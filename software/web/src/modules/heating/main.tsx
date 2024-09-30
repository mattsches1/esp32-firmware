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
import { h, Fragment, createRef } from "preact";
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
import { get_noninternal_meter_slots } from "../power_manager/main";
import { UplotLoader } from "../../ts/components/uplot_loader";
import { UplotData, UplotWrapper, UplotPath } from "../../ts/components/uplot_wrapper_2nd";

export function HeatingNavbar() {
    return <NavbarItem name="heating" title={__("heating.navbar.heating")} symbol={<Thermometer />} hidden={false} />;
}

type HeatingConfig = API.getType["heating/config"];

interface HeatingState {
    dap_config: API.getType["day_ahead_prices/config"];
    dap_state:  API.getType["day_ahead_prices/state"];
    dap_prices: API.getType["day_ahead_prices/prices"];
}

export class Heating extends ConfigComponent<'heating/config', {}, HeatingState> {
    uplot_loader_ref        = createRef();
    uplot_wrapper_ref       = createRef();
    uplot_legend_div_ref    = createRef();
    uplot_wrapper_flags_ref = createRef();

    summer_start_day:   number;
    summer_start_month: number;
    summer_end_day:     number;
    summer_end_month:   number;

    static days: [string, string][] = [...Array(31).keys()].map((i) => [
        (i+1).toString(),
        (i+1).toString()
    ]);

    static months: [string, string][] = [
        ["1", "Januar"],
        ["2", "Februar"],
        ["3", "März"],
        ["4", "April"],
        ["5", "Mai"],
        ["6", "Juni"],
        ["7", "Juli"],
        ["8", "August"],
        ["9", "September"],
        ["10", "Oktober"],
        ["11", "November"],
        ["12", "Dezember"]
    ];

    constructor() {
        super('heating/config',
              __("heating.script.save_failed"));

        util.addApiEventListener("day_ahead_prices/config", () => {
            this.setState({dap_config: API.get("day_ahead_prices/config")});
        });

        util.addApiEventListener("day_ahead_prices/state", () => {
            this.setState({dap_state: API.get("day_ahead_prices/state")});
        });

        util.addApiEventListener("day_ahead_prices/prices", () => {
            this.setState({dap_prices: API.get("day_ahead_prices/prices")});
            // Update chart every time new price data comes in
            this.update_uplot();
        });
    }

    get_price_timeframe() {
        let time = new Date();
        let s = ""
        if(this.state.dap_prices.resolution == 0) {
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

        let data: UplotData;

        // If we have not got any prices yet, use empty data
        if (this.state.dap_prices.prices.length == 0) {
            data = {
                keys: [],
                names: [],
                values: [],
                stacked: [],
                paths: [],
            }
        // Else fill with time and the three different prices we want to show
        } else {
            data = {
                keys: ['time', 'price'],
                names: [__("day_ahead_prices.content.time"), __("day_ahead_prices.content.electricity_price")],
                values: [[], [], [], []],
                stacked: [null, true],
                paths: [null, UplotPath.Step],
                // Only enable the electricity price by default.
                // The chart with only electricity price is the most useful in most cases.
                default_visibilty: [null, true],
                lines_vertical: []
            }
            let resolution_multiplier = this.state.dap_prices.resolution == 0 ? 15 : 60
            const grid_costs_and_taxes_and_supplier_markup = this.state.dap_config.grid_costs_and_taxes/1000.0 + this.state.dap_config.supplier_markup/1000.0;
            for (let i = 0; i < this.state.dap_prices.prices.length; i++) {
                data.values[0].push(this.state.dap_prices.first_date*60 + i*60*resolution_multiplier);
                data.values[1].push(this.state.dap_prices.prices[i]/1000.0 + grid_costs_and_taxes_and_supplier_markup);
            }

            const num_per_day = 24*60/resolution_multiplier;
            console.log(num_per_day)

            if (this.state.dap_prices.prices.length >= num_per_day*2) {
                let avg_price_day1 = this.state.dap_prices.prices.slice(0, num_per_day).reduce((a, b) => a + b, 0) / num_per_day;
                let avg_price_day2 = this.state.dap_prices.prices.slice(num_per_day).reduce((a, b) => a + b, 0) / num_per_day;

                for (let i = 0; i < num_per_day; i++) {
                    console.log(this.state.dap_prices.prices[i], this.state.dpc_extended_threshold, avg_price_day1);
                    if (this.state.dap_prices.prices[i] < avg_price_day1*this.state.dpc_extended_threshold/100) {
                        if (this.state.dpc_extended_active) {
                            data.lines_vertical.push({'index': i, 'text': '', 'color': [0, 255, 0, 0.5]});
                        }
                    } else if(this.state.dap_prices.prices[i] > avg_price_day1*this.state.dpc_blocking_threshold/100) {
                        if (this.state.dpc_blocking_active) {
                            data.lines_vertical.push({'index': i, 'text': '', 'color': [255, 0, 0, 0.5]});
                        }
                    }
                }
                for (let i = num_per_day; i < num_per_day*2; i++) {
                    if (this.state.dap_prices.prices[i] < avg_price_day2*this.state.dpc_extended_threshold/100) {
                        if (this.state.dpc_extended_active) {
                            data.lines_vertical.push({'index': i, 'text': '', 'color': [0, 255, 0, 0.5]});
                        }
                    } else if(this.state.dap_prices.prices[i] > avg_price_day2*this.state.dpc_blocking_threshold/100) {
                        if (this.state.dpc_blocking_active) {
                            data.lines_vertical.push({'index': i, 'text': '', 'color': [255, 0, 0, 0.5]});
                        }
                    }
                }
            }

            // Add vertical line at current time
            const resolution_divisor = this.state.dap_prices.resolution == 0 ? 15 : 60;
            const diff = Math.floor(Date.now() / 60000) - this.state.dap_prices.first_date;
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
            case 1: case 3: case 5: case 7: case 8: case 10: case 12: return Heating.days.slice(0, 31);
            case 4: case 6: case 9: case 11:                          return Heating.days.slice(0, 30);
            case 2:                                                   return Heating.days.slice(0, 28);
            default: console.log("Invalid month: " + month);
        }
        return Heating.days.slice(0, 31);
    }

    recalculateSummer(state: Readonly<HeatingConfig>) {
        this.summer_start_day = state.winter_end_day + 1;
        this.summer_start_month = state.winter_end_month;
        if(this.summer_start_day > this.month_to_days(state.winter_end_month).length) {
            this.summer_start_day = 1;
            this.summer_start_month = state.winter_end_month + 1 > 12 ? 1 : state.winter_end_month + 1;
        }

        this.summer_end_day = state.winter_start_day - 1;
        this.summer_end_month = state.winter_start_month;
        if(this.summer_end_day < 1) {
            this.summer_end_month = state.winter_start_month - 1 < 1 ? 12 : state.winter_start_month - 1;
            this.summer_end_day = this.month_to_days(this.summer_end_month).length;
        }
    }

    render(props: {}, state: Readonly<HeatingConfig>) {
        if (!util.render_allowed())
            return <SubPage name="heating" />;

        let days_winter_start = this.month_to_days(state.winter_start_month);
        let days_winter_end = this.month_to_days(state.winter_end_month);
        let days_summer_start = this.month_to_days(1);
        let days_summer_end = this.month_to_days(1);

        this.recalculateSummer(state);

        const meter_slots = get_noninternal_meter_slots([MeterValueID.PowerActiveLSumImExDiff], __("power_manager.content.meter_slot_grid_power_missing_value"));

        return (
            <SubPage name="heating">
                <ConfigForm id="heating_config_form"
                            title={__("heating.content.heating")}
                            isModified={this.isModified()}
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
                    <FormRow label={__("heating.content.minimum_holding_time")} label_muted="Für SG-Ready-Ausgang 1 und SG-Ready-Ausgang 2">
                        <InputNumber
                            unit={__("heating.content.minutes")}
                            value={state.minimum_control_holding_time}
                            onValue={this.set("minimum_control_holding_time")}
                            min={0}
                            max={60}
                        />
                    </FormRow>
                    <FormRow label="SG-Ready-Ausgang 1 aktiv wenn" label_muted="Ausgang 1 wird für den blockierenden Betrieb verwendet (SG Ready Zustand 1).">
                        <InputSelect
                            items={[
                                ["0", __("heating.content.closed")],
                                ["1", __("heating.content.opened")]
                            ]}
                            value={state.sg_ready_blocking_active_type}
                            onValue={(v) => this.setState({sg_ready_blocking_active_type: parseInt(v)})}
                        />
                    </FormRow>
                    <FormRow label="SG-Ready-Ausgang 2 aktiv wenn" label_muted="Ausgang 2 wird für die Einschaltempfehlung verwendet (SG Ready Zustand 3).">
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
                    <FormSeparator heading="Winter-Einstellungen"/>

                    <FormRow label={__("heating.content.winter_start")} label_muted="">
                        <div class="row no-gutters">
                            <div class="col-md-6">
                                <div class="input-group">
                                    <div class="input-group-prepend heating-input-group-prepend"><span class="heating-fixed-size input-group-text">{__("heating.content.month")}</span></div>
                                    <InputSelect
                                        className="heating-input-group-prepend"
                                        items={Heating.months}
                                        value={state.winter_start_month}
                                        onValue={(v) => {
                                            this.setState({winter_start_month: parseInt(v)});
                                            days_winter_start = this.month_to_days(parseInt(v));
                                            this.recalculateSummer(state);
                                        }}
                                    />
                                </div>
                            </div>
                            <div class="col-md-6">
                                <div class="input-group">
                                    <div class="input-group-prepend heating-input-group-append"><span class="heating-fixed-size input-group-text">{__("heating.content.day")}</span></div>
                                    <InputSelect
                                        className="heating-input-group-prepend"
                                        items={days_winter_start}
                                        value={state.winter_start_day}
                                        onValue={(v) => {
                                            this.setState({winter_start_day: parseInt(v)})
                                            this.recalculateSummer(state);
                                        }}
                                    />
                                </div>
                            </div>
                        </div>
                    </FormRow>
                    <FormRow label={__("heating.content.winter_end")} label_muted="">
                        <div class="row no-gutters">
                            <div class="col-md-6">
                                <div class="input-group">
                                    <div class="input-group-prepend heating-input-group-prepand"><span class="heating-fixed-size input-group-text">{__("heating.content.month")}</span></div>
                                    <InputSelect
                                        className="heating-input-group-prepend"
                                        items={Heating.months}
                                        value={state.winter_end_month}
                                        onValue={(v) => {
                                            this.setState({winter_end_month: parseInt(v)});
                                            days_winter_end = this.month_to_days(parseInt(v));
                                            this.recalculateSummer(state);
                                        }}
                                    />
                                </div>
                            </div>
                            <div class="col-md-6">
                                <div class="input-group">
                                    <div class="input-group-prepend heating-input-group-append"><span class="heating-fixed-size input-group-text">{__("heating.content.day")}</span></div>
                                    <InputSelect
                                        className="heating-input-group-prepend"
                                        items={days_winter_end}
                                        value={state.winter_end_day}
                                        onValue={(v) => {
                                            this.setState({winter_end_day: parseInt(v)})
                                            this.recalculateSummer(state);
                                        }}
                                    />
                                </div>
                            </div>
                        </div>
                    </FormRow>

                    <FormSeparator heading="Sommer-Einstellungen"/>
                    <FormRow label="Sommer Start" label_muted="Zeitraum anhand des Wintermodus berechnet">
                        <div class="row no-gutters">
                            <div class="col-md-6">
                                <div class="input-group">
                                    <div class="input-group-prepend heating-input-group-prepand"><span class="heating-fixed-size input-group-text">{__("heating.content.month")}</span></div>
                                    <InputSelect
                                        className="heating-input-group-prepend"
                                        items={Heating.months}
                                        value={this.summer_start_month}
                                    />
                                </div>
                            </div>
                            <div class="col-md-6">
                                <div class="input-group">
                                    <div class="input-group-prepend heating-input-group-append"><span class="heating-fixed-size input-group-text">{__("heating.content.day")}</span></div>
                                    <InputSelect
                                        className="heating-input-group-prepend"
                                        items={days_summer_start}
                                        value={this.summer_start_day}
                                    />
                                </div>
                            </div>
                        </div>
                    </FormRow>
                    <FormRow label="Sommer Ende" label_muted="Zeitraum anhand des Wintermodus berechnet">
                        <div class="row no-gutters">
                            <div class="col-md-6">
                                <div class="input-group">
                                    <div class="input-group-prepend heating-input-group-prepend"><span class="heating-fixed-size input-group-text">{__("heating.content.month")}</span></div>
                                    <InputSelect
                                        className="heating-input-group-prepend"
                                        items={Heating.months}
                                        value={this.summer_end_month}
                                    />
                                </div>
                            </div>
                            <div class="col-md-6">
                                <div class="input-group">
                                    <div class="input-group-prepend heating-input-group-append"><span class="heating-fixed-size input-group-text">{__("heating.content.day")}</span></div>
                                    <InputSelect
                                        className="heating-input-group-prepend"
                                        items={days_summer_end}
                                        value={this.summer_end_day}
                                    />
                                </div>
                            </div>
                        </div>
                    </FormRow>
                    <FormRow label={__("heating.content.block_time")} help={__("heating.content.block_time_help")}>
                        <Switch desc={__("heating.content.enable_daily_block_period")}
                                checked={state.summer_block_time_active}
                                onClick={this.toggle('summer_block_time_active')}
                        />
                    </FormRow>
                    <Collapse in={state.summer_block_time_active}>
                        <div>
                            <FormRow label={__("heating.content.morning")}>
                                <div class="row no-gutters">
                                    <div class="col-md-6">
                                        <div class="input-group">
                                            <div class="input-group-prepend heating-input-group-prepend"><span class="heating-fixed-size input-group-text">{__("heating.content.from")}</span></div>
                                            <InputTime
                                                className={"form-control-md heating-input-group-prepend"}
                                                date={new Date(0, 0, 1, 0, 0)}
                                                showSeconds={false}
                                            />
                                        </div>
                                    </div>
                                    <div class="col-md-6">
                                        <div class="input-group">
                                            <div class="input-group-prepend heating-input-group-append"><span class="heating-fixed-size input-group-text">{__("heating.content.to")}</span></div>
                                                <InputTime
                                                className={"form-control-md heating-input-group-prepend"}
                                                date={this.get_date_from_minutes(state.summer_block_time_morning)}
                                                showSeconds={false}
                                                onDate={(d: Date) => this.setState({summer_block_time_morning: this.get_minutes_from_date(d)})}
                                            />
                                        </div>
                                    </div>
                                </div>
                            </FormRow>
                            <FormRow label={__("heating.content.evening")}>
                                <div class="row no-gutters">
                                    <div class="col-md-6">
                                        <div class="input-group">
                                            <div class="input-group-prepend heating-input-group-prepend"><span class="heating-fixed-size input-group-text">{__("heating.content.from")}</span></div>
                                                <InputTime
                                                className={"form-control-md heating-input-group-prepend"}
                                                date={this.get_date_from_minutes(state.summer_block_time_evening)}
                                                showSeconds={false}
                                                onDate={(d: Date) => this.setState({summer_block_time_evening: this.get_minutes_from_date(d)})}
                                            />
                                        </div>
                                    </div>
                                    <div class="col-md-6">
                                        <div class="input-group">
                                            <div class="input-group-prepend heating-input-group-append"><span class="heating-fixed-size input-group-text">{__("heating.content.to")}</span></div>
                                            <InputTime
                                                className={"form-control-md heating-input-group-prepend"}
                                                date={new Date(0, 0, 1, 23, 59)}
                                                showSeconds={false}
                                            />
                                        </div>
                                    </div>
                                </div>
                            </FormRow>
                            <FormRow label={__("heating.content.pv_yield_forecast")} label_muted="Setzt die Blockierzeit anhand des erwarteten PV-Ertrag außer Kraft" help={__("heating.content.pv_yield_forecast_help")}>
                                <SwitchableInputNumber
                                    switch_label_active="Aktiv"
                                    switch_label_inactive="Inaktiv"
                                    unit="kWh"
                                    checked={state.summer_yield_forecast_active}
                                    onClick={this.toggle('summer_yield_forecast_active')}
                                    value={state.summer_yield_forecast_threshold}
                                    onValue={this.set("summer_yield_forecast_threshold")}
                                    min={0}
                                    max={1000}
                                    switch_label_min_width="100px"
                                />
                            </FormRow>
                        </div>
                    </Collapse>

                    <FormSeparator heading="Allgemeine Einstellungen"/>
                    <FormRow label={__("heating.content.pv_excess_control")} help={__("heating.content.pv_excess_control_help")}>
                        <SwitchableInputNumber
                            switch_label_active="Aktiv"
                            switch_label_inactive="Inaktiv"
                            unit="Watt"
                            checked={state.pv_excess_control_active}
                            onClick={this.toggle('pv_excess_control_active')}
                            value={state.pv_excess_control_threshold}
                            onValue={this.set("pv_excess_control_threshold")}
                            min={0}
                            max={100000}
                            switch_label_min_width="100px"
                        />
                    </FormRow>
                    <FormRow label={__("heating.content.dpc")} label_muted="Für niedrige Preise (einschalten unter Tagesdurchschnitts-Schwelle)" help={__("heating.content.dpc_extended_help")}>
                        <SwitchableInputNumber
                            switch_label_active="Aktiv"
                            switch_label_inactive="Inaktiv"
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
                    <FormRow label={__("heating.content.dpc")} label_muted="Für hohe Preise (blockieren über Tagesdurchschnitts-Schwelle)" help={__("heating.content.dpc_blocking_help")}>
                        <SwitchableInputNumber
                            switch_label_active="Aktiv"
                            switch_label_inactive="Inaktiv"
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
                    <FormRow label="Preisbasierter Heizplan" label_muted="Heizplan anhand der dynamischen Preise. Rot = blockierender Betrieb, Grün = Einschaltempfehlung">
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
                                class="heating--chart pb-3"
                                sub_page="heating"
                                color_cache_group="heating.default"
                                show={true}
                                on_mount={() => this.update_uplot()}
                                legend_time_label={__("day_ahead_prices.content.time")}
                                legend_time_with_minutes={true}
                                legend_div_ref={this.uplot_legend_div_ref}
                                aspect_ratio={3}
                                x_height={50}
                                x_format={{hour: '2-digit', minute: '2-digit'}}
                                x_padding_factor={0}
                                x_include_date={true}
                                y_min={0}
                                y_max={5}
                                y_unit={"ct/kWh"}
                                y_label={__("day_ahead_prices.content.price_ct_per_kwh")}
                                y_digits={3}
                                y_skip_upper={true}
                                y_sync_ref={this.uplot_wrapper_flags_ref}
                                only_show_visible={true}
                                padding={[15, 5, null, null]}
                            />
                        </UplotLoader>
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

export function init() {
}
