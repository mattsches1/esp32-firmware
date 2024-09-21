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

#pragma once

#include "config.h"
#include "web_server.h"

#include "module.h"
#include <HTTPClient.h>

#include "../meters/value_history.h"

#define EVSE_STOP_TIMEOUT 10000

class SOC final : public IModule
{
public:
    SOC(){}
    void pre_setup() override;
    void setup() override;
    void register_urls() override;

private:
    typedef enum {
        inactive = 0,
        init = 10,
        login1 = 20,
        login2 = 30,
        login3 = 40,
        login4 = 50,
        get_amz_date = 55,
        get_vehicles = 60,
        get_vehicle_status = 70,
        get_pin_auth = 80,
        deep_refresh = 90,
        waiting_for_evse_stop = 100
    } SequencerState;

    struct Header {
        String name;
        String value;
    };

    struct Vehicle {
        String vin;
        String make;
        String model_description;
        uint16_t year;
        String color;
    };

    typedef enum {
        not_connected = 0,
        waiting_for_charge_release = 1,
        ready_for_charging = 2,
        charging = 3,
        error = 4
    } ChargerState;

    void check_for_vin_list_completion();
    String get_vin_list();

    void sequencer();
    void sequencer_state_idle();
    void sequencer_state_init();
    void sequencer_state_login1();
    void sequencer_state_login1(CookieJar *cookie_jar);
    void sequencer_state_login2();
    void sequencer_state_login2(CookieJar *cookie_jar);
    void sequencer_state_login3();
    void sequencer_state_login4();
    void sequencer_state_get_amz_date();
    void sequencer_state_get_vehicles();
    void sequencer_state_get_vehicle_status();
    void sequencer_state_get_pin_auth();
    void sequencer_state_deep_refresh();
    void sequencer_state_waiting_for_evse_stop();

    void update_all_data();
    void update_history();
    float live_samples_per_second();

    int http_request(const String &url, const char* cert, http_method method, std::vector<Header> *headers, String *payload, CookieJar *cookie_jar, const char *headers_to_collect[]=nullptr, size_t num_headers_to_collect=0);
    int http_request(const String &url, const char* cert, http_method method, std::vector<Header> *headers, String *payload, CookieJar *cookie_jar, DynamicJsonDocument *response, const char *headers_to_collect[]=nullptr, size_t num_headers_to_collect=0);
    int http_request(const String &url, const char* cert, http_method method, std::vector<Header> *headers, String *payload, CookieJar *cookie_jar, StaticJsonDocument<400> &response, StaticJsonDocument<200> &filter);
    
    String sha256(String &payload);
    void hmac_sha256(const char* payload, size_t payload_length, const char* key, size_t key_length, unsigned char* hmac);
    String random_string(const int len);
    void aws_get_canonical_headers(String *canonical_headers, String *signed_headers, std::vector<Header> headers, String &url);
    void aws_get_signature(const String &secret_key, const String &date_stamp, const String &region_name, const String &service_name, const String &string_to_sign, String &signature);
    void aws_get_authorization_header(http_method method, String &canonical_uri, String &canonical_query_string, String &canonical_headers, String &signed_headers, String &hashed_payload, String &amz_date, String &secret_key, String &access_key_id, String &authorization_header);
    void log_memory();
    void test_http_client();

    bool debug = true;
    int debug_level = 0;

    bool login_ok = false, pin_auth_ok = false, soc_status_ok = false;
    bool api_login_retry = false, pin_auth_retry = false;
    bool busy = false;
    bool ignore_soc_limit_once = false;

    bool vin_list_requested = false;
    bool vin_list_request_active = false;
    bool vin_list_available = false;
    std::vector<Vehicle> vehicles;

    bool soc_requested = false;
    bool soc_request_active = false;

    SequencerState sequencer_state = SequencerState::inactive;
    std::mutex mutex;

    ConfigRoot state;
    ConfigRoot config;
    ConfigRoot config_in_use;
    ConfigRoot setpoint;
    ConfigRoot setpoint_update;
    ConfigRoot api_debug_data;

    bool enabled;
    uint8_t soc = 0;
    uint32_t last_request = 0;

    ChargerState charger_state;

    struct {
        struct {
            String uid;
            String login_token;
        } login1;
        struct {
            String id_token;
        } login2;
        struct {
            String identity_id;
            String token;
        } login3;
        struct {
            String access_key_id;
            String secret_key;
            String session_token;
        } login4;
        String pin_auth_token;
        String amz_date;
    } api_data;

    ValueHistory soc_history;

    size_t history_chars_per_value;
    uint32_t last_live_update = 0;
    uint32_t last_history_update = 0;
    uint32_t last_history_slot = UINT32_MAX;

    int samples_this_interval = 0;
    uint32_t begin_this_interval = 0;
    uint32_t end_this_interval = 0;

    int samples_last_interval = 0;
    uint32_t begin_last_interval = 0;
    uint32_t end_last_interval = 0;

    CookieJar cookie_jar;
};
                                    
