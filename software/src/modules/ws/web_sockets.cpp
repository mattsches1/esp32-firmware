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

#include "web_sockets.h"

#include "event_log_prefix.h"
#include "generated/module_dependencies.h"
#include "rollback_timing.h"
#include "tools.h"
#include "tools/net.h"
#include "tools/memory.h"
#include "esp_httpd_priv.h"

#include "gcc_warnings.h"

static constexpr micros_t KEEP_ALIVE_TIMEOUT = 10_s;
static constexpr int BROADCAST_FD = -2;

static bool format_socket_peer(int fd, char *peer, size_t peer_size, int *peer_errno)
{
    struct sockaddr_storage addr = {};
    socklen_t addr_len = sizeof(addr);

    if (getpeername(fd, reinterpret_cast<struct sockaddr *>(&addr), &addr_len) != 0) {
        *peer_errno = errno;
        snprintf(peer, peer_size, "unknown");
        return false;
    }

    char ip[INET6_ADDRSTRLEN] = {};
    tf_ipaddr_ntoa(&addr, ip, sizeof(ip));

    if (addr.ss_family == AF_INET) {
        const auto *addr4 = reinterpret_cast<const struct sockaddr_in *>(&addr);
        snprintf(peer, peer_size, "%s:%u", ip, static_cast<unsigned>(ntohs(addr4->sin_port)));
    } else if (addr.ss_family == AF_INET6) {
        const auto *addr6 = reinterpret_cast<const struct sockaddr_in6 *>(&addr);
        snprintf(peer, peer_size, "[%s]:%u", ip, static_cast<unsigned>(ntohs(addr6->sin6_port)));
    } else {
        snprintf(peer, peer_size, "unknown-family-%u", static_cast<unsigned>(addr.ss_family));
    }

    *peer_errno = 0;
    return true;
}

static bool should_log_websocket_anomaly()
{
    static micros_t last_log = 0_us;
    const micros_t now = now_us();

    if (last_log != 0_us && now - last_log < 10_s) {
        return false;
    }

    last_log = now;
    return true;
}

static int timed_httpd_send(httpd_handle_t hd, int fd, const char *buf, size_t len, int flags)
{
    const micros_t send_started = now_us();
    errno = 0;
    const int send_result = httpd_default_send(hd, fd, buf, len, flags);
    const int send_errno = errno;
    const uint64_t send_duration_ms = (now_us() - send_started).to<millis_t>().as<uint64_t>();
    int result = send_result;

    if (send_result < 0) {
        result = send_errno == EAGAIN || send_errno == EINTR
            ? HTTPD_SOCK_ERR_TIMEOUT
            : HTTPD_SOCK_ERR_FAIL;
    }

    if ((result < 0 || send_duration_ms >= 100) && should_log_websocket_anomaly()) {
        char peer[INET6_ADDRSTRLEN + 16];
        int peer_errno = 0;
        format_socket_peer(fd, peer, sizeof(peer), &peer_errno);
        logger.printfln("Unusual HTTPD transport send: fd %i peer %s (peer errno %i), duration %llums, bytes %zu, result %i, errno %i",
                        fd,
                        peer,
                        peer_errno,
                        send_duration_ms,
                        len,
                        result,
                        send_errno);
    }

    errno = send_errno;
    return result;
}

static void clear_ws_work_item(ws_work_item *wi)
{
    free(wi->payload);
    wi->payload = nullptr;
}

// Called by HTTP thread.
bool WebSockets::haveWork(ws_work_item *item)
{
    std::lock_guard<std::recursive_mutex> lock{work_queue_mutex};

    if (work_queue.empty())
        return false;

    *item = work_queue.front();
    work_queue.pop_front();
    return true;
}

// Called by any thread.
void WebSockets::cleanUpQueue()
{
    std::lock_guard<std::recursive_mutex> lock{work_queue_mutex};
    while (!work_queue.empty()) {
        ws_work_item *wi = &work_queue.front();
        if (wi->unicast_fd != -1) {
            return;
        }
        clear_ws_work_item(wi);
        // unicast_fd was -1.
        work_queue.pop_front();
    }
}

// Called by any thread.
bool WebSockets::queueFull()
{
    std::lock_guard<std::recursive_mutex> lock{work_queue_mutex};
    if (work_queue.size() < MAX_WEB_SOCKET_WORK_ITEMS_IN_QUEUE) {
        return false;
    }

    cleanUpQueue();

    if (work_queue.size() >= MAX_WEB_SOCKET_WORK_ITEMS_IN_QUEUE) {
        return true;
    }
    // Print only to the console because printing to the event log
    // would generate more websocket messages to fill up the queue.
    printf("web_sockets: Work queue was full but %u items were cleaned.\n", MAX_WEB_SOCKET_WORK_ITEMS_IN_QUEUE - work_queue.size());

    return false;
}

// Send a payload to a single destination. If a write fails, return false and let the callers handle session closing.
// This function is intended to be used from within the initial WS request and thus cannot close the WS session directly.
// Closing the WS session directly from within the initial WS request would leak the WebSocketClient object because the session context isn't freed.
// Called by HTTP thread.
bool WebSockets::send_ws_item_direct(int fd, httpd_ws_frame *ws_pkt)
{
    const httpd_ws_client_info_t ws_client_info = httpd_ws_get_fd_info(this->httpd, fd);

    if (ws_client_info != HTTPD_WS_CLIENT_WEBSOCKET) {
        logger.tracefln(server.get_trace_buffer_index(), "send_ws_item_direct non-WS fd %i type %u", fd, static_cast<unsigned>(ws_client_info));
        return false;
    }

    uint64_t pong_age_before_ms = 0;
    const bool pong_before_known = this->getLastPongAgeMs(fd, &pong_age_before_ms);
    char peer[INET6_ADDRSTRLEN + 16];
    int peer_errno = 0;
    format_socket_peer(fd, peer, sizeof(peer), &peer_errno);
    const micros_t send_started = now_us();
    errno = 0;
    const esp_err_t err = httpd_ws_send_frame_async(this->httpd, fd, ws_pkt);
    const int send_errno = errno;
    const uint64_t send_duration_ms = (now_us() - send_started).to<millis_t>().as<uint64_t>();

    if ((err != ESP_OK || send_duration_ms >= 100) && should_log_websocket_anomaly()) {
        uint64_t pong_age_after_ms = 0;
        const bool pong_after_known = this->getLastPongAgeMs(fd, &pong_age_after_ms);
        logger.printfln("Unusual WebSocket send: fd %i peer %s (peer errno %i), duration %llums, pong before %s%llums, after %s%llums, payload %zu bytes, result 0x%x, errno %i",
                        fd,
                        peer,
                        peer_errno,
                        send_duration_ms,
                        pong_before_known ? "" : "unknown/",
                        pong_age_before_ms,
                        pong_after_known ? "" : "unknown/",
                        pong_age_after_ms,
                        ws_pkt->len,
                        static_cast<unsigned>(err),
                        err == ESP_OK ? 0 : send_errno);
    }

    return err == ESP_OK;
}

// Send a work item to multiple destinations. If a write fails, close the WS session.
// Unlike send_ws_item_direct, this function is only allowed to be called after the initial WS request has finished.
// Calling this function from within the initial WS request would leak the WebSocketClient object because the session context isn't freed.
// Called by HTTP thread.
bool WebSockets::send_ws_work_item(const ws_work_item *wi)
{
    httpd_ws_frame ws_pkt = {
        .final      = true, // Doesn't matter, frame is not fragmented.
        .fragmented = false,
        .type       = wi->ws_type,
        .payload    = reinterpret_cast<uint8_t *>(wi->payload),
        .len        = wi->payload_len,
    };

    int fd_count;
    int fds[MAX_WEB_SOCKET_CLIENTS];

    if (wi->unicast_fd == BROADCAST_FD) {
        fd_count = MAX_WEB_SOCKET_CLIENTS;
        std::lock_guard<std::recursive_mutex> lock{keep_alive_mutex};
        memcpy(fds, keep_alive_fds, sizeof(fds));
    } else {
        fd_count = 1;
        fds[0] = wi->unicast_fd;
    }

    bool result = true;

    for (int i = 0; i < fd_count; ++i) {
        if (fds[i] == -1) {
            continue;
        }

        const httpd_ws_client_info_t ws_client_info = httpd_ws_get_fd_info(this->httpd, fds[i]);

        if (ws_client_info != HTTPD_WS_CLIENT_WEBSOCKET) {
            logger.tracefln(server.get_trace_buffer_index(), "send_ws_work_item non-WS fd %i type %u", fds[i], static_cast<unsigned>(ws_client_info));
            continue;
        }

        uint64_t pong_age_before_ms = 0;
        const bool pong_before_known = this->getLastPongAgeMs(fds[i], &pong_age_before_ms);
        char peer[INET6_ADDRSTRLEN + 16];
        int peer_errno = 0;
        format_socket_peer(fds[i], peer, sizeof(peer), &peer_errno);
        const micros_t send_started = now_us();
        errno = 0;
        const esp_err_t err = httpd_ws_send_frame_async(this->httpd, fds[i], &ws_pkt);
        const int send_errno = errno;
        const uint64_t send_duration_ms = (now_us() - send_started).to<millis_t>().as<uint64_t>();

        if ((err != ESP_OK || send_duration_ms >= 100) && should_log_websocket_anomaly()) {
            uint64_t pong_age_after_ms = 0;
            const bool pong_after_known = this->getLastPongAgeMs(fds[i], &pong_age_after_ms);
            logger.printfln("Unusual WebSocket send: fd %i peer %s (peer errno %i), duration %llums, pong before %s%llums, after %s%llums, payload %zu bytes, result 0x%x, errno %i",
                            fds[i],
                            peer,
                            peer_errno,
                            send_duration_ms,
                            pong_before_known ? "" : "unknown/",
                            pong_age_before_ms,
                            pong_after_known ? "" : "unknown/",
                            pong_age_after_ms,
                            ws_pkt.len,
                            static_cast<unsigned>(err),
                            err == ESP_OK ? 0 : send_errno);
        }

        if (err != ESP_OK) {
            logger.tracefln(server.get_trace_buffer_index(), "send_ws_work_item close fd %i, err 0x%x", fds[i], static_cast<unsigned>(err));
            this->keepAliveCloseDead_HTTPThread(fds[i]);
            result = false;
        }
    }

    return result;
}

// Called by HTTP thread.
void WebSockets::processPendingCloses_HTTPThread()
{
    // Drain the pending close queue. This runs on the HTTP thread,
    // so it is safe to call httpd_ws_get_fd_info and httpd_sess_delete here.
    ws_pending_close closes_to_process[MAX_WEB_SOCKET_CLIENTS];
    size_t count;
    {
        std::lock_guard<std::recursive_mutex> lock{work_queue_mutex};
        if (pending_close_count == 0) {
            return;
        }
        count = pending_close_count;
        memcpy(closes_to_process, pending_closes, count * sizeof(ws_pending_close));
        pending_close_count = 0;
    }

    for (size_t i = 0; i < count; ++i) {
        logger.tracefln(server.get_trace_buffer_index(), "close pending fd %i", closes_to_process[i].fd);
        this->keepAliveCloseDead_HTTPThread(closes_to_process[i].fd,
                                            closes_to_process[i].last_pong_known,
                                            closes_to_process[i].last_pong_age_ms);
    }
}

// Called by HTTP thread.
void WebSockets::work(void *arg)
{
    WebSockets *ws = static_cast<WebSockets *>(arg);
    ws->worker_active = WEBSOCKET_WORKER_RUNNING;
    const uint64_t worker_started_us = now_us().as<uint64_t>();
    const uint64_t enqueued_at_us = ws->worker_enqueued_at_us.exchange(0, std::memory_order_relaxed);
    const uint64_t queue_delay_ms = enqueued_at_us == 0 ? 0 : (worker_started_us - enqueued_at_us) / 1000;

    // Check and close connections before sending any data.
    ws->processPendingCloses_HTTPThread();
    ws->checkActiveClients();

    ws_work_item wi;
    size_t work_item_count = 0;
    while (ws->haveWork(&wi)) {
        ws->send_ws_work_item(&wi);
        clear_ws_work_item(&wi);
        ++work_item_count;
    }

    ws->worker_active = WEBSOCKET_WORKER_DONE;
    const uint64_t worker_duration_ms = (now_us().as<uint64_t>() - worker_started_us) / 1000;
    if ((queue_delay_ms >= 100 || worker_duration_ms >= 1000) && should_log_websocket_anomaly()) {
        logger.printfln("Unusual WebSocket worker timing: queue delay %llums, execution %llums, work items %zu",
                        queue_delay_ms,
                        worker_duration_ms,
                        work_item_count);
    }
#if MODULE_WATCHDOG_AVAILABLE()
    watchdog.reset(ws->watchdog_handle);
#endif
}

// Called by HTTP thread.
esp_err_t WebSockets::ws_handler(httpd_req_t *req)
{
    uint8_t small_payload_buffer[4]; // Close frames usually have two bytes payload.
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    // First receive the full ws message
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        //logger.printfln("httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }

    if (ws_pkt.len > 0) {
        // Buffer 1 byte larger so that null termination can be added for text frames

        if (ws_pkt.len < std::size(small_payload_buffer)) {
            ws_pkt.payload = small_payload_buffer;
        } else {
            ws_pkt.payload = static_cast<uint8_t *>(malloc(ws_pkt.len + 1));
            if (ws_pkt.payload == nullptr) {
                logger.printfln("Failed to allocate payload buffer");
                return ESP_ERR_NO_MEM;
            }
        }
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            //logger.printfln("httpd_ws_recv_frame failed with %d", ret);
            if (ws_pkt.payload != small_payload_buffer) {
                free(ws_pkt.payload);
            }
            ws_pkt.payload = nullptr;
            return ret;
        }
    }

    WebSocketsClient *client = static_cast<WebSocketsClient *>(req->sess_ctx);
    httpd_ws_type_t frame_type;

    if (ws_pkt.type == HTTPD_WS_TYPE_CONTINUE) {
        frame_type = client->last_received_frame_type;
    } else {
        frame_type = ws_pkt.type;
        client->last_received_frame_type = frame_type;
    }

    if (frame_type == HTTPD_WS_TYPE_PING) {
        // We use a patched version of esp-idf that does not handle ping frames in a strange way.
        // We have to send the pong ourselves.
        ws_pkt.type = HTTPD_WS_TYPE_PONG;
        httpd_ws_send_frame(req, &ws_pkt);
    } else if (frame_type == HTTPD_WS_TYPE_PONG) {
        // If it was a PONG, update the keep-alive
        WebSockets *ws = static_cast<WebSockets *>(req->user_ctx);
        ws->receivedPong(httpd_req_to_sockfd(req));
    } else if (frame_type == HTTPD_WS_TYPE_TEXT) {
        // If it was a TEXT frame, pass it to the text data handler.
        if (ws_pkt.len == 0) {
            logger.printfln("Ignoring zero-length text frame");
        } else {
            ws_pkt.payload[ws_pkt.len] = 0;
            WebSockets *ws = static_cast<WebSockets *>(req->user_ctx);
            if (ws->on_text_data_received_fn != nullptr) {
                ws->on_text_data_received_fn(client, &ws_pkt);
            } else {
                logger.printfln("Ignoring received text frame with message: \"%.20s\" (web sockets are unidirectional for now)", ws_pkt.payload);
            }
        }
    } else if (frame_type == HTTPD_WS_TYPE_BINARY) {
        // If it was a binary data frame, pass it to the binary data handler.
        WebSockets *ws = static_cast<WebSockets *>(req->user_ctx);
        if (ws->on_binary_data_received_fn != nullptr) {
            ws->on_binary_data_received_fn(client, &ws_pkt);
        } else {
            logger.printfln("Ignoring received binary frame containing %zu bytes", ws_pkt.len);
        }
    } else if (frame_type == HTTPD_WS_TYPE_CLOSE) {
        // If it was a CLOSE, remove it from the keep-alive list
        WebSockets *ws = static_cast<WebSockets *>(req->user_ctx);
        const int fd = httpd_req_to_sockfd(req);
        ws->keepAliveRemove(fd);

        logger.tracefln(server.get_trace_buffer_index(), "WS close fd %i", fd);

        // Abuse the ws_control_frames flag to indicate that we received a close frame.
        // Any socket that still has both ws_handshake_done and ws_control_frames set to true on close wasn't closed cleanly.
        struct httpd_req_aux *aux = static_cast<struct httpd_req_aux *>(req->aux);
        aux->sd->ws_control_frames = false;

        if (ws->on_client_disconnect_fn != nullptr) {
            ws->on_client_disconnect_fn(client, true);
        }
    } else {
        logger.printfln("Received unexpected frame type=%u final=%i len=%zu", static_cast<unsigned>(ws_pkt.type), ws_pkt.final, ws_pkt.len);
    }

    if (ws_pkt.payload != nullptr) {
        if (ws_pkt.payload != small_payload_buffer) {
            free(ws_pkt.payload);
        }

        ws_pkt.payload = nullptr;
    }

    return ESP_OK;
}

// Called by HTTP thread.
void WebSockets::keepAliveAdd(int fd)
{
    std::lock_guard<std::recursive_mutex> lock{keep_alive_mutex};
    for (int i = 0; i < MAX_WEB_SOCKET_CLIENTS; ++i) {
        if (keep_alive_fds[i] == fd) {
            logger.tracefln(server.get_trace_buffer_index(), "keepAliveAdd fd %i reused", fd);
            // fd is already in the keep alive array. Only update last_pong to prevent instantly closing the new connection.
            // This can happen if web sockets are opened and closed rapidly (so that LWIP "reuses" the fd) and we miss a close frame.
            keep_alive_last_pong[i] = now_us();
            return;
        }
    }

    for (int i = 0; i < MAX_WEB_SOCKET_CLIENTS; ++i) {
        if (keep_alive_fds[i] == -1) {
            keep_alive_fds[i] = fd;
            keep_alive_last_pong[i] = now_us();
            return;
        }
    }
}

// Called by any thread.
void WebSockets::keepAliveRemove(int fd)
{
    {
        std::lock_guard<std::recursive_mutex> lock{keep_alive_mutex};
        for (size_t i = 0; i < MAX_WEB_SOCKET_CLIENTS; ++i) {
            if (keep_alive_fds[i] == fd) {
                keep_alive_fds[i] = -1;
                keep_alive_last_pong[i] = 0_us;
                break;
            }
        }
    }

    {
        std::lock_guard<std::recursive_mutex> lock{work_queue_mutex};
        size_t work_queue_size = work_queue.size();
        for (size_t i = 0; i < work_queue_size; ++i) {
            if (work_queue[i].unicast_fd == fd) {
                work_queue[i].unicast_fd = -1;
                // Removing items from somewhere in the work queue is inefficient and would mess with the iteration.
                // Leave it there and let the worker clean it up.
            }
        }
    }
}

// Called by any thread.
void WebSockets::keepAliveCloseDead_async(int fd)
{
    uint64_t last_pong_age_ms = 0;
    const bool last_pong_known = this->getLastPongAgeMs(fd, &last_pong_age_ms);
    this->keepAliveRemove(fd);

    logger.tracefln(server.get_trace_buffer_index(), "closeDead async %i", fd);

    // Enqueue the fd for closing on the HTTP thread.
    // The fd-validity and fd-reuse checks (httpd_ws_get_fd_info) must not
    // be called from non-HTTP threads because they access httpd's internal
    // sock_db without synchronization. The HTTP thread will perform these
    // checks in processPendingCloses_HTTPThread() before actually closing.
    {
        std::lock_guard<std::recursive_mutex> lock{work_queue_mutex};
        if (pending_close_count < MAX_WEB_SOCKET_CLIENTS) {
            pending_closes[pending_close_count++] = {fd, last_pong_age_ms, last_pong_known};
        }
    }
}

// Called by HTTP thread.
void WebSockets::keepAliveCloseDead_HTTPThread(int fd, bool last_pong_known, uint64_t last_pong_age_ms)
{
    if (fd < 0) {
        return;
    }

    if (!last_pong_known) {
        last_pong_known = this->getLastPongAgeMs(fd, &last_pong_age_ms);
    }
    this->keepAliveRemove(fd);

    // Don't kill this socket if it is a HTTP socket:
    // Sometimes a fd is reused so fast that the keep alive does not notice
    // the closed fd before it is reopened as normal HTTP connection.
    const httpd_ws_client_info_t ws_client_info = httpd_ws_get_fd_info(httpd, fd);

    if (ws_client_info == HTTPD_WS_CLIENT_HTTP) {
        logger.tracefln(server.get_trace_buffer_index(), "keepAliveCloseDead_HTTPThread fd %i reused for HTTP", fd);
        return;
    }

    if (ws_client_info == HTTPD_WS_CLIENT_INVALID) {
        logger.tracefln(server.get_trace_buffer_index(), "keepAliveCloseDead_HTTPThread invalid fd %i with no associated session", fd);
        return;
    }

    // Don't log normal close because the caller already does.

    // This old comment might not be relevant anymore:
    // Seems like we have to do everything by ourselves...
    // In the case that the client is really dead (for example: someone pulled the ethernet cable)
    // We manually have to delete the session, thus closing the fd.
    // If we don't do this, httpd_get_client_list will still return the fd
    // and httpd_ws_get_fd_info will return that this fd is active.
    // We then will attempt to send data to the fd, running into a timeout.
    // The httpd task will then block forever, as other tasks will generate data
    // faster than we are able to throw it away via send timeouts.
    // Unfortunately there is no API to throw away a web socket connection, so
    // we have to poke around in the internal structures here.
    struct httpd_data *hd = static_cast<struct httpd_data *>(httpd);

    struct sock_db *current = hd->hd_sd;
    struct sock_db *end = hd->hd_sd + hd->config.max_open_sockets - 1;

    while (current <= end) {
        if (current->fd == fd) {
            const micros_t close_started = now_us();
            httpd_sess_delete(hd, current);
            const uint64_t close_duration_ms = (now_us() - close_started).to<millis_t>().as<uint64_t>();

            if (close_duration_ms >= 100 && should_log_websocket_anomaly()) {
                if (last_pong_known) {
                    logger.printfln("Slow WebSocket session deletion: fd %i, duration %llums, last pong %llums ago",
                                    fd,
                                    close_duration_ms,
                                    last_pong_age_ms);
                } else {
                    logger.printfln("Slow WebSocket session deletion: fd %i, duration %llums, last pong unknown",
                                    fd,
                                    close_duration_ms);
                }
            }
            break;
        }
        current++;
    }

    // Sometimes the deletion is not complete, but leaves an invalid socket. Also remove those.
    httpd_sess_delete_invalid(hd);
}

// Called by main thread.
void WebSockets::pingActiveClients()
{
    if (!this->haveActiveClient())
        return;

    std::lock_guard<std::recursive_mutex> lock{work_queue_mutex};
    if (queueFull()) {
        return;
    }

    work_queue.push_back({BROADCAST_FD, nullptr, 0, HTTPD_WS_TYPE_PING});
}

// Called by HTTP thread.
void WebSockets::checkActiveClients()
{
    ws_pending_close dead_clients[MAX_WEB_SOCKET_CLIENTS];
    size_t dead_count = 0;

    {
        std::lock_guard<std::recursive_mutex> lock{keep_alive_mutex};
        const micros_t now = now_us();
        for (int i = 0; i < MAX_WEB_SOCKET_CLIENTS; ++i) {
            if (keep_alive_fds[i] == -1) {
                continue;
            }

            if (deadline_elapsed(keep_alive_last_pong[i] + KEEP_ALIVE_TIMEOUT)) {
                dead_clients[dead_count++] = {
                    keep_alive_fds[i],
                    (now - keep_alive_last_pong[i]).to<millis_t>().as<uint64_t>(),
                    true,
                };
                keep_alive_fds[i] = -1;
                keep_alive_last_pong[i] = 0_us;
            }
        }
    }

    // Close outside the lock to avoid blocking other keep-alive operations.
    for (size_t i = 0; i < dead_count; ++i) {
        this->keepAliveCloseDead_HTTPThread(dead_clients[i].fd,
                                            dead_clients[i].last_pong_known,
                                            dead_clients[i].last_pong_age_ms);
    }
}

// Called by HTTP thread.
void WebSockets::closeLRUClient_HTTPThread()
{
    int fd_to_close = -1;
    uint64_t last_pong_age_ms = 0;

    {
        std::lock_guard<std::recursive_mutex> lock{keep_alive_mutex};
        int idx_to_close = -1;

        for (int i = 0; i < MAX_WEB_SOCKET_CLIENTS; ++i) {
            if (keep_alive_fds[i] == -1) {
                return; //Found free slot
            }

            const httpd_ws_client_info_t type = httpd_ws_get_fd_info(httpd, keep_alive_fds[i]);

            if  (type != HTTPD_WS_CLIENT_WEBSOCKET) {
                // Found non-websocket fd.
                // Probably was a websocket, was then closed
                // and re-opened as non-websocket-fd.
                logger.tracefln(server.get_trace_buffer_index(), "closeLRUC fd %i reused for HTTP, type %u", keep_alive_fds[i], static_cast<unsigned>(type));
                idx_to_close = i;
                break;
            }
        }

        if (idx_to_close == -1) {
            idx_to_close = 0;
            for (int i = 1; i < MAX_WEB_SOCKET_CLIENTS; ++i) {
                if (keep_alive_last_pong[i] < keep_alive_last_pong[idx_to_close]) {
                    idx_to_close = i;
                }
            }

            logger.tracefln(server.get_trace_buffer_index(), "closeLRUC fd %i", keep_alive_fds[idx_to_close]);
        }

        fd_to_close = keep_alive_fds[idx_to_close];
        last_pong_age_ms = (now_us() - keep_alive_last_pong[idx_to_close]).to<millis_t>().as<uint64_t>();
        keep_alive_fds[idx_to_close] = -1;
        keep_alive_last_pong[idx_to_close] = 0_us;
    }

    // Close outside the lock to avoid blocking other keep-alive operations.
    this->keepAliveCloseDead_HTTPThread(fd_to_close, true, last_pong_age_ms);
}

// Called by HTTP thread.
void WebSockets::receivedPong(int fd)
{
    std::lock_guard<std::recursive_mutex> lock{keep_alive_mutex};

    for (int i = 0; i < MAX_WEB_SOCKET_CLIENTS; ++i) {
        if (keep_alive_fds[i] == fd) {
            keep_alive_last_pong[i] = now_us();
            break;
        }
    }
}

// Called by HTTP thread.
void WebSockets::fakeReceivedPongAll()
{
    std::lock_guard<std::recursive_mutex> lock{keep_alive_mutex};

    const micros_t t_now = now_us();

    for (int i = 0; i < MAX_WEB_SOCKET_CLIENTS; ++i) {
        if (keep_alive_fds[i] != -1) {
            keep_alive_last_pong[i] = t_now;
        }
    }
}

// Called by main thread.
bool WebSockets::sendToClient(const char *payload, size_t payload_len, int fd, httpd_ws_type_t ws_type)
{
    char *payload_copy = static_cast<char *>(malloc(payload_len * sizeof(char)));
    if (payload_copy == nullptr) {
        return false;
    }

    memcpy(payload_copy, payload, payload_len);

    return sendToClientOwned(payload_copy, payload_len, fd, ws_type);
}

// Called by main thread.
bool WebSockets::sendToClientOwned(char *payload, size_t payload_len, int fd, httpd_ws_type_t ws_type)
{
    std::lock_guard<std::recursive_mutex> lock{work_queue_mutex};
    if (queueFull()) {
        free(payload);
        return false;
    }

    work_queue.push_back({fd, payload, payload_len, ws_type});
    return true;
}

// Called by any thread.
bool WebSockets::haveActiveClient()
{
    if (boot_stage < BootStage::REGISTER_URLS) {
        // Can't have any clients yet but keep_alive_fds might not have been initialized yet.
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock{keep_alive_mutex};
    for (int i = 0; i < MAX_WEB_SOCKET_CLIENTS; ++i) {
        if (keep_alive_fds[i] != -1)
            return true;
    }
    return false;
}

// Called by HTTP thread.
bool WebSockets::haveFreeSlot()
{
    std::lock_guard<std::recursive_mutex> lock{keep_alive_mutex};
    for (int i = 0; i < MAX_WEB_SOCKET_CLIENTS; ++i) {
        if (keep_alive_fds[i] == -1)
            return true;
    }
    return false;
}

bool WebSockets::getLastPongAgeMs(int fd, uint64_t *age_ms)
{
    std::lock_guard<std::recursive_mutex> lock{keep_alive_mutex};
    for (int i = 0; i < MAX_WEB_SOCKET_CLIENTS; ++i) {
        if (keep_alive_fds[i] == fd) {
            *age_ms = (now_us() - keep_alive_last_pong[i]).to<millis_t>().as<uint64_t>();
            return true;
        }
    }

    return false;
}

// Called by main thread.
bool WebSockets::sendToAll(const char *payload, size_t payload_len, httpd_ws_type_t ws_type)
{
    if (!this->haveActiveClient()) {
        return true;
    }

    char *payload_copy = static_cast<char *>(malloc(payload_len * sizeof(char)));
    if (payload_copy == nullptr) {
        return false;
    }
    memcpy(payload_copy, payload, payload_len);

    return sendToAllOwnedInternal(payload_copy, payload_len, ws_type);
}

// Called by main thread.
bool WebSockets::sendToAllOwned(char *payload, size_t payload_len, httpd_ws_type_t ws_type)
{
    if (!this->haveActiveClient()) {
        free(payload);
        return true;
    }

    return sendToAllOwnedInternal(payload, payload_len, ws_type);
}

// Called by main thread.
bool WebSockets::sendToAllOwnedInternal(char *payload, size_t payload_len, httpd_ws_type_t ws_type)
{
    std::lock_guard<std::recursive_mutex> lock{work_queue_mutex};
    if (queueFull()) {
        free(payload);
        return false;
    }
    work_queue.push_back({BROADCAST_FD, payload, payload_len, ws_type});
    return true;
}

// Called by HTTP thread.
bool WebSockets::sendToAllOwnedNoFreeBlocking_HTTPThread(char *payload, size_t payload_len, httpd_ws_type_t ws_type)
{
    if (!this->haveActiveClient()) {
        return true;
    }

    ws_work_item wi{BROADCAST_FD, payload, payload_len, ws_type};
    return this->send_ws_work_item(&wi);
}

// Called by main thread.
void WebSockets::triggerHttpThread()
{
    if (worker_active == WEBSOCKET_WORKER_RUNNING) {
        return;
    }

    if (worker_active == WEBSOCKET_WORKER_ENQUEUED) {
        // Protect against stuck localhost communication that blocks the
        // httpd_queue_work control socket. While the worker is enqueued,
        // poke localhost in regular intervals to get things going again.
        // A poll count of 32 results in a poke interval of roughly 4s.
        if ((++worker_poll_count) % 32 == 0) {
            // Don't log this because it happens constantly during a firmware upload.
            //logger.printfln("Poking localhost to get the worker unstuck");
            poke_localhost();
        }
        return;
    }

    // Don't schedule work task if no work is pending.
    // Schedule it anyway every five seconds to reset the watchdog and detect timeouts.
    if (!deadline_elapsed(last_worker_run + 5_s)) {
        std::lock_guard<std::recursive_mutex> lock{work_queue_mutex};
        if (work_queue.empty() && (pending_close_count == 0)) {
            return;
        }
    }

    // If we don't set worker_active to "enqueued" BEFORE enqueueing the worker,
    // we can be preempted after enqueueing, but before we set worker_active to "enqueued"
    // the worker can then run to completion, we then set worker_active to "enqueued" and are
    // NEVER able to start the worker again.
    worker_active = WEBSOCKET_WORKER_ENQUEUED;
    worker_enqueued_at_us.store(now_us().as<uint64_t>(), std::memory_order_relaxed);
    errno = 0;
    const micros_t queue_started = now_us();
    esp_err_t err = httpd_queue_work(httpd, work, this);
    const int queue_errno = errno;
    const uint64_t queue_duration_ms = (now_us() - queue_started).to<millis_t>().as<uint64_t>();
    if (err == ESP_OK) {
        last_worker_run = now_us();
        worker_poll_count = 0;
    } else {
        logger.printfln("Failed to start WebSocket worker: %i | %s (%i)", err, strerror(queue_errno), queue_errno);
        worker_active = WEBSOCKET_WORKER_DONE;
        worker_enqueued_at_us.store(0, std::memory_order_relaxed);
    }
    if ((err != ESP_OK || queue_duration_ms >= 100) && should_log_websocket_anomaly()) {
        logger.printfln("Unusual WebSocket worker enqueue: duration %llums, result %i, errno %i",
                        queue_duration_ms,
                        err,
                        queue_errno);
    }
}

void WebSockets::pre_setup() {
    state = Config::Object({
        {"keep_alive_fds", Config::Tuple(MAX_WEB_SOCKET_CLIENTS, Config::Int32(-1))},
        {"keep_alive_pongs", Config::Tuple(MAX_WEB_SOCKET_CLIENTS, Config::Uptime())},
        {"keep_alive_peers", Config::Tuple(MAX_WEB_SOCKET_CLIENTS, Config::Str("", 0, INET6_ADDRSTRLEN - 1))},
        {"worker_active", Config::Uint8(WEBSOCKET_WORKER_DONE)},
        {"last_worker_run", Config::Uptime()},
        {"queue_len", Config::Uint16(0)}
    });

    for (int i = 0; i < MAX_WEB_SOCKET_CLIENTS; ++i) {
        keep_alive_fds[i] = -1;
        keep_alive_last_pong[i] = 0_us;
    }
}

// Called by main thread.
void WebSockets::updateDebugState()
{
    {
        std::lock_guard<std::recursive_mutex> lock{work_queue_mutex};

        state.get("worker_active"  )->updateUint(worker_active);
        state.get("last_worker_run")->updateUptime(last_worker_run);
        state.get("queue_len"      )->updateUint(work_queue.size());
    }

    {
        Config *state_keep_alive_fds   = static_cast<Config *>(state.get("keep_alive_fds"));
        Config *state_keep_alive_pongs = static_cast<Config *>(state.get("keep_alive_pongs"));
        Config *state_keep_alive_peers = static_cast<Config *>(state.get("keep_alive_peers"));

        std::lock_guard<std::recursive_mutex> lock{keep_alive_mutex};

        for (size_t i = 0; i < MAX_WEB_SOCKET_CLIENTS; ++i) {
            state_keep_alive_fds->get(i)->updateInt(keep_alive_fds[i]);
            state_keep_alive_pongs->get(i)->updateUptime(keep_alive_last_pong[i]);

            if (keep_alive_fds[i] == -1) {
                state_keep_alive_peers->get(i)->updateString("NC");
                continue;
            }

            const IPAddress peer_address = tf_peer_address_of_sockfd(keep_alive_fds[i]);
            state_keep_alive_peers->get(i)->updateString(peer_address.toString());
        }
    }
}

// Called by HTTP thread.
static void session_ctx_free_fn(void *ctx)
{
    if (ctx == nullptr) {
        return;
    }

    delete static_cast<WebSocketsClient *>(ctx);
}

void WebSockets::start(const char *uri, const char *state_path, const char *supported_subprotocol, uint16_t port)
{
    if (this->running) {
        esp_system_abort("WebSockets already started");
    }

    this->running = true;

    if (!this->uri_handler_registered) {
        if (supported_subprotocol != nullptr && !address_is_in_rodata(supported_subprotocol)) {
            supported_subprotocol = perm_strdup(supported_subprotocol);
        }

        server.onWS_HTTPThread(uri, &this->httpd, [this, supported_subprotocol](WebServerRequest request) -> WebServerRequestReturnProtect {
            if (!this->running) {
                return request.send_plain(503, "Endpoint not available");
            }

            struct httpd_data *hd = static_cast<struct httpd_data *>(this->httpd);
            httpd_req_t *req = &hd->hd_req;
            struct httpd_req_aux *aux = static_cast<struct httpd_req_aux *>(req->aux);

            if (!aux->ws_handshake_detect) {
                return request.send_plain(426, "Upgrade Required: WebSockets only");
            }

            if (!this->haveFreeSlot()) {
                this->closeLRUClient_HTTPThread();
            }

            const esp_err_t ret = httpd_ws_respond_server_handshake(req, supported_subprotocol);
            if (ret != ESP_OK) {
                if (ret == ESP_ERR_NOT_FOUND)       return request.send_plain(400, "Bad Request: Missing Sec-WebSocket headers");
                if (ret == ESP_ERR_INVALID_VERSION) return request.send_plain(400, "Bad Request: Invalid WebSocket version");
                if (ret == ESP_ERR_INVALID_ARG)     return request.send_plain(500, "Server error: EINVAL");

                return WebServerRequestReturnProtect{.error = ret};
            }

            aux->sd->ws_handshake_done = true;
            aux->sd->ws_handler = WebSockets::ws_handler;
            aux->sd->ws_control_frames = true;
            aux->sd->ws_user_ctx = this;

            const int sock = httpd_req_to_sockfd(req);
            // The probe delegates to the plain HTTPD sender, so leave custom TLS transports unchanged.
            if (aux->sd->transport_ctx == nullptr) {
                const esp_err_t send_override_result = httpd_sess_set_send_override(this->httpd, sock, timed_httpd_send);
                if (send_override_result != ESP_OK) {
                    logger.printfln("Failed to enable HTTPD transport send timing for fd %i: 0x%x",
                                    sock,
                                    static_cast<unsigned>(send_override_result));
                }
            }

            bool success = true;

            if (this->on_client_connect_fn) {
                WebSocketsClient *client = new WebSocketsClient{sock, this};

                httpd_sess_set_ctx(httpd, sock, client, session_ctx_free_fn);

                // call the client connect callback before adding the client to
                // the keep alive list to ensure that the full state is send by the
                // callback before any other message with a partial state might
                // be send to all clients known by the keep alive list
                success = this->on_client_connect_fn(client);
            }

            if (success) {
                logger.tracefln(server.get_trace_buffer_index(), "WS new fd %i connected", sock);
                this->keepAliveAdd(sock);
                return request.unsafe_ResponseAlreadySent(); // Don't send a HTTP response after switching to WebSockets.
            } else {
                logger.tracefln(server.get_trace_buffer_index(), "WS new fd %i failed on connect", sock);
                // Returning ESP_FAIL inside the WebServerRequestReturnProtect should tell httpd to close the connection.
                return WebServerRequestReturnProtect{.error = ESP_FAIL};
            }
        }, port);

        this->uri_handler_registered = true;
    }

    this->task_ids[0] = task_scheduler.scheduleWithFixedDelay([this](){
        this->triggerHttpThread();
    }, 100_ms, 100_ms);

#if MODULE_WATCHDOG_AVAILABLE()
    this->watchdog_handle = watchdog.add(
        "websocket_worker",
        "WebSocket worker timed out. Web server deadlocked or busy?",
        WEB_SERVER_DEAD_TIMEOUT,
        WEB_SERVER_DEAD_TIMEOUT,
        true); // Force the initial deadline to WEB_SERVER_DEAD_TIMEOUT (currently 5 minutes). The firmware is accepted as good after 7 minutes.
#endif

    this->task_ids[2] = task_scheduler.scheduleWithFixedDelay([this](){
        this->pingActiveClients();
    }, 1_s, 1_s);

    if (state_path != nullptr) {
        this->task_ids[1] = task_scheduler.scheduleWithFixedDelay([this]() {
            this->updateDebugState();
        }, 1_s, 1_s);

        if (!this->state_handler_registered) {
            // TODO Add pull only states to API
            server.on(state_path, HTTP_GET, [this](WebServerRequest request) {
                if (!this->running) {
                    return request.send_plain(503, "Endpoint not available");
                }

                return request.send_json(200, this->state.to_string());
            });

            this->state_handler_registered = true;
        }
    }
}

static constexpr char reboot_close_payload[] = {0x03,0xE9,'E','S','P',' ','r','e','b','o','o','t'}; // Code 1001: Going away

void WebSockets::stop() {
    if (!this->running) {
        return;
    }

    // Mark as not running before disconnecting clients to make sure no client (re)connects.
    this->running = false;

    // Send close frame to all clients
    this->sendToAll(reboot_close_payload, std::size(reboot_close_payload), HTTPD_WS_TYPE_CLOSE);

    worker_active = WEBSOCKET_WORKER_ENQUEUED;
    worker_enqueued_at_us.store(now_us().as<uint64_t>(), std::memory_order_relaxed);
    httpd_queue_work(httpd, WebSockets::work, this);

    // Give http thread 200ms to send the close frames.
    for(int i = 0; i < 10 && worker_active != WEBSOCKET_WORKER_DONE; ++i)
        delay(20);

    // Disconnect clients
    std::lock_guard<std::recursive_mutex> lock{keep_alive_mutex};
    for (int i = 0; i < MAX_WEB_SOCKET_CLIENTS; ++i) {
        if (keep_alive_fds[i] != -1)
            continue;

        this->keepAliveCloseDead_async(keep_alive_fds[i]);
    }

    for (size_t i = 0; i < ARRAY_SIZE(this->task_ids); ++i) {
        task_scheduler.cancel(this->task_ids[i]);
        this->task_ids[i] = 0;
    }

#if MODULE_WATCHDOG_AVAILABLE()
    watchdog.remove(this->watchdog_handle);
#endif
}

void WebSockets::onConnect_HTTPThread(std::function<bool(WebSocketsClient *)> &&fn)
{
    on_client_connect_fn = std::move(fn);
}

void WebSockets::onDisconnect_HTTPThread(std::function<void(WebSocketsClient *client, bool clean_close)> &&fn)
{
    on_client_disconnect_fn = std::move(fn);
}

void WebSockets::onBinaryDataReceived_HTTPThread(std::function<void(WebSocketsClient *client, httpd_ws_frame_t *ws_pkt)> &&fn)
{
    on_binary_data_received_fn = std::move(fn);
}

void WebSockets::onTextDataReceived_HTTPThread(std::function<void(WebSocketsClient *client, httpd_ws_frame_t *ws_pkt)> &&fn)
{
    on_text_data_received_fn = std::move(fn);
}

void WebSockets::pre_reboot() {
    this->stop();
}

// Called by HTTP thread.
void WebSockets::notify_unclean_close(struct sock_db *session)
{
    keepAliveRemove(session->fd);

    WebSocketsClient *client = static_cast<WebSocketsClient *>(session->ctx);

    if (client == nullptr) {
        logger.tracefln(server.get_trace_buffer_index(), "Uncleanly closed session via fd %i has no WebSocketsClient", session->fd);
        return;
    }

    logger.tracefln(server.get_trace_buffer_index(), "unclean close fd %i WSC %p", session->fd, static_cast<void *>(client));

    if (on_client_disconnect_fn != nullptr) {
        on_client_disconnect_fn(client, false);
    }
}
