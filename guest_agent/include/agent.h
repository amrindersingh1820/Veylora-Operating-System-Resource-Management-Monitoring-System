/* agent.h – Guest Monitoring Agent Main Configuration & Channel Interface
 *
 * Configures the monitoring loop, output channel (virtio-serial, TCP socket, or stdout),
 * and sample frequency.
 */

#ifndef VOS_GUEST_AGENT_H
#define VOS_GUEST_AGENT_H

#include "metrics.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CHANNEL_VIRTIO_SERIAL = 0,
    CHANNEL_TCP_SERVER,
    CHANNEL_STDOUT
} AgentChannelType;

typedef struct {
    char             vm_id[64];
    int              interval_ms;
    AgentChannelType channel_type;
    char             channel_path[256]; /* For virtio-serial device path */
    int              tcp_port;          /* For TCP server mode */
    bool             oneshot;           /* Collect one sample and exit */
    bool             verbose;
} AgentConfig;

/* Initialize agent configuration with sensible defaults */
void agent_config_default(AgentConfig *cfg);

/* Parse command-line flags into AgentConfig */
int agent_config_parse(AgentConfig *cfg, int argc, char **argv);

/* Start the agent loop */
int agent_run(const AgentConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif /* VOS_GUEST_AGENT_H */

