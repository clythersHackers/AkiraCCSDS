/**
 * @file akira_cfdp_service.h
 * @brief Generic AkiraOS CFDP service instance.
 */

#ifndef AKIRA_CCSDS_CFDP_SERVICE_H
#define AKIRA_CCSDS_CFDP_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "ccsds_cfdp_entity.h"
#include "ccsds_cfdp_space_packet.h"
#include "ccsds_router.h"
#include "ccsds_space_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

struct akira_cfdp_service_config {
    uint64_t local_entity_id;
    uint64_t remote_entity_id;
    uint8_t entity_id_len;
    uint8_t transaction_sequence_number_len;
    uint64_t initial_transaction_sequence_number;
    uint16_t local_apid;
    uint16_t remote_apid;
    enum ccsds_packet_type packet_type;
    ccsds_cfdp_space_packet_send_cb_t send_packet;
    void *send_user;
    uint64_t (*now_ms)(void *user);
    const ccsds_cfdp_filestore_ops_t *receive_filestore;
    ccsds_cfdp_event_cb_t event_cb;
    void *event_user;
};

typedef struct akira_cfdp_service_config akira_cfdp_service_config_t;

void akira_cfdp_service_config_defaults(akira_cfdp_service_config_t *config);

enum ccsds_cfdp_status
akira_cfdp_service_init(const akira_cfdp_service_config_t *config);

int akira_cfdp_service_register_rx(struct ccsds_router *router);

enum ccsds_cfdp_status
akira_cfdp_service_send_file(const ccsds_cfdp_filestore_ops_t *filestore,
                             const ccsds_cfdp_put_request_t *request,
                             ccsds_cfdp_transaction_id_t *transaction_id);

void akira_cfdp_service_poll(uint64_t now_ms);

ccsds_cfdp_entity_t *akira_cfdp_service_entity(void);

ccsds_cfdp_space_packet_adapter_t *
akira_cfdp_service_space_packet_adapter(void);

const ccsds_cfdp_filestore_ops_t *akira_cfdp_service_receive_filestore(void);

#ifdef __cplusplus
}
#endif

#endif /* AKIRA_CCSDS_CFDP_SERVICE_H */
