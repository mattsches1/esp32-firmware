/* warp-charger
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

import feather from "../../ts/feather";

import * as util from "../../ts/util";
import * as API from "../../ts/api";

import { h, render } from "preact";
import { __, translate_unchecked } from "../../ts/translation";
import { ConfigPageHeader } from "../../ts/components/config_page_header";

render(<ConfigPageHeader prefix="phase_switcher" title={__("phase_switcher.content.phase_switcher")} />, $('#phase_switcher_header')[0]);

import Chartist from "../../ts/chartist";
import ctAxisTitle from "../../ts/chartist-plugin-axistitle";

function update_phase_switcher_state() {
    let state = API.get('phase_switcher/state');

    $("#phase_switcher_state_available_charging_power").val(state.available_charging_power.toString() + " W");
    util.update_button_group("phase_switcher_state_btn_group_active_phases", state.active_phases);

    $("#phase_switcher_content_available_charging_power").val(state.available_charging_power.toString() + " W");
    util.update_button_group("phase_switcher_content_btn_group_active_phases", state.active_phases);
    util.update_button_group("phase_switcher_content_btn_group_requested_phases", state.requested_phases_pending);
    $('#phase_switcher_time_since_state_change').val(util.format_timespan(Math.floor(state.time_since_state_change)));
    $('#phase_switcher_sequencer_state').val(translate_unchecked("phase_switcher.script.sequencer_states." + String(state.sequencer_state)));
    $('#phase_switcher_delay_time_current_value').val(util.format_timespan(Math.floor(state.delay_time)));
    if (state.sequencer_state == 20){
        $('#phase_switcher_minimum_duration_current_value').val(util.format_timespan(Math.floor(state.time_since_state_change)));
    } else {
        $('#phase_switcher_minimum_duration_current_value').val(util.format_timespan(Math.floor(0)));
    }
    if (state.sequencer_state == 40){
        $('#phase_switcher_pause_time_current_value').val(util.format_timespan(Math.floor(state.time_since_state_change)));
    } else {
        $('#phase_switcher_pause_time_current_value').val(util.format_timespan(Math.floor(0)));
    }
    util.update_button_group("phase_switcher_content_btn_group_contactor_state", state.contactor_state ? 1 : 0);
}

function update_phase_switcher_config() {
    let config = API.default_updater('phase_switcher/config');
}

function update_meter_values() {
    let values = API.get('meter/values');
    $('#phase_switcher_content_actual_charging_power').val(util.toLocaleFixed(values.power, 0) + " W");
}

function update_phase_switcher_low_level_state() {
    let state = API.get('phase_switcher/low_level_state');

    for(let i = 0; i < state.output_channels.length; ++i) {
        //intentionally inverted: the high button is the first
        util.update_button_group(`btn_group_phase_switcher_output_channel${i}`, state.output_channels[i] ? 0 : 1);
    }

    for(let i = 0; i < state.input_channels.length; ++i) {
        //intentionally inverted: the high button is the first
        util.update_button_group(`btn_group_phase_switcher_input_channel${i}`, state.input_channels[i] ? 0 : 1);
    }

    $(`#phase_switcher_on_delay_one_phase`).val(state.current_on_delay_time[0] + " s");
    $(`#phase_switcher_on_delay_two_phases`).val(state.current_on_delay_time[1] + " s");
    $(`#phase_switcher_on_delay_three_phases`).val(state.current_on_delay_time[2] + " s");

    $(`#phase_switcher_off_delay_one_phase`).val(state.current_off_delay_time[0] + " s");
    $(`#phase_switcher_off_delay_two_phases`).val(state.current_off_delay_time[1] + " s");
    $(`#phase_switcher_off_delay_three_phases`).val(state.current_off_delay_time[2] + " s");

}


export function init() {
    API.register_config_form('phase_switcher/config', {
            overrides: () => {
                return {
                    enabled: $('#phase_switcher_config_enabled').is(':checked'),
                    operating_mode: parseInt($('#phase_switcher_config_operating_mode').val().toString()),
                    delay_time_more_phases: parseInt($('#phase_switcher_config_delay_time_more_phases').val().toString()),
                    delay_time_less_phases: parseInt($('#phase_switcher_config_delay_time_less_phases').val().toString()),
                    minimum_duration: parseInt($('#phase_switcher_config_minimum_duration').val().toString()),
                    pause_time: parseInt($('#phase_switcher_config_pause_time').val().toString()),
                }
            },
            error_string: __("phase_switcher.script.save_failed"),
            reboot_string: __("phase_switcher.script.reboot_content_changed")
        }
    );


    // The phase switcher tab layout is generated when it is shown first.
    // We have to create the chart then, to make sure it is scaled correctly.
    // Immediately deregister afterwards, as we don't want to recreate the chart
    // every time.
    $('#sidebar-phase_switcher').on('shown.bs.tab', function (e) {
        init_chart();
    });

    $('#sidebar-phase_switcher').on('hidden.bs.tab', function (e) {
        if (graph_update_interval != null) {
            clearInterval(graph_update_interval);
            graph_update_interval = null;
        }
    });
}

export function add_event_listeners(source: API.APIEventTarget) {
    source.addEventListener('phase_switcher/state', () => update_phase_switcher_state());
    source.addEventListener('phase_switcher/low_level_state', () => update_phase_switcher_low_level_state());
    source.addEventListener('phase_switcher/config', () => update_phase_switcher_config());
    source.addEventListener('meter/values', () => update_meter_values());
}

export function update_sidebar_state(module_init: any) {
    $('#sidebar-phase_switcher').prop('hidden', !module_init.phase_switcher);
}

let phase_switcher_chart: Chartist.IChartistLineChart;
let graph_update_interval: number = null;

function update_chart() {
    $.get("/phase_switcher/requested_power_history").done(function (requested_power_values: Number[]) {
        $.get("/phase_switcher/charging_power_history").done(function (charging_power_values: Number[]) {
            $.get("/phase_switcher/requested_phases_history").done(function (requested_phases_values: Number[]) {

                const HISTORY_MINUTE_INTERVAL = 1;
                const HISTORY_HOURS = 12;
                const VALUE_COUNT = HISTORY_HOURS * (60 / HISTORY_MINUTE_INTERVAL);
                const LABEL_COUNT = 9;
                const VALUES_PER_LABEL = VALUE_COUNT / (LABEL_COUNT - 1); // - 1 for the last label that has no values

                if (requested_power_values.length != VALUE_COUNT || charging_power_values.length != VALUE_COUNT || requested_phases_values.length != VALUE_COUNT ) {
                    console.log("Phase switcher: Unexpected number of requested power values to plot!");
                    return;
                }

                if (charging_power_values.length != VALUE_COUNT) {
                    console.log("Phase switcher: Unexpected number of charging power values to plot!");
                    return;
                }

                if (requested_phases_values.length != VALUE_COUNT ) {
                    console.log("Phase switcher: Unexpected number of requested phases values to plot!");
                    return;
                }

                let labels = [];

                let now = Date.now();
                let start = now - 1000 * 60 * 60 * HISTORY_HOURS;
                for(let i = 0; i < requested_power_values.length + 1; ++i) {
                    if (i % VALUES_PER_LABEL == 0) {
                        let d = new Date(start + i * (1000 * 60 * HISTORY_MINUTE_INTERVAL));
                        labels[i] = d.toLocaleTimeString(navigator.language, {hour: '2-digit', minute: '2-digit', hour12: false});
                    }
                    else {
                        labels[i] = null;
                    }
                }

                let data = {
                    labels: labels,
                    series: [
                        requested_power_values,
                        charging_power_values,
                        requested_phases_values
                    ]
                };
                phase_switcher_chart.update(data);
            });
        });
    });
}

function change_chart_time() {
    if (graph_update_interval != null) {
        clearInterval(graph_update_interval);
        graph_update_interval = null;
    }

    update_chart();
    graph_update_interval = window.setInterval(update_chart, 10000);
}

function init_chart() {
    // Create a new line chart object where as first parameter we pass in a selector
    // that is resolving to our chart container element. The Second parameter
    // is the actual data object. The data object is initialized with dummy data,
    // the chart object is later updated with correct data via the update_chart() function.

    let data = {
        labels: ['1', '2'],
        series: [
            [1],
            [2],
            [3]
        ]
    };

    phase_switcher_chart = new Chartist.Line('#phase_switcher_chart', data, {
        fullWidth: true,
        showPoint: false,
        axisX: {
            offset: 50,
            labelOffset: {x: 0, y: 5}
        },
        axisY: {
            scaleMinSpace: 40,
            onlyInteger: true,
            offset: 50,
            labelOffset: {x: 0, y: 6}
        },
        plugins: [
            ctAxisTitle({
                axisX: {
                axisTitle: __("phase_switcher.script.time"),
                axisClass: "ct-axis-title",
                offset: {
                    x: 0,
                    y: 40
                },
                textAnchor: "middle"
                },
                axisY: {
                axisTitle: __("phase_switcher.script.power"),
                axisClass: "ct-axis-title",
                offset: {
                    x: 0,
                    y: 12
                },
                flipTitle: true
                }
            })
        ]
    });





    // phase_switcher_chart = new Chartist.Line('#phase_switcher_chart', <any>data, {
    //     fullWidth: true,
    //     showPoint: false,
    //     axisX: {
    //         offset: 50,
    //         labelOffset: {x: 0, y: 5}
    //     },
    //     axisY: {
    //         scaleMinSpace: 40,
    //         onlyInteger: true,
    //         offset: 50,
    //         labelOffset: {x: 0, y: 6}
    //     },
    //     plugins: [
    //         ctAxisTitle({
    //             axisX: {
    //             axisTitle: __("phase_switcher.script.time"),
    //             axisClass: "ct-axis-title",
    //             offset: {
    //                 x: 0,
    //                 y: 40
    //             },
    //             textAnchor: "middle"
    //             },
    //             axisY: {
    //             axisTitle: __("phase_switcher.script.power"),
    //             axisClass: "ct-axis-title",
    //             offset: {
    //                 x: 0,
    //                 y: 12
    //             },
    //             flipTitle: true
    //             }
    //         })
    //     ]
    // });

    change_chart_time();
}