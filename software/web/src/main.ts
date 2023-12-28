/* esp32-firmware
 * Copyright (C) 2020-2021 Erik Fleckstein <erik@tinkerforge.com>
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

import $ from "jquery";

import "bootstrap";

import * as feather from 'feather-icons';
import * as util from "./ts/util";
import * as API from "./ts/api";
import { __, translate_data_i18n, select_language } from "./ts/translation";

import { init_async_modal } from "./ts/components/async_modal";

import * as device_name from "./modules/device_name/main";
import * as evse_start from "./modules/evse_start/main";
import * as evse_common from "./modules/evse_common/main";
import * as phase_switcher from "./modules/phase_switcher/main";
import * as soc from "./modules/soc/main";
import * as meter from "./modules/meter/main";
import * as charge_tracker from "./modules/charge_tracker/main";
import * as charge_manager from "./modules/charge_manager/main";
import * as network_start from "./modules/network_start/main";
import * as network from "./modules/network/main";
import * as wifi from "./modules/wifi/main";
import * as wireguard from "./modules/wireguard/main";
import * as interfaces_start from "./modules/interfaces_start/main";
import * as mqtt from "./modules/mqtt/main";
import * as modbus_tcp from "./modules/modbus_tcp/main";
import * as users_start from "./modules/users_start/main";
import * as nfc from "./modules/nfc/main";
import * as users from "./modules/users/main";
import * as system_start from "./modules/system_start/main";
import * as ntp from "./modules/ntp/main";
import * as rtc from "./modules/rtc/main";
import * as event_log from "./modules/event_log/main";
import * as firmware_update from "./modules/firmware_update/main";
import * as charge_limits from "./modules/charge_limits/main";
import * as debug from "./modules/debug/main";

interface Module {
    init(): void;
    add_event_listeners(source: API.APIEventTarget): void;
    update_sidebar_state(m: API.Modules): void;
}

let modules: Module[] = [device_name, evse_start, evse_common, phase_switcher, soc, meter, charge_tracker, charge_manager, network_start, network, wifi, wireguard, interfaces_start, mqtt, modbus_tcp, users_start, nfc, users, system_start, ntp, rtc, event_log, firmware_update, charge_limits, debug];

$('.navbar-collapse a').on("click", function () {
    $(".navbar-collapse").collapse('hide');
});

let first_module_init = true;

function show_page() {
    $('#nav-list').removeAttr("style");
    $('#main').removeAttr("style");
}

function update_modules() {
    let module_init = API.get('info/modules');
    for (let m of modules) {
        m.update_sidebar_state(module_init);
    }

    if (first_module_init) {
        let hash = window.location.hash;
        let elem = $(`a[href="${hash}"]`);
        if (hash == "#meter") {
            // FIXME: brute force solution to allow hidden meter module in
            //        energy manager firmware. should be solved in a better way
        }
        else if (hash == "" || hash == '#status' || elem.length == 0 || elem.prop("hidden")) {
            window.location.hash = "#status";
            show_page();
            return;
        }
        elem.one('shown.bs.tab', show_page);
        elem.click();
        elem.parents(".nav-nested.collapse").collapse('show');
        first_module_init = false;
    }
}

export function registerEvents(eventSource: API.APIEventTarget) {
    eventSource.addEventListener('info/modules', update_modules);

    for (let m of modules) {
        m.add_event_listeners(eventSource);
    }
}

// This is the part of the initialization to be done
// if we are authenticated (or authentication is deactivated)
function init() {
    for (let m of modules) {
        m.init();
    }

    util.setupEventSource(true, false, (ws: WebSocket, eventTarget: API.APIEventTarget) => registerEvents(eventTarget));
}

function login() {
    let username = (document.getElementById("main_login_username") as HTMLInputElement).value;
    let password = (document.getElementById("main_login_password") as HTMLInputElement).value;

    let xhr = new XMLHttpRequest();
    let result = false;
    xhr.onreadystatechange = function () {
        if(xhr.readyState === XMLHttpRequest.DONE) {
            let status = xhr.status;
            if (status == 200) {
                 window.location.href = location.protocol + "//" + location.host;
                 result = true;
            } else if (status == 401) {
                console.log("credentials incorrect?");
            } else {
                console.log("error: got status ");
                console.log(status);
            }
        }
    }
    xhr.open("GET", location.protocol + "//" + location.host + "/credential_check", false, username, password);
    xhr.send();
    return result;
}

function setup_normal() {
    window.setTimeout(show_page, 1000);

    $('#sidebar-login').prop('hidden', true);
    $('#sidebar-status').addClass('active');
    $('#status').addClass(['show', 'active']);

    util.ifLoggedInElseReload(init);
}

function setup_login() {
    window.setTimeout(show_page, 1000);

    $('#sidebar-status').prop('hidden', true);
    $('#sidebar-login').addClass('active');
    $('#login').addClass(['show', 'active']);

    let show_password = document.getElementById('main_login_show_password') as HTMLInputElement;
    let password = document.getElementById('main_login_password') as HTMLInputElement;
    show_password.addEventListener("change", () => {
        if (password.type == 'password')
            password.type = 'text';
        else
            password.type = 'password';
    });

    let form = document.getElementById('main_login_form') as HTMLFormElement;
    form.addEventListener('submit', function (event: Event) {
        event.preventDefault();
        event.stopPropagation();

        if(!login()) {
            password.classList.add("shake");
            window.setTimeout(() => {
                password.classList.remove("shake");
                password.focus();
            }, 500);
        }
    }, false);
}

$(window).on('pageshow', (ev) => {
    util.ifLoggedInElse(setup_normal, setup_login);

    init_async_modal();

    feather.replace();

    translate_data_i18n();

    // Disabled for now.
    //  We somwhow have to force preact to re-render all components for this to work.
    /*window.addEventListener('languagechange', () => {
        console.log("language change!");
        select_language();
        translate_data_i18n();
    });*/

    $('a[data-toggle="tab"]').on('show.bs.tab', () => {
        $('a[data-toggle="tab"]').not(event.target as Element).removeClass("active");
        $('a[data-toggle="tab"]').not(event.target as Element).trigger("hidden.bs.tab");
    });

    window.addEventListener("hashchange", (event) => {
        if (event.oldURL != event.newURL) {
            let hash = window.location.hash;
            let elem = $(`a[href="${hash}"]`);
            if (hash == "#meter") {
                // FIXME: brute force solution to allow hidden meter module in
                //        energy manager firmware. should be solved in a better way
            }
            else if (hash == "" || elem.length == 0/* || elem.prop("hidden")*/) {
                window.location.hash = "#status";
                return;
            }
            elem.click();
            elem.parents(".nav-nested.collapse").collapse('show');
        }
    });

    $('a[data-toggle="tab"]').on('shown.bs.tab', function() {
        let hash = $(this).attr("href");
        if (window.location.hash == "" && hash == "#status")
            return;
        window.location.hash = hash;
    });

    $('a[data-toggle="tab"]').on('shown.bs.tab', () => {
        setTimeout(() => window.scrollTo(0,0), 1);
    });

    $("#reboot_button").on("click", () => {
        $('#reboot').modal('hide');
        util.reboot();
    });
});
