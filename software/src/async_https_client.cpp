/* esp32-firmware
 * Copyright (C) 2024 Matthias Bolte <matthias@tinkerforge.com>
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

#define EVENT_LOG_PREFIX "async_https_clnt"

#include "async_https_client.h"

#include "event_log_prefix.h"
#include "main_dependencies.h"

#define ASYNC_HTTPS_CLIENT_TIMEOUT 15000

extern "C" esp_err_t esp_crt_bundle_attach(void *conf);

AsyncHTTPSClient::~AsyncHTTPSClient()  {
    if (task_id != 0) {
        task_scheduler.cancel(task_id);
    }
    if (http_client != nullptr) {
        esp_http_client_close(http_client);
        esp_http_client_cleanup(http_client);
    }
}

esp_err_t AsyncHTTPSClient::event_handler(esp_http_client_event_t *event)
{
    AsyncHTTPSClient *that = static_cast<AsyncHTTPSClient *>(event->user_data);
    AsyncHTTPSClientEvent async_event;
    int http_status;

    switch (event->event_id) {
    case HTTP_EVENT_ERROR:
        async_event.type = AsyncHTTPSClientEventType::Error;
        async_event.error = AsyncHTTPSClientError::HTTPError;
        async_event.error_http_client = ESP_OK;
        async_event.error_http_status = -1;

        that->callback(&async_event);
        break;

    case HTTP_EVENT_ON_HEADER:
        if (!that->use_cookies) {
            break;
        }
        for (int i = 0; event->header_key[i] != 0; i++) {
            event->header_key[i] = tolower(event->header_key[i]);
        }
        if (!strcmp("set-cookie", event->header_key)) {
            that->parse_cookie(event->header_value);
        }
        if (that->complete_len == -1) {
            that->complete_len = (ssize_t)esp_http_client_get_content_length(that->http_client);
        }
        break;

    case HTTP_EVENT_ON_DATA:
        that->last_async_alive = millis();
        http_status = esp_http_client_get_status_code(that->http_client);

        if (http_status != 200) {
            that->in_progress = false;

            async_event.type = AsyncHTTPSClientEventType::Error;
            async_event.error = AsyncHTTPSClientError::HTTPStatusError;
            async_event.error_http_client = ESP_OK;
            async_event.error_http_status = http_status;

            that->callback(&async_event);
            break;
        }

        if (that->received_len == 0) {
            that->complete_len = (ssize_t)esp_http_client_get_content_length(that->http_client);
        }

        async_event.type = AsyncHTTPSClientEventType::Data;
        async_event.data_chunk_offset = that->received_len;
        async_event.data_chunk = event->data;
        async_event.data_chunk_len = event->data_len;
        async_event.data_complete_len = that->complete_len;
        async_event.data_remaining_len = that->complete_len - that->received_len - event->data_len;

        that->received_len += event->data_len;

        that->callback(&async_event);
        break;

    default:
        break;
    }

    return ESP_OK;
}

static const char *https_prefix = "https://";
static const size_t https_prefix_len = strlen(https_prefix);

void AsyncHTTPSClient::fetch(const char *url, int cert_id, esp_http_client_method_t method, const char *body, int body_size, std::function<void(AsyncHTTPSClientEvent *event)> callback) {
    AsyncHTTPSClientEvent async_event;

    if (strncmp(url, https_prefix, https_prefix_len) != 0) {
        error_abort(async_event, AsyncHTTPSClientError::NoHTTPSURL);
        return;
    }

    if (in_progress) {
        error_abort(async_event, AsyncHTTPSClientError::Busy);
        return;
    }

    this->callback = callback;
    in_progress = true;
    abort_requested = false;
    received_len = 0;
    complete_len = -1;
    if (body != nullptr) {
        owned_body = String(body, body_size);
    }

    esp_http_client_config_t http_config = {};

    http_config.method = method;
    http_config.url = url;
    http_config.event_handler = event_handler;
    http_config.user_data = this;
    http_config.is_async = true;
    http_config.timeout_ms = 50;
    http_config.buffer_size = 1024;
    http_config.buffer_size_tx = 1024;

    if (cert_id < 0) {
        http_config.crt_bundle_attach = esp_crt_bundle_attach;
    }
    else {
#if MODULE_CERTS_AVAILABLE()
        size_t cert_len = 0;
        cert = certs.get_cert(static_cast<uint8_t>(cert_id), &cert_len);

        if (cert == nullptr) {
            error_abort(async_event, AsyncHTTPSClientError::NoCert);
            return;
        }

        http_config.cert_pem = (const char *)cert.get();
        // http_config.skip_cert_common_name_check = true;
#else
        // defense in depth: it should not be possible to arrive here because in case
        // that the certs module is not available the cert_id should always be -1
        logger.printfln("Can't use custom certificate: certs module is not built into this firmware!");

        error_abort(async_event, AsyncHTTPSClientError::NoCert);
        return;
#endif
    }

    http_client = esp_http_client_init(&http_config);

    if (http_client == nullptr) {
        error_abort(async_event, AsyncHTTPSClientError::HTTPClientInitFailed);
        return;
    }

    if (owned_body.length() > 0 && esp_http_client_set_post_field(http_client, owned_body.c_str(), owned_body.length())) {
        error_abort(async_event, AsyncHTTPSClientError::HTTPClientSetBodyFailed);
        return;
    }

    if (cookies.length() > 0) {
        if (esp_http_client_set_header(http_client, "cookie", cookies.c_str()) != ESP_OK) {
            error_abort(async_event, AsyncHTTPSClientError::HTTPClientSetCookieFailed);
            return;
        }
    }
    if (headers.size() > 0) {
        for (std::pair<String, String> header : headers) {
            if (esp_http_client_set_header(http_client, header.first.c_str(), header.second.c_str()) != ESP_OK) {
                error_abort(async_event, AsyncHTTPSClientError::HTTPClientSetCookieFailed);
                return;
            }
        }
    }

    last_async_alive = millis();

    task_id = task_scheduler.scheduleWithFixedDelay([this]() {
        bool no_response = false;
        bool short_read = false;
        esp_err_t err = ESP_OK;

        if (!abort_requested) {
            if (in_progress && deadline_elapsed(last_async_alive + ASYNC_HTTPS_CLIENT_TIMEOUT)) {
                in_progress = false;
                no_response = true;
            }

            if (in_progress) {
                err = esp_http_client_perform(http_client);
                if (!abort_requested && in_progress) {
                    if (err == ESP_ERR_HTTP_EAGAIN || err == ESP_ERR_HTTP_FETCH_HEADER) {
                        return;
                    }

                    if (err == ESP_OK && (complete_len == -1 || received_len != complete_len)) {
                        in_progress = false;
                        short_read = true;
                    }
                }
            }
        }

        if (http_client != nullptr) {
            esp_http_client_close(http_client);
            esp_http_client_cleanup(http_client);
            http_client = nullptr;
        }
        owned_body = String();

        cert.reset();

        AsyncHTTPSClientEvent inner_async_event;

        if (abort_requested) {
            inner_async_event.type = AsyncHTTPSClientEventType::Aborted;

            this->callback(&inner_async_event);
        }
        else if (no_response) {
            inner_async_event.type = AsyncHTTPSClientEventType::Error;
            inner_async_event.error = AsyncHTTPSClientError::NoResponse;
            inner_async_event.error_http_client = ESP_OK;
            inner_async_event.error_http_status = -1;
            headers = std::vector<std::pair<String, String>>();

            this->callback(&inner_async_event);
        }
        else if (short_read) {
            inner_async_event.type = AsyncHTTPSClientEventType::Error;
            inner_async_event.error = AsyncHTTPSClientError::ShortRead;
            inner_async_event.error_http_client = ESP_OK;
            inner_async_event.error_http_status = -1;
            headers = std::vector<std::pair<String, String>>();

            this->callback(&inner_async_event);
        }
        else if (err != ESP_OK) {
            in_progress = false;
            inner_async_event.type = AsyncHTTPSClientEventType::Error;
            inner_async_event.error = AsyncHTTPSClientError::HTTPClientError;
            inner_async_event.error_http_client = err;
            inner_async_event.error_http_status = -1;
            headers = std::vector<std::pair<String, String>>();

            this->callback(&inner_async_event);
        }
        else if (in_progress) {
            inner_async_event.type = AsyncHTTPSClientEventType::Finished;

            this->callback(&inner_async_event);
        }

        in_progress = false;
        this->headers = std::vector<std::pair<String,String>>();

        task_scheduler.cancel(task_scheduler.currentTaskId());
        task_id = 0;
    }, 0, 200);
}

void AsyncHTTPSClient::download_async(const char *url, int cert_id, std::function<void(AsyncHTTPSClientEvent *event)> callback)
{
    fetch(url, cert_id, HTTP_METHOD_GET, nullptr, 0, callback);
}

void AsyncHTTPSClient::post_async(const char *url, int cert_id, const char *body, int body_size, std::function<void(AsyncHTTPSClientEvent *event)> callback) {
    fetch(url, cert_id, HTTP_METHOD_POST, body, body_size, callback);
}

void AsyncHTTPSClient::put_async(const char *url, int cert_id, const char *body, int body_size, std::function<void(AsyncHTTPSClientEvent *event)> callback) {
    fetch(url, cert_id, HTTP_METHOD_PUT, body, body_size, callback);
}

void AsyncHTTPSClient::delete_async(const char *url, int cert_id, const char *body, int body_size, std::function<void(AsyncHTTPSClientEvent *event)> callback) {
    fetch(url, cert_id, HTTP_METHOD_DELETE, body, body_size, callback);
}

void AsyncHTTPSClient::set_header(const char *key, const char *value) {
    if (key == nullptr || value == nullptr) {
        return;
    }

    String k = key;
    String v = value;
    headers.push_back(std::pair<String, String>(k, v));
}

void AsyncHTTPSClient::error_abort(AsyncHTTPSClientEvent &async_event, AsyncHTTPSClientError reason) {
    cert.reset();

    in_progress = false;

    async_event.type = AsyncHTTPSClientEventType::Error;
    async_event.error = reason;
    async_event.error_http_client = ESP_OK;
    async_event.error_http_status = -1;
    if (http_client != nullptr) {
        esp_http_client_close(http_client);
        esp_http_client_cleanup(http_client);
        http_client = nullptr;
    }
    owned_body = String();
    headers = std::vector<std::pair<String, String>>();

    callback(&async_event);
}

void AsyncHTTPSClient::parse_cookie(const char *cookie) {
    char *i = strchr(cookie, ';');
    if (i != nullptr) {
        *i = 0;
    }
    cookies += String(cookie) + ';';
}

void AsyncHTTPSClient::abort_async()
{
    abort_requested = true;
}