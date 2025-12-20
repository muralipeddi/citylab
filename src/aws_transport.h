#ifndef AWS_TRANSPORT_H
#define AWS_TRANSPORT_H

#include <stdbool.h>

void start_aws_transport(void);
int aws_send_telemetry(const char *payload);
bool is_aws_connected(void);

#endif