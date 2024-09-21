/* State of Charge (SOC) module for WARP Charger
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
#include "soc.h"

#include "bindings/errors.h"

#include "api.h"
#include "event_log.h"
#include "task_scheduler.h"
#include "tools.h"
#include "web_server.h"
#include "modules.h"

#include <WiFiClientSecure.h>
#include "cert_pem_fca.h"

#include "mbedtls/md.h"                     // for sha256
#include <base64.h>                         // for Base64 encoding PIN

#define API_KEY "3_mOx_J2dRgjXYCdyhchv3b5lhi54eBcdCTX4BI8MORqmZCoQWhA0mV2PTlptLGUQI"
#define X_API_KEY "qLYupk65UU1tw2Ih1cJhs4izijgRDbir2UFHA3Je"

extern EventLog logger;

extern TaskScheduler task_scheduler;
extern WebServer server;

extern API api;

void SOC::pre_setup()
{
    state = Config::Object({
        {"soc", Config::Uint8(0)},
        {"sequencer_state", Config::Uint8(0)},
        {"time_since_state_change", Config::Uint32(0)},
        {"last_request_status", Config::Bool(false)},
        {"ignore_soc_limit_once", Config::Bool(false)}
    });

    config = Config::Object({
        {"enabled", Config::Bool(false)},
        {"user_name", Config::Str("", 0, 64)},
        {"password", Config::Str("", 0, 64)},
        {"pin", Config::Str("", 4, 4)},
        {"vin", Config::Str("", 17, 17)},
        {"update_rate_when_charging", Config::Uint(300, 300, 3600)},
        {"update_rate_when_idle", Config::Uint(3600, 60, 36000)}
    });

    setpoint = Config::Object({
        {"setpoint", Config::Uint(80, 10, 100)}
    });

    setpoint_update = setpoint;
}

void SOC::setup()
{
    if (!meters.initialized){
        logger.printfln("SOC: Energy meter not available. Disabling SOC module.");
        return;
    }

    api.restorePersistentConfig("soc/config", &config);
    config_in_use = config;
    enabled = config.get("enabled")->asBool();

    api.restorePersistentConfig("soc/setpoint", &setpoint);

    api.addFeature("soc");

    soc_history.setup();

    history_chars_per_value = max(String(METER_VALUE_HISTORY_VALUE_MIN).length(), String(METER_VALUE_HISTORY_VALUE_MAX).length());
    // val_min values are replaced with null -> require at least 4 chars per value.
    history_chars_per_value = max(4U, history_chars_per_value);
    // For ',' between the values.
    ++history_chars_per_value;

    if (!enabled){
        initialized = true;
        logger.printfln("SOC module disabled by configuration.");
        return;
    }

    // Reserve memory for strings to reduce heap fragmentation:
    api_data.amz_date.reserve(16);
    api_data.login4.access_key_id.reserve(20);
    api_data.login1.uid.reserve(32);
    api_data.login4.secret_key.reserve(40);
    api_data.login3.identity_id.reserve(50);
    api_data.login1.login_token.reserve(256);
    api_data.pin_auth_token.reserve(750);
    api_data.login3.token.reserve(900);
    api_data.login2.id_token.reserve(1024);
    api_data.login4.session_token.reserve(1240);

    task_scheduler.scheduleWithFixedDelay([this](){
        this->sequencer();
    }, 10000, 1000);

    task_scheduler.scheduleWithFixedDelay([this](){
        update_all_data();
    }, 30, 250);

    task_scheduler.scheduleWithFixedDelay([this](){
        update_history();
    }, 0, 1000);

    initialized = true;
}

void SOC::register_urls()
{
    if (!initialized)
        return;

    api.addState("soc/state", &state);
 
    api.addState("soc/setpoint", &setpoint);
    api.addCommand("soc/setpoint_update", &setpoint_update, {}, [this](){
        uint16_t _setpoint = setpoint_update.get("setpoint")->asUint();
        setpoint.get("setpoint")->updateUint(_setpoint);
        api.writeConfig("soc/setpoint", &setpoint);
    }, false);

    api.addPersistentConfig("soc/config", &config, {"password", "pin"});

    api.addCommand("soc/vin_list", Config::Null(), {}, [this](){
        // Abort if a request to list vins is running.
        if (vin_list_requested || vin_list_request_active)
            return;

        logger.printfln("SOC: VIN list requested...");

        task_scheduler.scheduleOnce([this]() {
            vin_list_available = false;
            vin_list_requested = true;
            check_for_vin_list_completion();
        }, 500);
    }, true);

    server.on("/soc/vin_list_results", HTTP_GET, [this](WebServerRequest request) {
        if (vehicles.size() > 0) {
            return request.send(200, "application/json; charset=utf-8", get_vin_list().c_str());
        } else {
            return request.send(200, "text/plain; charset=utf-8", get_vin_list().c_str());
        }
        logger.printfln("SOC: VIN list requested -> complete");
    });

    api.addCommand("soc/manual_request", Config::Null(), {}, [this](){
        if (enabled){
            soc_requested = true;
            if (debug) logger.printfln("SOC: Manual request received; initialized: %d, enabled: %d", initialized, enabled);
        }
    }, false);

    api.addCommand("soc/toggle_ignore_once", Config::Null(), {}, [this](){
        this->ignore_soc_limit_once ^= 1;
        if (debug) logger.printfln("SOC: Ignore once set to %d", this->ignore_soc_limit_once);
    }, false);

    if (enabled) soc_history.register_urls("soc");

    server.on("/soc/start_debug", HTTP_GET, [this](WebServerRequest request) {
        logger.printfln("SOC: Enabling debug mode");
        debug = true;
        return request.send(200);
    });

    server.on("/soc/stop_debug", HTTP_GET, [this](WebServerRequest request){
        logger.printfln("SOC: Disabling debug mode");
        debug = false;
        return request.send(200);
    });

    server.on("/soc/test_http_client", HTTP_GET, [this](WebServerRequest request){
        logger.printfln("SOC: Testing HTTP client...");
        test_http_client();
        return request.send(200);
    });

// !!! FIXME
    server.on("/soc/set_debug0", HTTP_GET, [this](WebServerRequest request) {
        debug_level = 0;
        return request.send(200, "application/text; charset=utf-8", "SOC debug level set to 0", 40);
    });
    server.on("/soc/set_debug1", HTTP_GET, [this](WebServerRequest request) {
        debug_level = 1;
        return request.send(200, "application/text; charset=utf-8", "SOC debug level set to 1", 40);
    });
    server.on("/soc/set_debug2", HTTP_GET, [this](WebServerRequest request) {
        debug_level = 2;
        return request.send(200, "application/text; charset=utf-8", "SOC debug level set to 2", 40);
    });
    server.on("/soc/set_debug3", HTTP_GET, [this](WebServerRequest request) {
        debug_level = 3;
        return request.send(200, "application/text; charset=utf-8", "SOC debug level set to 3", 40);
    });
    server.on("/soc/set_debug4", HTTP_GET, [this](WebServerRequest request) {
        debug_level = 4;
        return request.send(200, "application/text; charset=utf-8", "SOC debug level set to 4", 40);
    });
    server.on("/soc/set_debug5", HTTP_GET, [this](WebServerRequest request) {
        debug_level = 5;
        return request.send(200, "application/text; charset=utf-8", "SOC debug level set to 5", 40);
    });

// !!! FIXME

}

void SOC::check_for_vin_list_completion()
{
    std::lock_guard<std::mutex> lock(mutex);

    if (debug) {
        logger.printfln("checking for vin list completion:");
        logger.printfln("vin_list_requested: %d, vin_list_request_active: %d, vin_list_available: %d", vin_list_requested, vin_list_request_active, vin_list_available);
    }

    if (!vin_list_available) {
        task_scheduler.scheduleOnce([this]() {
            check_for_vin_list_completion();
        }, 500);
        return;
    }
    logger.printfln("SOC: VIN list request completed.");

#ifdef MODULE_WS_AVAILABLE
    ws.pushRawStateUpdate(this->get_vin_list(), "soc/vin_list_results");
#endif

}

String SOC::get_vin_list()
{    
    if (vin_list_requested || vin_list_request_active)
        return "Requesting vin list";
    
    if (vehicles.size() == 0)
        return "No vehicles found";

    String result = "[";
    for (auto v = vehicles.begin(); v != vehicles.end(); ++v) {
        result += "{\"vin\": \"" + v->vin + "\",";
        result += "\"make\": \"" + v->make + "\",";
        result += "\"modelDescription\": \"" + v->model_description + "\",";
        result += "\"year\": \"" + (String)v->year + "\",";
        result += "\"color\": \"" + v->color + "\"}";
        if (v < vehicles.end() - 1)
            result += ",";
    }
    result += "]";
    return result;
}

void SOC::sequencer()
{
    // if (!mutex.try_lock()) {
    //     if (soc_requested) soc_requested = false;
    //     if (debug) logger.printfln("SOC: Sequencer mutex locked, skipping execution");
    //     return;
    // }

    std::lock_guard<std::mutex> lock(mutex);

    if (!api.hasFeature("evse")) {
        return;
    }

    if (api.getState("wifi/state", false)->get("connection_state")->asInt() != 3 ) {    // 3 = connected
        return;
    }   

    charger_state = ChargerState(api.getState("evse/state", false)->get("charger_state")->asUint());

    uint32_t update_rate = config_in_use.get("update_rate_when_idle")->asUint() * 1000;

    if (charger_state == charging) {
        update_rate = config_in_use.get("update_rate_when_charging")->asUint() * 1000;
    }

    if (deadline_elapsed(last_request + update_rate) && enabled){
        soc_requested = true;
    }

    // if (debug) {
    //     static SequencerState sequencer_state_old;
    //     if (sequencer_state != sequencer_state_old) log_memory();  
    // }

    switch(sequencer_state){
        case inactive:              sequencer_state_idle(); break;
        case init:                  sequencer_state_init(); break;
        case login1:                sequencer_state_login1(&cookie_jar); break;
        case login2:                sequencer_state_login2(&cookie_jar); break;
        case login3:                sequencer_state_login3(); break;
        case login4:                sequencer_state_login4(); break;
        case get_amz_date:          sequencer_state_get_amz_date(); break;
        case get_vehicles:          sequencer_state_get_vehicles(); break;
        case get_vehicle_status:    sequencer_state_get_vehicle_status(); break;
        case get_pin_auth:          sequencer_state_get_pin_auth(); break;
        case deep_refresh:          sequencer_state_deep_refresh(); break;
        case waiting_for_evse_stop: sequencer_state_waiting_for_evse_stop(); break;
    }

    // mutex.unlock();

}

void SOC::sequencer_state_idle()
{
    if (!initialized) 
        return;

    static ChargerState last_charger_state = charger_state;
    if (charger_state != last_charger_state) {
        if (charger_state == CHARGER_STATE_NOT_PLUGGED_IN) {
            ignore_soc_limit_once = false;
        } else if (charger_state == CHARGER_STATE_CHARGING && soc >= setpoint.get("setpoint")->asUint()) {
            logger.printfln("SOC: Charging started when above target SOC. Charging cycle will not be stopped by SOC module.");
            ignore_soc_limit_once = true;
        } 
        last_charger_state = charger_state;
    }


    if (vin_list_requested || soc_requested) {

        if (vin_list_requested) {
            vin_list_available = false;
            vin_list_request_active = true;
            vin_list_requested = false;
        } else if (soc_requested) {
            soc_request_active = true;
            soc_requested = false;
        }
        
        api_login_retry = false;
        pin_auth_retry = false;

        if (login_ok && !debug) {
            sequencer_state = get_amz_date;
        } else {        
            sequencer_state = init;
        }

        last_request = millis();

    } else {
        vin_list_request_active = false;
        soc_request_active = false;

        bool phase_switcher_quick_charging_active = false;

        if (api.hasFeature("phase_switcher"))
            phase_switcher_quick_charging_active = (api.getState("phase_switcher/state", false)->get("sequencer_state")->asUint() == 25);

        if (charger_state == charging && enabled && soc >= setpoint.get("setpoint")->asUint() && !ignore_soc_limit_once && !phase_switcher_quick_charging_active)
            sequencer_state = waiting_for_evse_stop;
    }
}

void SOC::sequencer_state_init()
{
    if (debug){
        logger.printfln("=====================================================");
        logger.printfln("Init\n");
        log_memory();
    }

    // CookieJar cookie_jar;
    String url = "https://loginmyuconnect.fiat.com/accounts.webSdkBootstrap?apiKey=" API_KEY;
    String payload;
    payload.reserve(220);
    std::vector<Header> headers;

    if (debug){
        logger.printfln("Allocated memory before request: cookie jar: %u; payload: %u; headers: %u", cookie_jar.capacity(), payload.length(), headers.capacity());
        log_memory();
    }

    int result = http_request(url, loginmyuconnect_fiat_com_root_cert_pem, HTTP_GET, &headers, &payload, &cookie_jar);

    if (debug){
        logger.printfln("Allocated memory after request: cookie jar: %u; payload: %u; headers: %u", cookie_jar.capacity(), payload.length(), headers.capacity());
        log_memory();
    }

    headers.clear();
    headers.shrink_to_fit();
    payload.clear();

    if (debug){
        logger.printfln("Allocated memory after destroy: cookie jar: %u; payload: %u; headers: %u", cookie_jar.capacity(), payload.length(), headers.capacity());
        log_memory();
    }

    if ((t_http_codes)result == HTTP_CODE_OK) {
        if (debug){
            logger.printfln("     ... ok");
            log_memory();
        }
        sequencer_state = login1;
    } else {
        logger.printfln("SOC: Init failed with HTTP code %d.", result);
        soc_status_ok = false;
        sequencer_state = inactive;
    }

    // if (debug) {
    //     logger.printfln("Cookies:");
    //     for (Cookie c : cookie_jar) {
    //         logger.printfln("---");
    //         logger.printfln("date: %s", ctime(&(c.date)));
    //         logger.printfln("name: %s", c.name.c_str());
    //         logger.write(("value: " + c.value).c_str(), ("value: " + c.value).length());
    //         logger.printfln("expires: %s", ctime(&(c.expires.date)));
    //     }
    // }
}
 
void SOC::sequencer_state_login1()
{
    if (debug){
        logger.printfln("=====================================================");
        logger.printfln("Login 1\n");
        log_memory();
    }

    // String url = "https://loginmyuconnect.fiat.com/accounts.login?loginID=" + config_in_use.get("user_name")->asString() + "&password=" + config_in_use.get("password")->asString() + "&targetEnv=jssdk&includeUserInfo=true&APIKey=" + API_KEY + "&format=json";

    String url;
    url.reserve(strlen("https://loginmyuconnect.fiat.com/accounts.login?loginID=") + strlen(config_in_use.get("user_name")->asUnsafeCStr()) + strlen("&password=") + strlen(config_in_use.get("password")->asUnsafeCStr()) + strlen("&targetEnv=jssdk&includeUserInfo=true&APIKey=") + strlen(API_KEY) + strlen("&format=json"));

    url = F("https://loginmyuconnect.fiat.com/accounts.login?loginID=");
    url += config_in_use.get("user_name")->asString();
    url += F("&password=");
    url += config_in_use.get("password")->asString();
    url += F("&targetEnv=jssdk&includeUserInfo=true&APIKey=");
    url += F(API_KEY);
    url += F("&format=json");

    if (debug) {
        logger.printfln("URL:");
        logger.write(url.c_str(), url.length());
    }

    // if (debug) {
    //     logger.printfln("Cookies:");
    //     for (Cookie c : cookie_jar) {
    //         logger.printfln("---");
    //         logger.printfln("date: %s", ctime(&(c.date)));
    //         logger.printfln("name: %s", c.name.c_str());
    //         logger.write(("value: " + c.value).c_str(), ("value: " + c.value).length());
    //         logger.printfln("expires: %s", ctime(&(c.expires.date)));
    //     }
    // }

    std::vector<Header> headers;
    String payload = F("");

    StaticJsonDocument<400> response;
    StaticJsonDocument<200> filter;
    filter["userInfo"]["UID"] = true;
    filter["sessionInfo"]["login_token"] = true;

    int result = http_request(url, loginmyuconnect_fiat_com_root_cert_pem, HTTP_GET, &headers, &payload, nullptr, response, filter);

    if ((t_http_codes)result == HTTP_CODE_OK) {
        api_data.login1.uid = response["userInfo"]["UID"].as<String>();
        api_data.login1.login_token = response["sessionInfo"]["login_token"].as<String>();

        if (debug) {
            logger.printfln("     ... ok");
            logger.printfln(("     UID: " + api_data.login1.uid).c_str());
            String s = "login_token: " + api_data.login1.login_token;
            logger.write(s.c_str(), strlen(s.c_str()));
            log_memory();
        }

        if (api_data.login1.uid != "null" && api_data.login1.login_token != "null") {
            sequencer_state = login2;
        } else {
            logger.printfln("SOC: Login 1 failed, UID and/or login token invalid.");
            soc_status_ok = false;
            sequencer_state = inactive;
        }
        
    } else {
        logger.printfln("SOC: Login 1 failed with HTTP code %d.", result);
        soc_status_ok = false;
        sequencer_state = inactive;

        // if (debug) {
        //     logger.printfln("Cookies:");
        //     for (Cookie c : cookie_jar) {
        //         logger.printfln("---");
        //         logger.printfln("date: %s", ctime(&(c.date)));
        //         logger.printfln("name: %s", c.name.c_str());
        //         logger.write(("value: " + c.value).c_str(), ("value: " + c.value).length());
        //         logger.printfln("expires: %s", ctime(&(c.expires.date)));
        //     }
        // }

    }
}

void SOC::sequencer_state_login1(CookieJar *cookie_jar)
{
    if (debug){
        logger.printfln("=====================================================");
        logger.printfln("Login 1\n");
        log_memory();
    }

    // String url = "https://loginmyuconnect.fiat.com/accounts.login?loginID=" + config_in_use.get("user_name")->asString() + "&password=" + config_in_use.get("password")->asString() + "&targetEnv=jssdk&includeUserInfo=true&APIKey=" + API_KEY + "&format=json";

    String url;
    url.reserve(strlen("https://loginmyuconnect.fiat.com/accounts.login?loginID=") + strlen(config_in_use.get("user_name")->asUnsafeCStr()) + strlen("&password=") + strlen(config_in_use.get("password")->asUnsafeCStr()) + strlen("&targetEnv=jssdk&includeUserInfo=true&APIKey=") + strlen(API_KEY) + strlen("&format=json"));

    url = F("https://loginmyuconnect.fiat.com/accounts.login?loginID=");
    url += config_in_use.get("user_name")->asString();
    url += F("&password=");
    url += config_in_use.get("password")->asString();
    url += F("&targetEnv=jssdk&includeUserInfo=true&APIKey=");
    url += F(API_KEY);
    url += F("&format=json");

    if (debug) {
        logger.printfln("URL:");
        logger.write(url.c_str(), url.length());
    }

    // if (debug) {
    //     logger.printfln("Cookies:");
    //     for (Cookie c : cookie_jar) {
    //         logger.printfln("---");
    //         logger.printfln("date: %s", ctime(&(c.date)));
    //         logger.printfln("name: %s", c.name.c_str());
    //         logger.write(("value: " + c.value).c_str(), ("value: " + c.value).length());
    //         logger.printfln("expires: %s", ctime(&(c.expires.date)));
    //     }
    // }

    std::vector<Header> headers;
    String payload = F("");
    // DynamicJsonDocument json_doc(4096);

    // int result = http_request(url, loginmyuconnect_fiat_com_root_cert_pem, HTTP_GET, &headers, &payload, &cookie_jar, &json_doc);

    StaticJsonDocument<400> response;
    StaticJsonDocument<200> filter;
    filter["userInfo"]["UID"] = true;
    filter["sessionInfo"]["login_token"] = true;

    int result = http_request(url, loginmyuconnect_fiat_com_root_cert_pem, HTTP_GET, &headers, &payload, cookie_jar, response, filter);

    if ((t_http_codes)result == HTTP_CODE_OK) {
        api_data.login1.uid = response["userInfo"]["UID"].as<String>();
        api_data.login1.login_token = response["sessionInfo"]["login_token"].as<String>();

        if (debug) {
            logger.printfln("     ... ok");
            logger.printfln(("     UID: " + api_data.login1.uid).c_str());
            String s = "login_token: " + api_data.login1.login_token;
            logger.write(s.c_str(), strlen(s.c_str()));
            log_memory();
        }

        if (api_data.login1.uid != "null" && api_data.login1.login_token != "null") {
            sequencer_state = login2;
            // sequencer_state_login2(cookie_jar);
        } else {
            logger.printfln("SOC: Login 1 failed, UID and/or login token invalid.");
            if (debug) log_memory();
            soc_status_ok = false;
            sequencer_state = inactive;
        }
        
    } else {
        logger.printfln("SOC: Login 1 failed with HTTP code %d.", result);
        soc_status_ok = false;
        sequencer_state = inactive;

        if (debug) {
            logger.printfln("Cookies:");
            for (Cookie c : *cookie_jar) {
                logger.printfln("---");
                logger.printfln("date: %s", ctime(&(c.date)));
                logger.printfln("name: %s", c.name.c_str());
                logger.write(("value: " + c.value).c_str(), ("value: " + c.value).length());
                logger.printfln("expires: %s", ctime(&(c.expires.date)));
            }
        }

    }
}

void SOC::sequencer_state_login2()
{
    if (debug){
        logger.printfln("=====================================================");
        logger.printfln("Login 2\n");
        log_memory();
    }

    String url = (String)"https://loginmyuconnect.fiat.com/accounts.getJWT?fields=profile.firstName%2Cprofile.lastName%2Cprofile.email%2Ccountry%2Clocale%2Cdata.disclaimerCodeGSDP&APIKey=" + API_KEY + "&login_token=" + api_data.login1.login_token;

    std::vector<Header> headers;
    String payload = F("");
    DynamicJsonDocument json_doc(1536);

    // int result = http_request(url, loginmyuconnect_fiat_com_root_cert_pem, HTTP_GET, &headers, &payload, &cookie_jar, &json_doc);
    int result = http_request(url, loginmyuconnect_fiat_com_root_cert_pem, HTTP_GET, &headers, &payload, nullptr, &json_doc);

    if ((t_http_codes)result == HTTP_CODE_OK) {
        api_data.login2.id_token = json_doc["id_token"].as<String>();

        if (debug) {
            logger.printfln("     ... ok");
            String s = "     id_token: " + api_data.login2.id_token;
            logger.write(s.c_str(), strlen(s.c_str()));
            log_memory();
        }

        if (api_data.login2.id_token != "null") {
            sequencer_state = login3;
        } else {
            logger.printfln("SOC: Login 2 failed, ID token invalid.");
            soc_status_ok = false;
            sequencer_state = inactive;
        }

    } else {
        logger.printfln("SOC: Login 2 failed with HTTP code %d.", result);
        soc_status_ok = false;
        sequencer_state = inactive;
    }
}

void SOC::sequencer_state_login2(CookieJar *cookie_jar)
{
    if (debug){
        logger.printfln("=====================================================");
        logger.printfln("Login 2\n");
        log_memory();
    }

    String url = (String)"https://loginmyuconnect.fiat.com/accounts.getJWT?fields=profile.firstName%2Cprofile.lastName%2Cprofile.email%2Ccountry%2Clocale%2Cdata.disclaimerCodeGSDP&APIKey=" + API_KEY + "&login_token=" + api_data.login1.login_token;

    std::vector<Header> headers;
    String payload = F("");
    DynamicJsonDocument json_doc(1536);

    int result = http_request(url, loginmyuconnect_fiat_com_root_cert_pem, HTTP_GET, &headers, &payload, cookie_jar, &json_doc);

    if ((t_http_codes)result == HTTP_CODE_OK) {
        api_data.login2.id_token = json_doc["id_token"].as<String>();

        if (debug) {
            logger.printfln("     ... ok");
            String s = "     id_token: " + api_data.login2.id_token;
            logger.write(s.c_str(), strlen(s.c_str()));
            log_memory();
        }

        if (api_data.login2.id_token != "null") {
            sequencer_state = login3;
        } else {
            logger.printfln("SOC: Login 2 failed, ID token invalid.");
            soc_status_ok = false;
            sequencer_state = inactive;
        }

    } else {
        logger.printfln("SOC: Login 2 failed with HTTP code %d.", result);
        soc_status_ok = false;
        sequencer_state = inactive;
    }

    cookie_jar->clear();
    cookie_jar->shrink_to_fit();

    // *cookie_jar = CookieJar();
}

void SOC::sequencer_state_login3()
{
    if (debug){
        logger.printfln("=====================================================");
        logger.printfln("Login 3\n");
        log_memory();
    }

    String url = "https://authz.sdpr-01.fcagcv.com/v2/cognito/identity/token";

    std::vector<Header> headers;
    headers.push_back({"content-type", "application/json"});
    headers.push_back({"x-api-key", X_API_KEY});
    headers.push_back({"clientrequestid", random_string(16)});
    headers.push_back({"x-originator-type", "web"});
    headers.push_back({"x-clientapp-version", "1.0"});
    
    DynamicJsonDocument json_doc(1536);
    String payload;

    json_doc["gigya_token"] = api_data.login2.id_token;

    serializeJson(json_doc, payload);
    json_doc.clear();

    if (debug) {
        logger.printfln("     Login 3 request body:");
        logger.write(payload.c_str(), strlen(payload.c_str()));
    }

    // int result = http_request(url, authz_sdpr_01_fcagcv_com_root_cert_pem, HTTP_POST, &headers, &payload, &cookie_jar, &json_doc);
    int result = http_request(url, authz_sdpr_01_fcagcv_com_root_cert_pem, HTTP_POST, &headers, &payload, nullptr, &json_doc);

    if ((t_http_codes)result == HTTP_CODE_OK) {
        api_data.login3.identity_id = json_doc["IdentityId"].as<String>();
        api_data.login3.token = json_doc["Token"].as<String>();

        if (debug) {
            logger.printfln("     ... ok");
            log_memory();
            // String s = "     IdentityId: " + api_data.login3.identity_id + "\n"
            //     + "     Token: " + api_data.login3.token;
            // logger.write(s.c_str(), strlen(s.c_str()));
        }

        if (api_data.login3.identity_id != "null" && api_data.login3.token != "null") {
            sequencer_state = login4;
        } else {
            logger.printfln("SOC: Login 3 failed, identity ID and/or token invalid.");
            soc_status_ok = false;
            sequencer_state = inactive;
        }

    } else {
        logger.printfln("SOC: Login 3 failed with HTTP code %d.", result);
        String body;
        serializeJson(json_doc, body);
        logger.printfln(body.c_str());
        soc_status_ok = false;
        sequencer_state = inactive;
    }
}

void SOC::sequencer_state_login4()
{
    if (debug){
        logger.printfln("=====================================================");
        logger.printfln("Login 4\n");
        log_memory();
    }

    String url = "https://cognito-identity.eu-west-1.amazonaws.com/";

    std::vector<Header> headers;
    headers.push_back({"X-Amz-User-Agent", "aws-sdk-js/2.283.1 callback"});
    headers.push_back({"Content-Type", "application/x-amz-json-1.1"});
    headers.push_back({"X-Amz-Target", "AWSCognitoIdentityService.GetCredentialsForIdentity"});
    headers.push_back({"X-Amz-Content-Sha256", sha256(api_data.login3.token)});
    
    DynamicJsonDocument json_doc(2048);
    String payload;
    payload.reserve(1200);

    if (debug){
        logger.printfln("After reserve");
        log_memory();
    }

    json_doc["IdentityId"] = api_data.login3.identity_id;
    json_doc["Logins"]["cognito-identity.amazonaws.com"] = api_data.login3.token;

    if (debug){
        logger.printfln("before serialize json");
        log_memory();
    }

    serializeJson(json_doc, payload);
    json_doc.clear();

    // if (debug) {
    //     logger.printfln("     Login 4 request body:");
    //     logger.write(payload.c_str(), strlen(payload.c_str()));
    //     logger.printfln("             SHA256 of login token: %s", (sha256(api_data.login3.token)).c_str());
    // }

    int result = http_request(url, cognito_identity_eu_west_1_amazonaws_com_root_cert_pem, HTTP_POST, &headers, &payload, nullptr, &json_doc);

    if (debug){
        logger.printfln("Before payload clear");
        log_memory();
    }

    payload.clear();

    if (debug){
        logger.printfln("After payload clear");
        log_memory();
    }

    if ((t_http_codes)result == HTTP_CODE_OK) {
        api_data.login4.access_key_id = json_doc["Credentials"]["AccessKeyId"].as<String>();
        api_data.login4.secret_key = json_doc["Credentials"]["SecretKey"].as<String>();
        api_data.login4.session_token = json_doc["Credentials"]["SessionToken"].as<String>();
        if (debug) {
            logger.printfln("     ... ok");
            log_memory();
            // String s = "     AccessKeyId: " + api_data.login4.access_key_id + "\n"
            //     + "     SecretKey: " + api_data.login4.secret_key + "\n"
            //     + "     SessionToken: " + api_data.login4.session_token;
            // logger.write(s.c_str(), strlen(s.c_str()));

            // logger.printfln("Received headers:");
            // for (Header h : headers) {
            //     logger.printfln((h.name + ": " + h.value).c_str());
            // }
        }

        if (api_data.login4.access_key_id != "null" && api_data.login4.secret_key != "null" && api_data.login4.session_token != "null") {
            login_ok = true;
            sequencer_state = get_amz_date;
        } else {
            logger.printfln("SOC: Login 4 failed, access key ID, secret key and/or session token invalid.");
            login_ok = false;
            soc_status_ok = false;
            sequencer_state = inactive;
        }

    } else {
        logger.printfln("SOC: Login 4 failed with HTTP code %d.", result);
        login_ok = false;
        soc_status_ok = false;
        sequencer_state = inactive;
    }
}

void SOC::sequencer_state_get_amz_date() 
{
   if (debug){
        logger.printfln("=====================================================");
        logger.printfln("Get amz_date\n");
        log_memory();
    }

    #define HTTP_TIME_PATTERN "%a, %d %b %Y %H:%M:%S"

    if (ntp.initialized) {
        time_t now_local = time(NULL);
        
        struct tm *tm_gmt;
        tm_gmt = gmtime(&now_local);
        char amz_date[17];
        strftime(amz_date, sizeof(amz_date), "%Y%m%dT%H%M%SZ", tm_gmt);
        if (debug) logger.printfln("amz date via RTC: %s", amz_date);
        api_data.amz_date = amz_date;

        if (vin_list_request_active)
            sequencer_state = get_vehicles;
        else if (charger_state == charging)
            if (pin_auth_ok)
                sequencer_state = deep_refresh;
            else
                sequencer_state = get_pin_auth;
        else
            sequencer_state = get_vehicle_status;

    } else {
        String url = "https://channels.sdpr-01.fcagcv.com";
        std::vector<Header> headers;
        String payload = F("");

        api_data.amz_date = F("");
        const char* headers_to_request = {"Date"};

        http_request(url, authz_sdpr_01_fcagcv_com_root_cert_pem, HTTP_GET, &headers, &payload, nullptr, &headers_to_request, 1);

        for (auto h = headers.begin(); h != headers.end(); ++h) {
            if (h->name == "Date"){
                struct tm tm;
                char amz_date[17];
                strptime(h->value.c_str(), HTTP_TIME_PATTERN, &tm);
                strftime(amz_date, sizeof(amz_date), "%Y%m%dT%H%M%SZ", &tm);
                if (debug){
                    logger.printfln("amz date via web request: %s", amz_date);
                    log_memory();
                }

                api_data.amz_date = amz_date;

                if (vin_list_request_active)
                    sequencer_state = get_vehicles;
                else if (charger_state == charging)
                    if (pin_auth_ok)
                        sequencer_state = deep_refresh;
                    else
                        sequencer_state = get_pin_auth;
                else
                    sequencer_state = get_vehicle_status;
                return;
            }
        }
        logger.printfln("SOC: Getting amz date failed, date header not found");
        sequencer_state = inactive;
        login_ok = false;
        pin_auth_ok = false;
    }
}

void SOC::sequencer_state_get_vehicles()
{
    if (debug){
        logger.printfln("=====================================================");
        logger.printfln("Getting vehicles\n");
        log_memory();
    }

    #define REGION "eu-west-1"
    #define SERVICE "execute-api"
    #define ALGORITHM "AWS4-HMAC-SHA256"

    String url = "https://channels.sdpr-01.fcagcv.com/v4/accounts/" + api_data.login1.uid + "/vehicles?stage=ALL";
    String client_request_id = random_string(16);
    String canonical_uri = url.substring(strlen("https://channels.sdpr-01.fcagcv.com"), url.indexOf("?"));
    String canonical_query_string = url.substring(url.indexOf("?") + 1);
    String payload = "";
    String hashed_payload = sha256(payload);

    std::vector<Header> headers;
    headers.push_back({"clientrequestid", client_request_id});
    headers.push_back({"content-type", "application/json"});
    headers.push_back({"x-amz-date", api_data.amz_date});
    headers.push_back({"x-amz-security-token", api_data.login4.session_token});
    headers.push_back({"x-api-key", X_API_KEY});
    headers.push_back({"x-originator-type", "web"});

    String canonical_headers;
    String signed_headers;

    aws_get_canonical_headers(&canonical_headers, &signed_headers, headers, url);

    String authorization_header;
    authorization_header.reserve(300);
    aws_get_authorization_header(HTTP_GET, canonical_uri, canonical_query_string, canonical_headers, signed_headers, hashed_payload, api_data.amz_date, api_data.login4.secret_key, api_data.login4.access_key_id, authorization_header);
    headers.push_back({"authorization", authorization_header});

    DynamicJsonDocument response(8192);

    // int result = http_request(url, authz_sdpr_01_fcagcv_com_root_cert_pem, HTTP_GET, &headers, &payload, &cookie_jar, &response);
    int result = http_request(url, authz_sdpr_01_fcagcv_com_root_cert_pem, HTTP_GET, &headers, &payload, nullptr, &response);

    if ((t_http_codes)result == HTTP_CODE_OK) {

        vehicles.clear();
        JsonArray arr = response["vehicles"].as<JsonArray>();
        for (JsonVariant value : arr) {
            if (value["fuelType"] == "E") {
                logger.printfln("SOC: Found VIN %s", value["vin"].as<String>().c_str());
                vehicles.push_back({value["vin"].as<String>(), value["make"].as<String>(), value["modelDescription"].as<String>(), value["year"].as<uint16_t>(), value["color"].as<String>() });
            }
        }
        if (debug) log_memory();
        vin_list_available = true;
        vin_list_request_active = false;
        vin_list_requested = false;

        sequencer_state = inactive;
    } else {
        if (login_ok && !api_login_retry) {
            login_ok = false;
            api_login_retry = true;
            sequencer_state = init;     // try again 
        } else {
            logger.printfln("SOC: Get vehicles retry failed with HTTP code %d.", result);
            vin_list_available = false;
            vin_list_request_active = false;
            vin_list_requested = false;
            sequencer_state = inactive;
        }
    }

}

void SOC::sequencer_state_get_vehicle_status()  
{
   if (debug){
        logger.printfln("=====================================================");
        logger.printfln("Getting vehicle status\n");
        log_memory();
    }

    #define REGION "eu-west-1"
    #define SERVICE "execute-api"
    #define ALGORITHM "AWS4-HMAC-SHA256"

    String url = "https://channels.sdpr-01.fcagcv.com/v2/accounts/" + api_data.login1.uid + "/vehicles/" + config_in_use.get("vin")->asString() + "/status";
    String client_request_id = random_string(16);
    String canonical_uri = url.substring(strlen("https://channels.sdpr-01.fcagcv.com"));
    String canonical_query_string = "";
    String payload = "";
    String hashed_payload = sha256(payload);

    std::vector<Header> headers;
    headers.push_back({"clientrequestid", client_request_id});
    headers.push_back({"content-type", "application/json"});
    headers.push_back({"x-amz-date", api_data.amz_date});
    headers.push_back({"x-amz-security-token", api_data.login4.session_token});
    headers.push_back({"x-api-key", X_API_KEY});
    headers.push_back({"x-originator-type", "web"});

    String canonical_headers;
    String signed_headers;

    // if (debug) log_memory();

    aws_get_canonical_headers(&canonical_headers, &signed_headers, headers, url);

    String authorization_header;
    authorization_header.reserve(300);
    aws_get_authorization_header(HTTP_GET, canonical_uri, canonical_query_string, canonical_headers, signed_headers, hashed_payload, api_data.amz_date, api_data.login4.secret_key, api_data.login4.access_key_id, authorization_header);
    headers.push_back({"authorization", authorization_header});

    // if (debug) log_memory();

    StaticJsonDocument<400> response;
    StaticJsonDocument<200> filter;
    filter["evInfo"]["battery"]["stateOfCharge"] = true;

    // int result = http_request(url, authz_sdpr_01_fcagcv_com_root_cert_pem, HTTP_GET, &headers, &payload, &cookie_jar, response, filter);
    int result = http_request(url, authz_sdpr_01_fcagcv_com_root_cert_pem, HTTP_GET, &headers, &payload, nullptr, response, filter);

    if ((t_http_codes)result == HTTP_CODE_OK) {

        soc = response["evInfo"]["battery"]["stateOfCharge"];

        if (debug){
            logger.printfln("SOC: Current value is %d", soc);
            log_memory();
        }
        soc_request_active = false;
        soc_requested = false;
        soc_status_ok = true;
        sequencer_state = inactive;

    } else {
        if (login_ok && !api_login_retry) {
            login_ok = false;
            api_login_retry = true;
            sequencer_state = init;     // login data outdated, try to log in again (once)
        } else {
            logger.printfln("SOC: Get vehicle status retry failed with HTTP code %d.", result);
            soc_request_active = false;
            soc_requested = false;
            soc_status_ok = false;
            sequencer_state = inactive;
        }
    }
}

void SOC::sequencer_state_get_pin_auth()  
{
   if (debug){
        logger.printfln("=====================================================");
        logger.printfln("Getting PIN auth\n");
        log_memory();
    }

    #define REGION "eu-west-1"
    #define SERVICE "execute-api"
    #define ALGORITHM "AWS4-HMAC-SHA256"
    #define X_API_KEY_PIN_AUTH "JWRYW7IYhW9v0RqDghQSx4UcRYRILNmc8zAuh5ys"

    String url = "https://mfa.fcl-01.fcagcv.com/v1/accounts/" + api_data.login1.uid + "/ignite/pin/authenticate";
    String client_request_id = random_string(16);
    String canonical_uri = url.substring(strlen("https://mfa.fcl-01.fcagcv.com"));
    String canonical_query_string = "";
    String pin_base64 = base64::encode(config_in_use.get("pin")->asUnsafeCStr());

    DynamicJsonDocument json_doc(256);
    String payload;
    json_doc["pin"] = pin_base64;
    serializeJson(json_doc, payload);
    json_doc.clear();

    String hashed_payload = sha256(payload);

    // if (debug) {
    //     // logger.printfln("URL: %s", url.c_str());
    //     // logger.printfln("Payload: %s", payload.c_str());
    //     // logger.printfln("Hashed payload: %s", hashed_payload.c_str());
    //     logger.printfln("ClientRequestId: %s", client_request_id.c_str());
    //     logger.printfln("AmzDate: %s", api_data.amz_date.c_str());
    //     logger.printfln("UID: %s", api_data.login1.uid.c_str());
    //     logger.printfln("AccessKeyId: %s", api_data.login4.access_key_id.c_str());
    //     logger.printfln("SecretKey: %s", api_data.login4.secret_key.c_str());
    //     logger.printfln("SessionToken:");
    //     logger.write(api_data.login4.session_token.c_str(), api_data.login4.session_token.length());
    // }

    std::vector<Header> headers;
    headers.push_back({"clientrequestid", client_request_id});
    headers.push_back({"content-type", "application/json"});
    headers.push_back({"locale", "de_de"});
    headers.push_back({"x-amz-content-sha256", hashed_payload});
    headers.push_back({"x-amz-date", api_data.amz_date});
    headers.push_back({"x-amz-security-token", api_data.login4.session_token});
    headers.push_back({"x-api-key", X_API_KEY_PIN_AUTH});

    String canonical_headers;
    String signed_headers;

    aws_get_canonical_headers(&canonical_headers, &signed_headers, headers, url);

    // String authorization_header;
    // {
    //     String canonical_request_data = "POST\n" + canonical_uri + "\n" + canonical_query_string + "\n" + canonical_headers + "\n" + signed_headers + "\n" + hashed_payload;
    //     String hashed_request_data = sha256(canonical_request_data);
        
    //     String credential_scope = api_data.amz_date.substring(0, 8) + "/" + REGION + "/" + SERVICE + "/" + "aws4_request";
    //     String string_to_sign = (String)ALGORITHM + "\n" + api_data.amz_date + "\n" + credential_scope + "\n" + hashed_request_data;
    //     String signature = aws_get_signature(api_data.login4.secret_key, api_data.amz_date.substring(0, 8), REGION, SERVICE, string_to_sign);
    //     authorization_header = (String)ALGORITHM + " " + "Credential=" + api_data.login4.access_key_id + "/" + credential_scope + ", SignedHeaders=" + signed_headers + ", Signature=" + signature;
    // }

    String authorization_header;
    authorization_header.reserve(300);
    aws_get_authorization_header(HTTP_POST, canonical_uri, canonical_query_string, canonical_headers, signed_headers, hashed_payload, api_data.amz_date, api_data.login4.secret_key, api_data.login4.access_key_id, authorization_header);
    headers.push_back({"authorization", authorization_header});

    DynamicJsonDocument response(1024);

    int result = http_request(url, authz_sdpr_01_fcagcv_com_root_cert_pem, HTTP_POST, &headers, &payload, nullptr, &response);
    
    if ((t_http_codes)result == HTTP_CODE_OK) {

        api_data.pin_auth_token = response["token"].as<String>();

        if (debug) {
            logger.printfln("     ... ok");
            log_memory();
            // logger.printfln("Pin Auth Token:\n");
            // logger.write(api_data.pin_auth_token.c_str(), api_data.pin_auth_token.length());
        } 
        pin_auth_ok = true;
        sequencer_state = deep_refresh;

    } else {
        if (login_ok && !api_login_retry) {
            login_ok = false;
            api_login_retry = true;
            pin_auth_retry = false;
            sequencer_state = init;     // login data outdated, try to log in again (once)
        } else {
            logger.printfln("SOC: Get PIN auth retry failed with HTTP code %d.", result);
            soc_request_active = false;
            soc_requested = false;
            soc_status_ok = false;
            sequencer_state = inactive;
        }
    }
}

void SOC::sequencer_state_deep_refresh()  
{
   if (debug){
        logger.printfln("=====================================================");
        logger.printfln("Requesting deep refresh\n");
        log_memory();
    }

    #define REGION "eu-west-1"
    #define SERVICE "execute-api"
    #define ALGORITHM "AWS4-HMAC-SHA256"
    #define COMMAND "DEEPREFRESH"

    String url = "https://channels.sdpr-01.fcagcv.com/v1/accounts/" + api_data.login1.uid + "/vehicles/" + config_in_use.get("vin")->asString() + "/ev";;
    String client_request_id = random_string(16);
    String canonical_uri = url.substring(strlen("https://channels.sdpr-01.fcagcv.com"));
    String canonical_query_string = "";

    DynamicJsonDocument json_doc(1024);
    String payload;
    json_doc["command"] = COMMAND;
    json_doc["pinAuth"] = api_data.pin_auth_token;
    serializeJson(json_doc, payload);
    json_doc.clear();

    String hashed_payload = sha256(payload);

    std::vector<Header> headers;
    headers.push_back({"clientrequestid", client_request_id});
    headers.push_back({"content-type", "application/json"});
    headers.push_back({"x-amz-content-sha256", hashed_payload});
    headers.push_back({"x-amz-date", api_data.amz_date});
    headers.push_back({"x-amz-security-token", api_data.login4.session_token});
    headers.push_back({"x-api-key", X_API_KEY});
    headers.push_back({"x-originator-type", "web"});

    String canonical_headers;
    String signed_headers;

    aws_get_canonical_headers(&canonical_headers, &signed_headers, headers, url);

    // String authorization_header;
    // {
    //     String canonical_request_data = "POST\n" + canonical_uri + "\n" + canonical_query_string + "\n" + canonical_headers + "\n" + signed_headers + "\n" + hashed_payload;
    //     String hashed_request_data = sha256(canonical_request_data);
    //     String credential_scope = api_data.amz_date.substring(0, 8) + "/" + REGION + "/" + SERVICE + "/" + "aws4_request";
    //     String string_to_sign = (String)ALGORITHM + "\n" + api_data.amz_date + "\n" + credential_scope + "\n" + hashed_request_data;
    //     String signature = aws_get_signature(api_data.login4.secret_key, api_data.amz_date.substring(0, 8), REGION, SERVICE, string_to_sign);
    //     authorization_header = (String)ALGORITHM + " " + "Credential=" + api_data.login4.access_key_id + "/" + credential_scope + ", SignedHeaders=" + signed_headers + ", Signature=" + signature;
    // }

    String authorization_header;
    authorization_header.reserve(300);
    aws_get_authorization_header(HTTP_POST, canonical_uri, canonical_query_string, canonical_headers, signed_headers, hashed_payload, api_data.amz_date, api_data.login4.secret_key, api_data.login4.access_key_id, authorization_header);
    headers.push_back({"authorization", authorization_header});

    DynamicJsonDocument response(256);

    int result = http_request(url, authz_sdpr_01_fcagcv_com_root_cert_pem, HTTP_POST, &headers, &payload, nullptr, &response);


    if ((t_http_codes)result == HTTP_CODE_OK) {
        if (debug){
            logger.printfln("     ... ok");;
            log_memory();
        }
        sequencer_state = get_vehicle_status;

    } else {
        if (login_ok && !pin_auth_retry) {
            pin_auth_ok = false;
            pin_auth_retry = true;
            sequencer_state = get_pin_auth;     // PIN authorization token outdated, try to log in again (once)
        
        } else {
            logger.printfln("SOC: Deep refresh retry failed with HTTP code %d.", result);
            soc_request_active = false;
            soc_requested = false;
            soc_status_ok = false;
            sequencer_state = inactive;
        }
    }
}

void SOC::sequencer_state_waiting_for_evse_stop()
{
    static uint32_t watchdog_start = 0;

    if (deadline_elapsed(watchdog_start + EVSE_STOP_TIMEOUT)){
        logger.printfln("SOC: SOC setpoint reached, sending stop API request to EVSE.");
        api.callCommand("evse/stop_charging", nullptr);
        watchdog_start = millis();
    }

    if (charger_state != charging){
        logger.printfln("SOC: EVSE stopped charging.");
        sequencer_state = inactive;
    } 
}

void SOC::update_all_data() 
{
    if (!initialized)
        return;

    state.get("soc")->updateUint(soc);
    state.get("sequencer_state")->updateUint(uint8_t(sequencer_state));
    state.get("time_since_state_change")->updateUint((millis() - last_request) / 1000);
    state.get("last_request_status")->updateBool(soc_status_ok);
    state.get("ignore_soc_limit_once")->updateBool(ignore_soc_limit_once);

    soc_history.add_sample(soc);
}

void SOC::update_history()
{
    uint32_t now = millis();
    uint32_t current_history_slot = now / (HISTORY_MINUTE_INTERVAL * 60 * 1000);
    bool update_history = current_history_slot != last_history_slot;
    METER_VALUE_HISTORY_VALUE_TYPE live_sample;
    METER_VALUE_HISTORY_VALUE_TYPE history_sample;
    METER_VALUE_HISTORY_VALUE_TYPE val_min = std::numeric_limits<METER_VALUE_HISTORY_VALUE_TYPE>::lowest();

    soc_history.tick(now, true, &live_sample, &history_sample);

    last_live_update = now;
    end_this_interval = last_live_update;

    if (samples_this_interval == 0) {
        begin_this_interval = last_live_update;
    }

    ++samples_this_interval;

#if MODULE_WS_AVAILABLE()
    {
        const size_t buf_size = history_chars_per_value + 100;
        char *buf_ptr = static_cast<char *>(malloc(sizeof(char) * buf_size));
        size_t buf_written = 0;

        buf_written += snprintf_u(buf_ptr + buf_written, buf_size - buf_written, "{\"topic\":\"soc/live_samples\",\"payload\":{\"samples_per_second\":%f,\"samples\":[", static_cast<double>(live_samples_per_second()));

        if (buf_written < buf_size) {
            for (uint32_t slot = 0; slot < METERS_SLOTS && buf_written < buf_size; slot++) {
                if (live_sample == val_min) {
                    buf_written += snprintf_u(buf_ptr + buf_written, buf_size - buf_written, slot == 0 ? "[%s]" : ",[%s]", "null");
                }
                else {
                    buf_written += snprintf_u(buf_ptr + buf_written, buf_size - buf_written, slot == 0 ? "[%d]" : ",[%d]", static_cast<int>(live_sample));
                }
            }

            if (buf_written < buf_size) {
                buf_written += snprintf_u(buf_ptr + buf_written, buf_size - buf_written, "%s", "]}}\n");
            }
        }

        if (buf_written > 0) {
            ws.web_sockets.sendToAllOwned(buf_ptr, static_cast<size_t>(buf_written));
        }
    }
#endif

    if (update_history) {
        last_history_update = now;
        samples_last_interval = samples_this_interval;
        begin_last_interval = begin_this_interval;
        end_last_interval = end_this_interval;

        samples_this_interval = 0;
        begin_this_interval = 0;
        end_this_interval = 0;

#if MODULE_WS_AVAILABLE()
        {
            const size_t buf_size = history_chars_per_value + 100;
            char *buf_ptr = static_cast<char *>(malloc(sizeof(char) * buf_size));
            size_t buf_written = 0;

            buf_written += snprintf_u(buf_ptr + buf_written, buf_size - buf_written, "%s", "{\"topic\":\"soc/history_samples\",\"payload\":{\"samples\":[");

            if (buf_written < buf_size) {
                for (uint32_t slot = 0; slot < METERS_SLOTS && buf_written < buf_size; slot++) {
                    if (history_sample == val_min) {
                        buf_written += snprintf_u(buf_ptr + buf_written, buf_size - buf_written, slot == 0 ? "[%s]" : ",[%s]", "null");
                    }
                    else {
                        buf_written += snprintf_u(buf_ptr + buf_written, buf_size - buf_written, slot == 0 ? "[%d]" : ",[%d]", static_cast<int>(history_sample));
                    }
                }

                if (buf_written < buf_size) {
                    buf_written += snprintf_u(buf_ptr + buf_written, buf_size - buf_written, "%s", "]}}\n");
                }
            }

            if (buf_written > 0) {
                ws.web_sockets.sendToAllOwned(buf_ptr, static_cast<size_t>(buf_written));
            }
        }
#endif
    }
}

float SOC::live_samples_per_second()
{
    float samples_per_second = 0;

    // Only calculate samples_per_second based on the last interval
    // if we have seen at least 2 values. With the API meter module,
    // it can happen that we see exactly one value in the first interval.
    // In this case 0 samples_per_second is reported for the next
    // interval (i.e. four minutes).
    if (samples_last_interval > 1) {
        uint32_t duration = end_last_interval - begin_last_interval;

        if (duration > 0) {
            // (samples_last_interval - 1) because there are N samples but only (N - 1) gaps
            // between them covering (end_last_interval - begin_last_interval) milliseconds
            samples_per_second = static_cast<float>((samples_last_interval - 1) * 1000) / static_cast<float>(duration);
        }
    }
    // Checking only for > 0 in this branch is fine: If we have seen
    // 0 or 1 samples in the last interval and exactly 1 in this interval,
    // we can only report that samples_per_second is 0.
    // This fixes itself when the next sample arrives.
    else if (samples_this_interval > 0) {
        uint32_t duration = end_this_interval - begin_this_interval;

        if (duration > 0) {
            // (samples_this_interval - 1) because there are N samples but only (N - 1) gaps
            // between them covering (end_this_interval - begin_this_interval) milliseconds
            samples_per_second = static_cast<float>((samples_this_interval - 1) * 1000) / static_cast<float>(duration);
        }
    }

    return samples_per_second;
}

int SOC::http_request(const String &url, const char* cert, http_method method, std::vector<Header> *headers, String *payload, CookieJar *cookie_jar, const char *headers_to_collect[], size_t num_headers_to_collect) 
{
    WiFiClientSecure *client = new WiFiClientSecure;
    int http_code = -1;

    if(client) {
        client->setCACert(cert);
        // client->setInsecure();
        {
            // Add a scoping block for HTTPClient http to make sure it is destroyed before WiFiClientSecure *client is 
            HTTPClient http;

            http.setCookieJar(cookie_jar);

            // http.setConnectTimeout(2000);
            // http.setTimeout(4000);

            if (headers_to_collect)
                http.collectHeaders(headers_to_collect, num_headers_to_collect);

            if (debug) {
                logger.printfln("Starting HTTP request");
                logger.printfln("URL: ");
                logger.write(url.c_str(), url.length());
            }
            
            // http.useHTTP10(true);

            if (http.begin(*client, url)) {

                // prepare for collecting response headers
                if (headers_to_collect)
                    http.collectHeaders(headers_to_collect, num_headers_to_collect);

                // add headers
                for (auto h = headers->begin(); h != headers->end(); ++h) {
                    // if (debug) {
                    //     String s = "SOC: Adding header name: " + h->name + "; value: " + h->value;
                    //     logger.write(s.c_str(), strlen(s.c_str()));
                    // }
                    http.addHeader(h->name, h->value);
                }

                // start connection and send HTTP header
                if (debug) logger.printfln("SOC: [HTTPS] request...");

                switch (method) {
                    case HTTP_POST: http_code = http.POST(*payload); break;
                    default: http_code = http.GET(); break;
                }

                // http_code will be negative on error
                if (http_code > 0) {
                    // HTTP header has been send and Server response header has been handled
                    // if (debug) logger.printfln("SOC: [HTTPS] ... code: %d\n", http_code);
            
                    // file found at server
                    if (http_code == HTTP_CODE_OK || http_code == HTTP_CODE_MOVED_PERMANENTLY) {
                        *payload = http.getString();
                    }
                    
                    if (headers_to_collect) {
                        headers->clear();
                        for (int i = 0; i< http.headers(); i++){
                            headers->push_back({http.headerName(i), http.header(i)});
                        }
                        if (debug) {
                            logger.printfln("SOC: [HTTPS] Collected headers:");
                            for (int i = 0; i< http.headers(); i++){
                                logger.printfln(("             " + http.headerName(i) + " : " + http.header(i)).c_str());
                            }
                        }
                    }

                } else {
                    if (debug) logger.printfln("SOC: [HTTPS] ... failed, error: %s", http.errorToString(http_code).c_str());
                }
                
                http.end();

            } else {
                logger.printfln("SOC: [HTTPS] Unable to connect to %s", url.c_str());
            }
            // End extra scoping block
        }
    
        delete client;

    } else {
        logger.printfln("Unable to create client");
    }
    return http_code;

}

int SOC::http_request(const String &url, const char* cert, http_method method, std::vector<Header> *headers, String *payload, CookieJar *cookie_jar, DynamicJsonDocument *response, const char *headers_to_collect[], size_t num_headers_to_collect) 
{
    WiFiClientSecure *client = new WiFiClientSecure;
    int http_code = -1;

    if(client) {
        client->setCACert(cert);
        // client->setInsecure();
        {
            // Add a scoping block for HTTPClient http to make sure it is destroyed before WiFiClientSecure *client is 
            HTTPClient http;

            http.setCookieJar(cookie_jar);

            // http.setConnectTimeout(2000);
            // http.setTimeout(4000);

            if (headers_to_collect)
                http.collectHeaders(headers_to_collect, num_headers_to_collect);

            // if (debug) {
            //     logger.printfln("Starting HTTP request");
            // }

            // http.useHTTP10(true);
            
            if (http.begin(*client, url)) {

                // prepare for collecting response headers
                if (headers_to_collect)
                    http.collectHeaders(headers_to_collect, num_headers_to_collect);

                // add headers
                for (auto h = headers->begin(); h != headers->end(); ++h) {
                    // if (debug) {
                    //     String s = "SOC: Adding header name: " + h->name + "; value: " + h->value;
                    //     logger.write(s.c_str(), strlen(s.c_str()));
                    // }
                    http.addHeader(h->name, h->value);
                }

                // start connection and send HTTP header
                // if (debug) logger.printfln("SOC: [HTTPS] request...");

                switch (method) {
                    case HTTP_POST: http_code = http.POST(*payload); break;
                    default: http_code = http.GET(); break;
                }

                // http_code will be negative on error
                if (http_code > 0) {
                    // HTTP header has been send and Server response header has been handled
                    // if (debug) logger.printfln("SOC: [HTTPS] ... code: %d\n", http_code);

                    // file found at server
                    if (http_code == HTTP_CODE_OK || http_code == HTTP_CODE_MOVED_PERMANENTLY) {
                        deserializeJson(*response, http.getStream());
                    // } else {
                    //     if (debug) {
                    //         logger.printfln("HTTP Request failed; response:");
                    //         String response_string = http.getString();
                    //         logger.write(response_string.c_str(), response_string.length());
                    //     }
                    }

                    if (headers_to_collect) {
                        headers->clear();
                        for (int i = 0; i< http.headers(); i++){
                            headers->push_back({http.headerName(i), http.header(i)});
                        }
                    }

                // } else {
                //     if (debug) logger.printfln("SOC: [HTTPS] ... failed, error: %s", http.errorToString(http_code).c_str());
                }
                
                http.end();

            } else {
                logger.printfln("SOC: [HTTPS] Unable to connect to %s", url.c_str());
            }
            // End extra scoping block
        }
    
        delete client;

    } else {
        logger.printfln("Unable to create client");
    }
    return http_code;

}

int SOC::http_request(const String &url, const char* cert, http_method method, std::vector<Header> *headers, String *payload, CookieJar *cookie_jar, StaticJsonDocument<400> &response, StaticJsonDocument<200> &filter) 
{
    WiFiClientSecure *client = new WiFiClientSecure;
    int http_code = -1;

    if(client) {
        client->setCACert(cert);
        // client->setInsecure();
        {
            // Add a scoping block for HTTPClient http to make sure it is destroyed before WiFiClientSecure *client is 
            HTTPClient http;

            http.setCookieJar(cookie_jar);

            // http.setConnectTimeout(2000);
            // http.setTimeout(4000);

            // if (debug) {
            //     logger.printfln("Starting HTTP request");
            // }

            // http.useHTTP10(true);
            
            if (http.begin(*client, url)) {

                // add headers
                for (auto h = headers->begin(); h != headers->end(); ++h) {
                    // if (debug) {
                    //     String s = "SOC: Adding header name: " + h->name + "; value: " + h->value;
                    //     logger.write(s.c_str(), strlen(s.c_str()));
                    // }
                    http.addHeader(h->name, h->value);
                }

                // start connection and send HTTP header
                // if (debug) logger.printfln("SOC: [HTTPS] request...");

                switch (method) {
                    case HTTP_POST: http_code = http.POST(*payload); break;
                    default: http_code = http.GET(); break;
                }

                // http_code will be negative on error
                if (http_code > 0) {
                    // HTTP header has been send and Server response header has been handled
                    // if (debug) logger.printfln("SOC: [HTTPS] ... code: %d\n", http_code);

                    // file found at server
                    if (http_code == HTTP_CODE_OK || http_code == HTTP_CODE_MOVED_PERMANENTLY) {
                        deserializeJson(response, http.getStream(), DeserializationOption::Filter(filter));
                    // } else {
                    //     if (debug) {
                    //         logger.printfln("HTTP Request failed; response:");
                    //         String response_string = http.getString();
                    //         logger.write(response_string.c_str(), response_string.length());
                    //     }
                    }

                // } else {
                //     if (debug) logger.printfln("SOC: [HTTPS] ... failed, error: %s", http.errorToString(http_code).c_str());
                }
                
                http.end();

            } else {
                logger.printfln("SOC: [HTTPS] Unable to connect to %s", url.c_str());
            }
            // End extra scoping block
        }
    
        delete client;

    } else {
        logger.printfln("Unable to create client");
    }
    return http_code;

}

String SOC::sha256(String &payload)
{
    byte sha[32];

    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const unsigned char *) payload.c_str(), payload.length());
    mbedtls_md_finish(&ctx, sha);
    mbedtls_md_free(&ctx);

    String sha_string = "";

    for (int i = 0; i < sizeof(sha); ++i) {
        if (sha[i] == 0)
            sha_string += "00";
        else if (sha[i] <= 0x0F)
            sha_string += "0" + String(sha[i], HEX);
        else
            sha_string += String(sha[i], HEX);
    }

    return sha_string;
}

void SOC::hmac_sha256(const char* payload, size_t payload_length, const char* key, size_t key_length, unsigned char* hmac)
{
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *) key, key_length);
    mbedtls_md_hmac_update(&ctx, (const unsigned char *) payload, payload_length);
    mbedtls_md_hmac_finish(&ctx, (unsigned char *) hmac);
    mbedtls_md_free(&ctx);

    // if (debug) {
    //     logger.printfln("-------- HMAC-SHA256 -----------");

    //     if (payload_length <= 128)
    //         logger.printfln("payload: %s", payload);
    //     else
    //         logger.printfln("payload:");
    //     logger.write(payload, payload_length);

    //     logger.printfln("payload length: %d", payload_length);

    //     logger.printfln("key: %s", key);
    //     logger.printfln("key length: %d", key_length);

    //     String hmac_string = "";
    //     for (int i = 0; i < 32; ++i) {
    //         if (hmac[i] == 0)
    //             hmac_string += "00";
    //         else if (hmac[i] <= 0x0F)
    //             hmac_string += "0" + String(hmac[i], HEX);
    //         else
    //             hmac_string += String(hmac[i], HEX);
    //     }
    //     logger.printfln("HMAC: %s", hmac_string.c_str());
    // }
}

String SOC::random_string(const int len) 
{
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    String tmp_s;
    tmp_s.reserve(len);

    for (int i = 0; i < len; ++i) {
        tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
    }
    
    return tmp_s;
}

void SOC::aws_get_canonical_headers(String *canonical_headers, String *signed_headers, std::vector<Header> headers, String &url)
{
    int pos1 = url.indexOf("//") + 2;
    int pos2 = url.indexOf("/", pos1);
    String host = url.substring(pos1, pos2);
    headers.push_back({"host", host});

    sort(headers.begin(), headers.end(), [] (Header &x, Header &y) { return x.name < y.name; });

    *canonical_headers = "";
    *signed_headers = "";
    
    for (auto &h : headers) {
        h.name.toLowerCase();
        if (h.name != "connection") {
            *canonical_headers += h.name + ":" + h.value + "\n";
            *signed_headers += h.name + ";";
        }
    }
    signed_headers->remove(signed_headers->length() - 1);
}

void SOC::aws_get_signature(const String &secret_key, const String &date_stamp, const String &region_name, const String &service_name, const String &string_to_sign, String &signature)
{
    // see http://docs.aws.amazon.com/general/latest/gr/sigv4-calculate-signature.html

    unsigned char k_date[32], k_region[32], k_service[32], k_signing_key[32], k_signature[32];

    hmac_sha256(date_stamp.c_str(), date_stamp.length(), ("AWS4" + secret_key).c_str(), ("AWS4" + secret_key).length(), k_date);
    hmac_sha256(region_name.c_str(), region_name.length(), (const char *) k_date, sizeof(k_date), k_region);
    hmac_sha256(service_name.c_str(), service_name.length(), (const char *) k_region, sizeof(k_region), k_service);
    hmac_sha256("aws4_request", strlen("aws4_request"), (const char *) k_service, sizeof(k_service), k_signing_key);
    hmac_sha256(string_to_sign.c_str(), string_to_sign.length(), (const char *) k_signing_key, sizeof(k_signing_key), k_signature);

    signature = F("");
    for (int i = 0; i < sizeof(k_signature); ++i) {
        if (k_signature[i] == 0) {
            signature += "00";
        } else if (k_signature[i] <= 0x0F) {
            signature += "0";
            signature += String(k_signature[i], HEX);
        } else {
            signature += String(k_signature[i], HEX);
        }
    }
    return;
}

void SOC::aws_get_authorization_header(http_method method, String &canonical_uri, String &canonical_query_string, String &canonical_headers, String &signed_headers, String &hashed_payload, String &amz_date, String &secret_key, String &access_key_id, String &authorization_header)
{
    String canonical_request_data;
    canonical_request_data.reserve(20 + canonical_uri.length() + canonical_query_string.length() + canonical_headers.length() + signed_headers.length() + hashed_payload.length() + amz_date.length() + secret_key.length() + access_key_id.length() );

    switch (method) {
        case HTTP_GET: canonical_request_data = F("GET\n"); break;
        case HTTP_POST: canonical_request_data = F("POST\n"); break;
        default: canonical_request_data = F("GET\n"); break;
    }

    canonical_request_data += canonical_uri;
    canonical_request_data += F("\n");
    canonical_request_data += canonical_query_string;
    canonical_request_data += F("\n");
    canonical_request_data += canonical_headers;
    canonical_request_data += F("\n");
    canonical_request_data += signed_headers;
    canonical_request_data += F("\n");
    canonical_request_data += hashed_payload;


    String hashed_request_data = sha256(canonical_request_data);

    String credential_scope = amz_date.substring(0, 8) + F("/") + F(REGION) + F("/") + F(SERVICE) + F("/aws4_request");
    String string_to_sign = (String)F(ALGORITHM) + F("\n") + amz_date + F("\n") + credential_scope + F("\n") + hashed_request_data;
    String signature;
    signature.reserve(64);
    aws_get_signature(secret_key, amz_date.substring(0, 8), F(REGION), F(SERVICE), string_to_sign, signature);
    
    // authorization_header = (String)ALGORITHM + " " + "Credential=" + access_key_id + "/" + credential_scope + ", SignedHeaders=" + signed_headers + ", Signature=" + signature;
    authorization_header = F(ALGORITHM);
    authorization_header += " Credential="; 
    authorization_header += access_key_id;
    authorization_header += F("/");
    authorization_header += credential_scope;
    authorization_header += ", SignedHeaders=";
    authorization_header += signed_headers; 
    authorization_header += ", Signature=";
    authorization_header += signature;
}

void SOC::log_memory()
{
    multi_heap_info_t dram_info;
    heap_caps_get_info(&dram_info,  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    logger.printfln("Mem info: Total free: %u, Largest free: %u",dram_info.total_free_bytes, dram_info.largest_free_block);
}

void SOC::test_http_client()
{
    String url = "https://www.howsmyssl.com=";
    String payload;
    payload.reserve(220);
    std::vector<Header> headers;
    
    logger.printfln("Testing HTTP request with URL %s", url.c_str());
    int result = http_request(url, howsmyssl_com_root_cert_pem, HTTP_GET, &headers, &payload, &cookie_jar);

    logger.printfln("HTTP result: %d", result);
    log_memory();

}
