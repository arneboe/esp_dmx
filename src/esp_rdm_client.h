#pragma once
#include <stdbool.h>
#include "rdm_types.h"
#include "dmx_types.h"


typedef void (*start_address_changed_cb_t)(uint16_t);

//TODO comment
bool rdm_client_init(dmx_port_t dmx_num, uint16_t start_address, uint16_t footprint, const char* device_label);


/** 
 * @param cb Will be invoked every time the dmx start address is changed.
 *           It will also be invoked when this method is called with the current start address. */
void set_start_address_changed_cb(dmx_port_t dmx_num, start_address_changed_cb_t cb);

/**
 * This method should be called anytime a dmx-rdm message is received.
*/
void rdm_client_handle_rdm_message(dmx_port_t dmx_num, const dmx_packet_t *dmxPacket, const void *data, const uint16_t size);