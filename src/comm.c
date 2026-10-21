#include "comm.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "dhcpserver.h"

#define REPLY_MAX_LEN 128
#define TELEMETRY_QUEUE_LEN 512 // ~0,5 s de folga a 1 kHz
#define PACKET_MAX_AGE_US 20000 // Envia pacote incompleto após 20 ms

typedef struct { char text[COMM_CMD_MAX_LEN]; } cmd_msg_t;
typedef struct { char text[REPLY_MAX_LEN]; } reply_msg_t;

// Filas entre núcleos (seguras para multicore)
static queue_t cmd_queue;       // comunicação (núcleo 0) -> controle (núcleo 1)
static queue_t reply_queue;     // controle -> comunicação
static queue_t telemetry_queue; // controle -> comunicação

static volatile uint32_t dropped_samples;

// --- Estado da tarefa de comunicação (núcleo 0) ---
static struct udp_pcb *pcb;
static ip_addr_t peer_addr;
static uint16_t peer_port;
static bool has_peer;
static dhcp_server_t dhcp_server;
static telemetry_packet_t packet;
static uint32_t packet_seq;

// Callback do lwIP: chega um comando do PC
static void on_udp_recv(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                        const ip_addr_t *addr, u16_t port) {
    (void)arg;
    (void)upcb;

    // Responde sempre para quem mandou o último comando
    ip_addr_copy(peer_addr, *addr);
    peer_port = port;
    has_peer = true;

    cmd_msg_t msg;
    uint16_t n = pbuf_copy_partial(p, msg.text, sizeof(msg.text) - 1, 0);
    msg.text[n] = '\0';
    pbuf_free(p);

    queue_try_add(&cmd_queue, &msg);
}

static void udp_send_buf(const void *data, uint16_t len) {
    if (!has_peer) return;
    cyw43_arch_lwip_begin();
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (p) {
        memcpy(p->payload, data, len);
        udp_sendto(pcb, p, &peer_addr, peer_port);
        pbuf_free(p);
    }
    cyw43_arch_lwip_end();
}

static void send_packet(void) {
    packet.magic = TELEMETRY_MAGIC;
    packet.seq = packet_seq++;
    packet.dropped = dropped_samples;
    uint16_t len = offsetof(telemetry_packet_t, samples) + packet.count * sizeof(telemetry_sample_t);
    udp_send_buf(&packet, len);
    packet.count = 0;
}

// Lê comandos digitados no terminal USB, linha a linha
static void poll_usb_command(void) {
    static cmd_msg_t msg;
    static int len;
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            msg.text[len] = '\0';
            if (len > 0) queue_try_add(&cmd_queue, &msg);
            len = 0;
        } else if (len < COMM_CMD_MAX_LEN - 1) {
            msg.text[len++] = (char)c;
        }
    }
}

void comm_run(void) {
    if (cyw43_arch_init()) {
        printf("ERRO: falha ao iniciar o Wi-Fi\n");
        while (true) poll_usb_command();
    }
    cyw43_arch_enable_ap_mode(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);

    ip_addr_t gw, mask;
    IP4_ADDR(ip_2_ip4(&gw), 192, 168, 4, 1);
    IP4_ADDR(ip_2_ip4(&mask), 255, 255, 255, 0);
    dhcp_server_init(&dhcp_server, &cyw43_state.netif[CYW43_ITF_AP], &gw, &mask);

    cyw43_arch_lwip_begin();
    pcb = udp_new();
    udp_bind(pcb, IP_ANY_TYPE, UDP_PORT);
    udp_recv(pcb, on_udp_recv, NULL);
    cyw43_arch_lwip_end();

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); // LED da placa: Wi-Fi pronto
    printf("Wi-Fi: rede \"%s\", robo em 192.168.4.1:%d (UDP)\n", WIFI_SSID, UDP_PORT);

    uint64_t packet_start = 0;
    while (true) {
        reply_msg_t reply;
        while (queue_try_remove(&reply_queue, &reply)) {
            printf("%s\n", reply.text);
            udp_send_buf(reply.text, strlen(reply.text));
        }
        poll_usb_command();

        while (queue_try_remove(&telemetry_queue, &packet.samples[packet.count])) {
            if (packet.count++ == 0) packet_start = time_us_64();
            if (packet.count == TELEMETRY_SAMPLES_PER_PACKET) send_packet();
        }
        if (packet.count > 0 && time_us_64() - packet_start > PACKET_MAX_AGE_US) {
            send_packet();
        }

        sleep_us(500);
    }
}

void comm_init(void) {
    queue_init(&cmd_queue, sizeof(cmd_msg_t), 8);
    queue_init(&reply_queue, sizeof(reply_msg_t), 16);
    queue_init(&telemetry_queue, sizeof(telemetry_sample_t), TELEMETRY_QUEUE_LEN);
}

bool comm_get_command(char *buf, size_t len) {
    cmd_msg_t msg;
    if (!queue_try_remove(&cmd_queue, &msg)) return false;
    strncpy(buf, msg.text, len - 1);
    buf[len - 1] = '\0';
    return true;
}

void comm_reply(const char *fmt, ...) {
    reply_msg_t msg;
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg.text, sizeof(msg.text), fmt, args);
    va_end(args);

    queue_try_add(&reply_queue, &msg);
}

bool comm_push_sample(const telemetry_sample_t *sample) {
    if (queue_try_add(&telemetry_queue, sample)) return true;
    dropped_samples++;
    return false;
}

uint32_t comm_dropped_samples(void) {
    return dropped_samples;
}
