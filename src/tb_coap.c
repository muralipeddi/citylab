#include "tb_coap.h"
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>

LOG_MODULE_REGISTER(tb_coap, CONFIG_LOG_DEFAULT_LEVEL);

static int sock = -1;
static struct sockaddr_in server;
static uint8_t coap_buf[256];

/* Flag to track if we have the IP address */
static bool ip_resolved = false;

/* Forward declaration */
static int tb_coap_connect(void);

static int server_resolve(void) {
  int err;
  struct addrinfo *result;
  struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM};

  LOG_INF("Resolving %s", CONFIG_CLOUD_THINGSBOARD_COAP_SERVER_HOSTNAME);

  err = getaddrinfo(CONFIG_CLOUD_THINGSBOARD_COAP_SERVER_HOSTNAME, NULL, &hints,
                    &result);
  if (err != 0) {
    LOG_ERR("DNS Lookup failed %d", err);
    ip_resolved = false;
    return -EIO;
  }

  if (result == NULL) {
    LOG_ERR("Address not found");
    ip_resolved = false;
    return -ENOENT;
  }

  struct sockaddr_in *server4 = ((struct sockaddr_in *)&server);
  server4->sin_addr.s_addr =
      ((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
  server4->sin_family = AF_INET;
  server4->sin_port = htons(CONFIG_CLOUD_THINGSBOARD_COAP_SERVER_PORT);

  char ipv4_addr[NET_IPV4_ADDR_LEN];
  inet_ntop(AF_INET, &server4->sin_addr.s_addr, ipv4_addr, sizeof(ipv4_addr));
  LOG_INF("ThingsBoard IP resolved: %s", ipv4_addr);

  freeaddrinfo(result);
  ip_resolved = true;
  return 0;
}

void tb_coap_force_disconnect(void) {
  if (sock >= 0) {
    close(sock);
    sock = -1;
  }
}

static int tb_coap_connect(void) {
  int err;

  /* 1. Ensure we have an IP before creating a socket */
  if (!ip_resolved) {
    LOG_WRN("IP not resolved. Resolving now...");
    if (server_resolve() != 0) {
      return -EIO;
    }
  }

  /* 2. Safety: Close any old socket to prevent leaks */
  tb_coap_force_disconnect();

  LOG_INF("Creating new socket...");
  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    LOG_ERR("Failed to create socket, errno: %d", errno);
    return -errno;
  }

  LOG_INF("Connecting to server...");
  err = connect(sock, (struct sockaddr *)&server, sizeof(server));

  /* --- FIX FOR ERROR 106 STARTS HERE --- */
  if (err < 0) {
    if (errno == EISCONN || errno == 106) {
      /* Error 106 means "Transport endpoint is already connected"
       * For us, this is GREAT. It means the link is ready.
       */
      LOG_WRN("Socket was already connected (106). Proceeding as Success.");
      return 0;
    }

    /* Real error */
    LOG_ERR("Failed to connect socket, errno: %d", errno);
    tb_coap_force_disconnect();
    return -errno;
  }
  /* --- FIX ENDS HERE --- */

  LOG_INF("Socket connected!");
  return 0;
}

void start_tb_coap() { server_resolve(); }

int tb_coap_send_telemetry_payload_string(const char *name,
                                          const char *payload) {
  if (sock < 0) {
    LOG_WRN("Socket not open, attempting to reconnect...");
    if (tb_coap_connect() != 0) {
      return -EIO;
    }
  }

  struct coap_packet request;
  int err;

  err = coap_packet_init(&request, coap_buf, sizeof(coap_buf), 1,
                         COAP_TYPE_NON_CON, 0, coap_next_token(),
                         COAP_METHOD_POST, coap_next_id());
  if (err < 0) {
    LOG_ERR("Failed to init CoAP packet, err: %d", err);
    return err;
  }

  coap_packet_append_option(&request, COAP_OPTION_URI_PATH, "api", 3);
  coap_packet_append_option(&request, COAP_OPTION_URI_PATH, "v1", 2);
  coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
                            CONFIG_CLOUD_THINGSBOARD_COAP_ACCESS_TOKEN,
                            strlen(CONFIG_CLOUD_THINGSBOARD_COAP_ACCESS_TOKEN));
  coap_packet_append_option(&request, COAP_OPTION_URI_PATH, "telemetry", 9);

  const uint8_t content_format = COAP_CONTENT_FORMAT_APP_JSON;
  coap_packet_append_option(&request, COAP_OPTION_CONTENT_FORMAT,
                            &content_format, sizeof(content_format));

  coap_packet_append_payload_marker(&request);
  coap_packet_append_payload(&request, (uint8_t *)payload, strlen(payload));

  err = send(sock, request.data, request.offset, 0);
  if (err < 0) {
    LOG_ERR("Failed to send CoAP packet, errno: %d", errno);
    tb_coap_force_disconnect();
    return -errno;
  }

  return 0;
}