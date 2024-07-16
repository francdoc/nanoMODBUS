#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "credentials.h"

#define BUF_SIZE 2048
#define DEBUG_printf printf
#define POLL_TIME_S 5

typedef struct TCP_CLIENT_T_ {
    struct tcp_pcb *tcp_pcb;
    ip_addr_t remote_addr;
    uint8_t buffer[BUF_SIZE];
    int buffer_len;
    int sent_len;
    bool complete;
    int run_count;
    bool connected;
} TCP_CLIENT_T;

static err_t tcp_client_close(void *arg) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    err_t err = ERR_OK;
    if (state->tcp_pcb != NULL) {
        tcp_arg(state->tcp_pcb, NULL);
        tcp_poll(state->tcp_pcb, NULL, 0);
        tcp_sent(state->tcp_pcb, NULL);
        tcp_recv(state->tcp_pcb, NULL);
        tcp_err(state->tcp_pcb, NULL);
        err = tcp_close(state->tcp_pcb);
        if (err != ERR_OK) {
            DEBUG_printf("close failed %d, calling abort\n", err);
            tcp_abort(state->tcp_pcb);
            err = ERR_ABRT;
        }
        state->tcp_pcb = NULL;
    }
    return err;
}

static err_t tcp_result(void *arg, int status) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    if (status == 0) {
        DEBUG_printf("test success\n");
    } else {
        DEBUG_printf("test failed %d\n", status);
    }
    state->complete = true;
    return tcp_client_close(arg);
}

static err_t tcp_client_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    DEBUG_printf("tcp_client_sent %u\n", len);
    state->sent_len += len;
    if (state->sent_len >= BUF_SIZE) {
        state->run_count++;
        state->buffer_len = 0;
        state->sent_len = 0;
        DEBUG_printf("Waiting for buffer from server\n");
    }
    return ERR_OK;
}

static err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    if (err != ERR_OK) {
        printf("connect failed %d\n", err);
        return tcp_result(arg, err);
    }
    state->connected = true;
    DEBUG_printf("Waiting for buffer from server\n");
    return ERR_OK;
}

static err_t tcp_client_poll(void *arg, struct tcp_pcb *tpcb) {
    DEBUG_printf("tcp_client_poll\n");
    return tcp_result(arg, -1);
}

static void tcp_client_err(void *arg, err_t err) {
    if (err != ERR_ABRT) {
        DEBUG_printf("tcp_client_err %d\n", err);
        tcp_result(arg, err);
    }
}

err_t tcp_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    if (!p) {
        return tcp_result(arg, -1);
    }
    cyw43_arch_lwip_check();
    if (p->tot_len > 0) {
        DEBUG_printf("recv %d err %d\n", p->tot_len, err);
        for (struct pbuf *q = p; q != NULL; q = q->next) {
            // Process received data if needed
        }
        const uint16_t buffer_left = BUF_SIZE - state->buffer_len;
        state->buffer_len += pbuf_copy_partial(p, state->buffer + state->buffer_len, p->tot_len > buffer_left ? buffer_left : p->tot_len, 0);
        tcp_recved(tpcb, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}

static bool tcp_client_open(void *arg, const char* address, const char* port) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    DEBUG_printf("Connecting to %s port %u\n", ip4addr_ntoa(&state->remote_addr), port);
    state->tcp_pcb = tcp_new_ip_type(IP_GET_TYPE(&state->remote_addr));
    if (!state->tcp_pcb) {
        DEBUG_printf("failed to create pcb\n");
        return false;
    }

    tcp_arg(state->tcp_pcb, state);
    tcp_poll(state->tcp_pcb, tcp_client_poll, POLL_TIME_S * 2);
    tcp_sent(state->tcp_pcb, tcp_client_sent);
    tcp_recv(state->tcp_pcb, tcp_client_recv);
    tcp_err(state->tcp_pcb, tcp_client_err);

    state->buffer_len = 0;

    // cyw43_arch_lwip_begin/end should be used around calls into lwIP to ensure correct locking.
    // You can omit them if you are in a callback from lwIP. Note that when using pico_cyw_arch_poll
    // these calls are a no-op and can be omitted, but it is a good practice to use them in
    // case you switch the cyw43_arch type later.
    cyw43_arch_lwip_begin();
    err_t err = tcp_connect(state->tcp_pcb, &state->remote_addr, port, tcp_client_connected);
    cyw43_arch_lwip_end();

    if (err != ERR_OK) {
        DEBUG_printf("tcp_connect failed %d\n", err);
        return false;
    }
    return true;
}

void* connect_tcp(const char* address, const char* port) {
    TCP_CLIENT_T *state = calloc(1, sizeof(TCP_CLIENT_T));
    if (!state) {
        DEBUG_printf("failed to allocate state\n");
        return NULL;
    }

    ip4addr_aton(address, &state->remote_addr);
    
    if (!tcp_client_open(state, address, port)) {
        free(state);
        return NULL;
    }
    return state;
}

int32_t read_fd_pico(uint8_t* buf, uint16_t count, int32_t timeout_ms, void* arg) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    if (!state->connected) {
        DEBUG_printf("Client not connected\n");
        return -1;
    }

    int32_t total_read = 0;
    absolute_time_t timeout = make_timeout_time_ms(timeout_ms);

    while (total_read < count) {
        if (state->buffer_len > 0) {
            int32_t to_copy = (state->buffer_len < count - total_read) ? state->buffer_len : count - total_read;
            memcpy(buf + total_read, state->buffer, to_copy);
            total_read += to_copy;
            state->buffer_len -= to_copy;
            // Shift remaining data to the start of the buffer
            memmove(state->buffer, state->buffer + to_copy, state->buffer_len);
        }
        if (absolute_time_diff_us(get_absolute_time(), timeout) <= 0) {
            break;
        }
        cyw43_arch_poll();
    }
    return total_read;
}
int32_t write_fd_pico(const uint8_t* buf, uint16_t count, int32_t timeout_ms, void* arg) {
    TCP_CLIENT_T *state = (TCP_CLIENT_T*)arg;
    if (!state->connected) {
        DEBUG_printf("Client not connected\n");
        return -1;
    }

    int32_t total_written = 0;
    absolute_time_t timeout = make_timeout_time_ms(timeout_ms);

    while (total_written < count) {
        int32_t to_write = count - total_written;
        err_t err = tcp_write(state->tcp_pcb, buf + total_written, to_write, TCP_WRITE_FLAG_COPY);
        if (err != ERR_OK) {
            DEBUG_printf("Failed to write data %d\n", err);
            return -1;
        }
        tcp_output(state->tcp_pcb);
        total_written += to_write;

        if (absolute_time_diff_us(get_absolute_time(), timeout) <= 0) {
            break;
        }
        cyw43_arch_poll();
    }

    return total_written;
}

int setup()
{
  stdio_init_all();

    if (cyw43_arch_init()) {
        DEBUG_printf("failed to initialise\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();

    sleep_ms(3000);

    printf("Connecting to Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return 1;
    } else {
        printf("Connected.\n");
    }
}