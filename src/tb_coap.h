#ifndef TB_COAP_H
#define TB_COAP_H

void start_tb_coap(void);
int tb_coap_send_telemetry_payload_string(const char *name,
                                          const char *payload);

/* Add this new function */
void tb_coap_force_disconnect(void);

#endif