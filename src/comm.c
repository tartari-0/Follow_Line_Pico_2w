#include "comm.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "dhcpserver.h"
#include "exp_log.h"

#define REPLY_MAX_LEN 128
#define TELEMETRY_QUEUE_LEN 512 // ~0,5 s de folga a 1 kHz
#define PACKET_MAX_AGE_US 20000 // Envia pacote incompleto após 20 ms

typedef struct { char text[COMM_CMD_MAX_LEN]; } cmd_msg_t;
typedef struct { char text[REPLY_MAX_LEN]; } reply_msg_t;
typedef struct { uint32_t exp_id, first, count; } resend_req_t;

// Filas entre núcleos (seguras para multicore)
static queue_t cmd_queue;       // comunicação (núcleo 0) -> controle (núcleo 1)
static queue_t reply_queue;     // controle -> comunicação
static queue_t telemetry_queue; // controle -> comunicação
static queue_t resend_queue;    // pedidos de retransmissão do PC (só no núcleo 0)

static volatile uint32_t dropped_samples;

// --- Estado da tarefa de comunicação (núcleo 0) ---
static struct udp_pcb *pcb;
static ip_addr_t peer_addr;
static uint16_t peer_port;
static bool has_peer;
static dhcp_server_t dhcp_server;
static telemetry_packet_t packet;
static uint32_t packet_seq;
static exp_packet_t exp_packet;
static uint32_t exp_sent_id;    // Ensaio cujas amostras estão sendo transmitidas
static uint32_t exp_sent_index; // Próxima amostra a transmitir
static uint64_t exp_wait_start;

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

    // "resend <id> <primeira> <quantidade>" é atendido aqui mesmo, sem passar pelo controle
    resend_req_t req;
    if (sscanf(msg.text, "resend %lu %lu %lu", (unsigned long *)&req.exp_id,
               (unsigned long *)&req.first, (unsigned long *)&req.count) == 3) {
        queue_try_add(&resend_queue, &req);
        return;
    }
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

// Envia as amostras [first, first + count) do ensaio atual.
// Retorna quantas estavam disponíveis no buffer circular.
static uint32_t send_exp_samples(uint32_t id, uint32_t first, uint32_t count) {
    uint32_t sent = 0;
    exp_packet.magic = EXP_PACKET_MAGIC;
    exp_packet.exp_id = id;
    exp_packet.total = exp_log_total();
    exp_packet.count = 0;
    exp_packet.first_index = first;

    for (uint32_t i = first; i < first + count; i++) {
        exp_sample_t *dst = &exp_packet.samples[exp_packet.count];
        if (!exp_log_get(i, dst) || exp_log_id() != id) {
            break; // Indisponível: o resto do intervalo também está
        }
        exp_packet.count++;
        sent++;
        if (exp_packet.count == EXP_SAMPLES_PER_PACKET || i + 1 == first + count) {
            udp_send_buf(&exp_packet, offsetof(exp_packet_t, samples) + exp_packet.count * sizeof(exp_sample_t));
            exp_packet.count = 0;
            exp_packet.first_index = i + 1;
        }
    }
    if (exp_packet.count > 0) {
        udp_send_buf(&exp_packet, offsetof(exp_packet_t, samples) + exp_packet.count * sizeof(exp_sample_t));
    }
    return sent;
}

// Transmite as amostras novas do ensaio (pacotes cheios, ou o que houver após 20 ms)
static void stream_exp_samples(void) {
    uint32_t id = exp_log_id();
    if (id != exp_sent_id) {
        exp_sent_id = id;
        exp_sent_index = 0;
        exp_wait_start = time_us_64();
    }
    uint32_t pending = exp_log_count() - exp_sent_index;
    if (pending == 0) {
        exp_wait_start = time_us_64();
        return;
    }
    if (pending >= EXP_SAMPLES_PER_PACKET || time_us_64() - exp_wait_start > PACKET_MAX_AGE_US) {
        uint32_t n = pending < EXP_SAMPLES_PER_PACKET ? pending : EXP_SAMPLES_PER_PACKET;
        uint32_t sent = send_exp_samples(id, exp_sent_index, n);
        // Se algo já foi sobrescrito (link muito lento), pula: o PC verá o buraco
        exp_sent_index += sent ? sent : n;
        exp_wait_start = time_us_64();
    }
}

static void handle_resend_requests(void) {
    resend_req_t req;
    while (queue_try_remove(&resend_queue, &req)) {
        if (req.exp_id != exp_log_id()) continue;
        if (req.count > 10 * EXP_SAMPLES_PER_PACKET) req.count = 10 * EXP_SAMPLES_PER_PACKET;
        uint32_t sent = send_exp_samples(req.exp_id, req.first, req.count);
        if (sent < req.count && req.first + sent < exp_log_count()) {
            char msg[64];
            int n = snprintf(msg, sizeof(msg), "resend indisponivel %lu %lu",
                             (unsigned long)(req.first + sent), (unsigned long)(req.count - sent));
            udp_send_buf(msg, (uint16_t)n);
        }
    }
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

        stream_exp_samples();
        handle_resend_requests();

        sleep_us(500);
    }
}

void comm_init(void) {
    queue_init(&cmd_queue, sizeof(cmd_msg_t), 8);
    queue_init(&reply_queue, sizeof(reply_msg_t), 16);
    queue_init(&telemetry_queue, sizeof(telemetry_sample_t), TELEMETRY_QUEUE_LEN);
    queue_init(&resend_queue, sizeof(resend_req_t), 32);
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
