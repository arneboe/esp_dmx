#pragma once
#include <stdbool.h>
#include "rdm_types.h"
#include "dmx_types.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef void (*start_address_changed_cb_t)(uint16_t);
typedef void (*identify_cb_t)(bool);
typedef void (*label_changed_cb_t)(const char*, size_t);


//TODO comment
bool rdm_client_init(dmx_port_t dmx_num, uint16_t start_address, uint16_t footprint, const char* device_label);


/** 
 * @param cb Will be invoked every time the dmx start address is changed. */
void rdm_client_set_start_address_changed_cb(dmx_port_t dmx_num, start_address_changed_cb_t cb);

/**
 * @param cb Will be invoked every time the indentify value changes.
 *           If the value is true, the device should identify itself (e.g. by blinking)
*/
void rdm_client_set_notify_cb(dmx_port_t dmx_num, identify_cb_t cb);

/**
 * @param cb Will be invoked every time the device label is changed.
 * @note the device label is **not** null-terminated
*/
void rdm_client_set_label_changed_cb(dmx_port_t dmx_num, label_changed_cb_t cb);

/**
 * This method should be called anytime a dmx-rdm message is received.
*/
void rdm_client_handle_rdm_message(dmx_port_t dmx_num, const dmx_packet_t *dmxPacket, const void *data, const uint16_t size);

#ifdef __cplusplus
}
#endif
