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

render(<ConfigPageHeader prefix="soc" title={__("soc.content.soc")} />, $('#soc_header')[0]);

import Chartist from "../../ts/chartist";
import ctAxisTitle from "../../ts/chartist-plugin-axistitle";


function update_soc_state() {
    let state = API.get('soc/state');

    $("#soc_status_soc").val(state.soc.toString() + " %");
    $("#soc_content_soc").val(state.soc.toString() + " %");
    $('#soc_sequencer_state').val(translate_unchecked("soc.script.sequencer_states." + String(state.sequencer_state)));
    $('#soc_time_since_state_change').val(util.format_timespan(Math.floor(state.time_since_state_change)));
    util.update_button_group("soc_content_btn_group_last_request_status", state.last_request_status ? 0 : 1);
}

function update_soc_config() {
    let config = API.default_updater('soc/config');
}

export function init() {
    API.register_config_form('soc/config', {
            overrides: () => {
                return {
                    soc_control_enabled: $('#soc_config_enabled').is(':checked'),
                    user_name: $('#soc_config_user_name').val().toString(),
                    password: util.passwordUpdate('#soc_config_password'),
                    pin: util.passwordUpdate('#soc_config_pin'),
                    vin: $('#soc_config_vin').val().toString(),
                    setpoint: parseInt($('#soc_config_setpoint').val().toString()),
                    update_rate_when_charging: parseInt($('#soc_config_update_rate_when_charging').val().toString()),
                    update_rate_when_idle: parseInt($('#soc_config_update_rate_when_idle').val().toString())
                }
            },
            error_string: __("soc.script.save_failed"),
            reboot_string: __("soc.script.reboot_content_changed")
        }
    );

    // $("#soc_vin_list_button").on("click", vin_list);
    $("#soc_refresh").on("click", refresh_soc);

    $('#soc_ignore_once').on("change", () => set_ignore_once($('#soc_ignore_once').prop('checked')));

    $("#soc_show_password").on("change", util.toggle_password_fn("#soc_password"));
    $("#soc_clear_password").on("change", util.clear_password_fn("#soc_password"));

    $("#soc_show_pin").on("change", util.toggle_password_fn("#soc_pin"));
    $("#soc_clear_pin").on("change", util.clear_password_fn("#soc_pin"));

    $('#soc_vin_list_dropdown').on('hidden.bs.dropdown', function (e) {
        $("#soc_vin_list_title").html(__("soc.content.vin_list_searching"));
        $("#soc_vin_list_spinner").prop('hidden', false);
        $("#soc_vin_list_results").prop('hidden', true);
        $("#soc_vin_list_button").dropdown('update');
    });

    // The soc tab layout is generated when it is shown first.
    // We have to create the chart then, to make sure it is scaled correctly.
    // Immediately deregister afterwards, as we don't want to recreate the chart
    // every time.
    $('#sidebar-soc').on('shown.bs.tab', function (e) {
        init_chart();
    });

    $('#sidebar-soc').on('hidden.bs.tab', function (e) {
        if (graph_update_interval != null) {
            clearInterval(graph_update_interval);
            graph_update_interval = null;
        }
    });

}

export function add_event_listeners(source: API.APIEventTarget) {
    source.addEventListener('soc/state', update_soc_state);
    source.addEventListener('soc/config', update_soc_config);
    source.addEventListener('soc/ignore_once', update_ignore_once);

//     source.addEventListener('soc/vin_info', (e) => {

// // !!! Ändern, so dass Request Status angezeigt wird!
//         console.log('soc/vin_list_results event listener: ' + e.data);

//         if (e.data == "Requesting vin list")
//             return;

//         window.clearTimeout(vin_list_timeout);
//         vin_list_timeout = null;

//         if (e.data == "No vehicles found") {
//             console.log("No vehicles found");
//             update_vin_list_results(JSON.parse("[]"));
//             return;
//         }

//         update_vin_list_results(JSON.parse(e.data));
//     }, false);


}

export function update_sidebar_state(module_init: any) {
    $('#sidebar-soc').prop('hidden', !module_init.soc);
}

function refresh_soc() {

    $.ajax({
        url: '/soc/request',
        method: 'PUT',
        contentType: 'application/json',
        data: JSON.stringify(null),
        // success: () => {},
        error: (xhr, status, error) => {
            util.add_alert("soc send request failed", "alert-danger", "soc send request failed", error + ": " + xhr.responseText);
        }
    });
}

function update_ignore_once() {
    let x = API.get('soc/ignore_once');

    $('#soc_ignore_once').prop("checked", x.ignore_once);
}

function set_ignore_once(ignore_once: boolean) {
    API.save('soc/ignore_once', {"ignore_once": ignore_once}, __("soc.script.ignore_once_update_failed"));
}

function car_icon() {
    return '<svg xmlns="http://www.w3.org/2000/svg" class="icon icon-tabler icon-tabler-car" width="24" height="24" viewBox="0 0 24 24" stroke-width="1.5" stroke="#2c3e50" fill="none" stroke-linecap="round" stroke-linejoin="round"><path stroke="none" d="M0 0h24v24H0z" fill="none"/><circle cx="7" cy="17" r="2" /><circle cx="17" cy="17" r="2" /><path d="M5 17h-2v-6l2 -5h9l4 5h1a2 2 0 0 1 2 2v4h-2m-4 0h-6m-6 -6h15m-6 0v-5" /></svg>'
}

// type VinInfo = Exclude<API.getType['soc/vin_list_results'], string>[0];

// function update_vin_list_results(data: VinInfo[]) {
//     $("#soc_vin_list_spinner").prop('hidden', true);
//     $("#soc_vin_list_results").prop('hidden', false);
//     $("#soc_vin_list_title").html(__("soc.script.vin_list_select_vin"));

//     if (data.length == 0) {
//         $("#soc_vin_list_results").html(__("soc.script.no_ap_found"));
//         return;
//     }
//     let result = ``;

//     $.each(data, (i, v: VinInfo) => {
//         let line = `<a id="soc_vin_list_result_${i}" class="dropdown-item" href="#">${car_icon()}<span class="pl-2">${v.vin}</span></a>`;
//         result += line;
//     });

//     $("#soc_vin_list_results").html(result);
//     $("#soc_vin_list_button").dropdown('update')

//     $.each(data, (i, v: VinInfo) => {
//         console.log("Setting vin: " + v.vin);
//         $(`#soc_vin_list_result_${i}`).on("click", () => set_vin(v.vin));
//     });
// }

// let vin_list_timeout: number = null;
// function vin_list() {
//     $.ajax({
//         url: '/soc/vin_list',
//         method: 'PUT',
//         contentType: 'application/json',
//         data: JSON.stringify(null),
//         error: (xhr, status, error) => {
//             util.add_alert("vin_list_failed", "alert-danger", __("soc.script.vin_list_init_failed"), error + ": " + xhr.responseText);
//             $('#soc_vin_list_dropdown').dropdown('hide');
//         },
//         success: () => {
//             vin_list_timeout = window.setTimeout(function () {
//                 vin_list_timeout = null;
//                     $.get("/soc/vin_list_results").done(function (data: VinInfo[]) {
//                         update_vin_list_results(data);
//                     }).fail((xhr, status, error) => {
//                         util.add_alert("vin_list_failed", "alert-danger", __("soc.script.vin_list_results_failed"), error + ": " + xhr.responseText);
//                         $('#soc_vin_list_dropdown').dropdown('hide');
//                     });
//                 }, 30000);
//         }
//     });
// }

// function set_vin(vin: string) {
//     $('#soc_vin').val(vin);
//     return;
// }

let soc_chart: Chartist.IChartistLineChart;
let graph_update_interval: number = null;

function update_chart() {
    $.get("/soc/history").done(function (values: Number[]) {
        const HISTORY_MINUTE_INTERVAL = 1;
        const HISTORY_HOURS = 12;
        const VALUE_COUNT = HISTORY_HOURS * (60 / HISTORY_MINUTE_INTERVAL);
        const LABEL_COUNT = 9;
        const VALUES_PER_LABEL = VALUE_COUNT / (LABEL_COUNT - 1); // - 1 for the last label that has no values

        if (values.length != VALUE_COUNT ) {
            console.log("SOC: Unexpected number of requested power values to plot!");
            return;
        }

        let labels = [];

        let now = Date.now();
        let start = now - 1000 * 60 * 60 * HISTORY_HOURS;
        for(let i = 0; i < values.length + 1; ++i) {
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
                values
            ]
        };
        init_chart(0, 100);
        soc_chart.update(data);
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

function init_chart(min_value=0, max_value=100) {
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
    soc_chart = new Chartist.Line('#soc_chart', data, {
        fullWidth: true,
        showPoint: false,
        low: min_value,
        high: max_value,
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
                axisTitle: __("soc.script.time"),
                axisClass: "ct-axis-title",
                offset: {
                    x: 0,
                    y: 40
                },
                textAnchor: "middle"
                },
                axisY: {
                axisTitle: __("soc.script.soc"),
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

    change_chart_time();
}
