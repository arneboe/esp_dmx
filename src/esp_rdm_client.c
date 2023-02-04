#include "esp_rdm_client.h"
#include "rdm_types.h"
#include "esp_rdm.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_dmx.h"
#include "private/rdm_encode/functions.h"
#include <string.h>


#define MAX_NUM_PERSONALITIES 100


typedef struct rdm_client_personality_t
{
    uint16_t footprint;
    char description[32];
    uint8_t description_len;
} rdm_client_personality_t;


/**
 * All parameters of a rdm client device
 */
typedef struct rdm_client_parameters_t
{
    start_address_changed_cb_t address_cb;
    identify_cb_t identify_cb;
    label_changed_cb_t label_cb;
    rdm_client_personality_t personalities[MAX_NUM_PERSONALITIES];
    personality_changed_cb_t personality_cb;
} rdm_client_parameters_t;

rdm_parameters_t rdm_parameters[DMX_NUM_MAX] = {0};
rdm_client_parameters_t rdm_client_parameters[DMX_NUM_MAX] = {0};

void rdm_client_set_start_address_changed_cb(dmx_port_t dmx_num, start_address_changed_cb_t cb)
{
    if (dmx_num >= DMX_NUM_MAX)
    {
        ESP_LOGE("rdm_client", "dmx_num too large");
        return;
    }

    rdm_client_parameters[dmx_num].address_cb = cb;
}

void rdm_client_set_start_address(dmx_port_t dmx_num, uint16_t start_address)
{
    if (dmx_num >= DMX_NUM_MAX)
    {
        ESP_LOGE("rdm_client", "dmx_num too large");
        return;
    }
    rdm_parameters_t *params = &rdm_parameters[dmx_num];
    params->device_info.start_address = start_address;
}

void rdm_client_set_notify_cb(dmx_port_t dmx_num, identify_cb_t cb)
{
    if (dmx_num >= DMX_NUM_MAX)
    {
        ESP_LOGE("rdm_client", "dmx_num too large");
        return;
    }

    rdm_client_parameters[dmx_num].identify_cb = cb;
}


void rdm_client_set_personality_changed_cb(dmx_port_t dmx_num, personality_changed_cb_t cb)
{
    if (dmx_num >= DMX_NUM_MAX)
    {
        ESP_LOGE("rdm_client", "dmx_num too large");
        return;
    }    
    rdm_client_parameters[dmx_num].personality_cb = cb;
}

void rdm_client_set_label_changed_cb(dmx_port_t dmx_num, label_changed_cb_t cb)
{
    if (dmx_num >= DMX_NUM_MAX)
    {
        ESP_LOGE("rdm_client", "dmx_num too large");
        return;
    }
    rdm_client_parameters[dmx_num].label_cb = cb;
}

bool rdm_client_init(dmx_port_t dmx_num, uint16_t start_address, uint16_t footprint, const char *device_label,
                     const char *personality_description)
{
    if (dmx_num >= DMX_NUM_MAX)
    {
        ESP_LOGE("rdm_client", "dmx_num too large");
        return false;
    }

    if (strlen(device_label) > 31)
    {
        ESP_LOGE("rdm_client", "device_label too long. Max size is 31");
        return false;
    }

    if(strlen(personality_description) > 31)
    {
        ESP_LOGE("rdm_client", "personality_description too long. Max size is 31");
        return false;
    }

    rdm_parameters_t *params = &rdm_parameters[dmx_num];

    params->device_info.major_rdm_version = 1;
    params->device_info.minor_rdm_version = 0;
    params->device_info.model_id = 0x4242;
    params->device_info.coarse_product_category = 0x01; // TODO create constants for product category and fine category
    params->device_info.fine_product_category = 0x00;
    params->device_info.software_version_id = 0x42;
    params->device_info.footprint = footprint;
    params->device_info.current_personality = 1;
    params->device_info.personality_count = 1;
    params->device_info.start_address = start_address;
    params->device_info.sub_device_count = 0;
    params->device_info.sensor_count = 0;
    params->identify_device = false;

    strcpy(params->device_label, device_label);
    params->device_label_len = strlen(device_label);

    rdm_client_parameters_t* client_params = &rdm_client_parameters[dmx_num];

    client_params->personalities[0].footprint = footprint;
    client_params->personalities[0].description_len = strlen(personality_description);
     client_params->personality_cb = NULL;
    strcpy(client_params->personalities[0].description, personality_description);

    return true;
}

bool rdm_client_set_personality(dmx_port_t dmx_num, uint8_t personality)
{
    if (dmx_num >= DMX_NUM_MAX)
    {
        ESP_LOGE("rdm_client", "dmx_num too large");
        return -1;
    }   
    rdm_parameters_t *params = &rdm_parameters[dmx_num];
    if(personality > params->device_info.personality_count)
    {
        ESP_LOGE("rdm_client", "personality too large");
        return false;
    }
    params->device_info.current_personality = personality;
    rdm_client_parameters_t* client_params = &rdm_client_parameters[dmx_num];
    params->device_info.footprint = client_params->personalities[personality - 1].footprint;
    return true;
}

int rdm_client_add_personality(dmx_port_t dmx_num, uint16_t footprint, const char* description)
{
    if (dmx_num >= DMX_NUM_MAX)
    {
        ESP_LOGE("rdm_client", "dmx_num too large");
        return -1;
    }
    if(strlen(description) > 31)
    {
        ESP_LOGE("rdm_client", "description too long. Max size is 31");
        return -1;
    }

    rdm_client_parameters_t* client_params = &rdm_client_parameters[dmx_num];
    rdm_parameters_t *params = &rdm_parameters[dmx_num];
    if(params->device_info.personality_count >= MAX_NUM_PERSONALITIES)
    {
        ESP_LOGE("rdm_client", "max num personalities reached");
        return -1;
    }

    client_params->personalities[params->device_info.personality_count].footprint = footprint;
    client_params->personalities[params->device_info.personality_count].description_len = strlen(description);
    strcpy(client_params->personalities[params->device_info.personality_count].description, description);    
    params->device_info.personality_count++;
    return params->device_info.personality_count;
}

void rdm_client_handle_discovery_command(dmx_port_t dmx_num, const rdm_header_t *header, const void *data, const uint16_t data_size)
{
    if (header->pid == RDM_PID_DISC_UNIQUE_BRANCH)
    {
        // TODO ensure that data_size is large enough
        const rdm_uid_t lowUid = buf_to_uid(data + 24);
        const rdm_uid_t highUid = buf_to_uid(data + 30);
        const rdm_uid_t ourUid = rdm_get_uid(dmx_num);

        if (!rdm_is_muted(dmx_num) && lowUid <= ourUid && ourUid <= highUid)
        {
            const size_t respSize = rdm_send_disc_response(dmx_num, 7, ourUid);
            ESP_LOGI("RDM", "Sent discovery response. %d bytes", respSize);
        }
    }
    else if (header->pid == RDM_PID_DISC_UN_MUTE)
    {
        ESP_LOGI("RDM", "Received UNMUTE");
        rdm_set_muted(dmx_num, false);
        // TODO add response for unmute
    }
    else if (header->pid == RDM_PID_DISC_MUTE)
    {
        ESP_LOGI("RDM", "Received MUTE");
        rdm_set_muted(dmx_num, true);

        // TODO store muteParams in rdm_client_params
        rdm_disc_mute_t muteParams;
        muteParams.managed_proxy = false;
        muteParams.sub_device = false;
        muteParams.boot_loader = false;
        muteParams.proxied_device = false;
        muteParams.binding_uid = 0;
        const size_t bytesSent = rdm_send_mute_response(dmx_num, header->source_uid, header->tn, &muteParams);
        if (bytesSent == 0)
        {
            // TODO print error or something
        }
    }
}

void rdm_client_handle_rdm_message(dmx_port_t dmx_num, const dmx_packet_t *dmxPacket, const void *data, const uint16_t size)
{
    rdm_header_t header;
    if (rdm_get_header(&header, data))
    {
        if (rdm_is_directed_at_us(dmx_num, &header))
        {
            if (header.cc == RDM_CC_DISC_COMMAND)
            {
                rdm_client_handle_discovery_command(dmx_num, &header, data, size);
            }
            else if (header.cc == RDM_CC_GET_COMMAND)
            {
                switch (header.pid)
                {
                case RDM_PID_DEVICE_INFO:
                {
                    uint8_t buffer[0x13]; // FIXME this is not good style
                    const size_t pdl = rdm_encode_device_info_(&buffer, &rdm_parameters[dmx_num].device_info);
                    if (pdl != 0x13)
                    {
                        ESP_LOGE("RDM DBG", "buffer overflow in rdm_encode_device_info_");
                        return;
                    }
                    const size_t bytesSent = rdm_send_get_param_response(dmx_num, header.source_uid, header.tn,
                                                                         RDM_PID_DEVICE_INFO, header.sub_device, buffer, pdl);
                    ESP_LOGI("RDM DBG", "Sent DEVICE_INFO response. %d bytes", bytesSent);
                }
                break;
                case RDM_PID_IDENTIFY_DEVICE:
                {
                    const rdm_parameters_t *params = &rdm_parameters[dmx_num];
                    const size_t bytesSent = rdm_send_get_param_response(dmx_num, header.source_uid, header.tn,
                                                                         RDM_PID_IDENTIFY_DEVICE, header.sub_device, &params->identify_device, 1);

                    ESP_LOGI("RDM DBG", "Sent IDENTIFY_DEVICE response. %d bytes", bytesSent);
                }
                break;
                case RDM_PID_DEVICE_LABEL:
                {
                    const rdm_parameters_t *params = &rdm_parameters[dmx_num];
                    const size_t bytesSent = rdm_send_get_param_response(dmx_num, header.source_uid, header.tn,
                                                                         RDM_PID_DEVICE_LABEL, header.sub_device, params->device_label, params->device_label_len);
                    ESP_LOGI("RDM DBG", "Sent GET DEVICE_LABEL response. label: %s, %d bytes", params->device_label, bytesSent);
                }
                break;
                case RDM_PID_SUPPORTED_PARAMETERS:
                {
                    const uint16_t supported_params[] = {bswap16(RDM_PID_DEVICE_LABEL), bswap16(RDM_PID_DMX_PERSONALITY)};
                    const size_t bytesSent = rdm_send_get_param_response(dmx_num, header.source_uid, header.tn,
                                                                         RDM_PID_SUPPORTED_PARAMETERS, header.sub_device, supported_params, sizeof(supported_params));
                    ESP_LOGI("RDM DBG", "Sent GET RDM_PID_SUPPORTED_PARAMETERS response. %d bytes", bytesSent);
                }
                break;
                case RDM_PID_DMX_START_ADDRESS:
                {
                    rdm_parameters_t *params = &rdm_parameters[dmx_num];
                    const uint16_t addr = bswap16(params->device_info.start_address);
                    const size_t bytesSent = rdm_send_get_param_response(dmx_num, header.source_uid, header.tn,
                                                                         RDM_PID_DMX_START_ADDRESS, header.sub_device, &addr, sizeof(addr));
                    ESP_LOGI("RDM DBG", "Sent GET RDM_PID_DMX_START_ADDRESS response. %d bytes", bytesSent);
                }
                break;
                case RDM_PID_DMX_PERSONALITY:
                {
                    rdm_parameters_t *params = &rdm_parameters[dmx_num];
                    const uint8_t data[] = {params->device_info.current_personality, params->device_info.personality_count};
                    const size_t bytesSent = rdm_send_get_param_response(dmx_num, header.source_uid, header.tn,
                                                                         RDM_PID_DMX_PERSONALITY, header.sub_device, data, sizeof(data));
                    ESP_LOGI("RDM DBG", "Sent GET RDM_PID_DMX_PERSONALITY response. %d bytes", bytesSent);
                }
                break;
                case RDM_PID_DMX_PERSONALITY_DESCRIPTION:
                {
                    const uint8_t requestedPersonality = ((uint8_t *)data)[24]; // FIXME find better way to get the address
                    const uint8_t personalityIdx = requestedPersonality - 1;
                    rdm_parameters_t *params = &rdm_parameters[dmx_num];
                    if(personalityIdx >= params->device_info.personality_count)
                    {
                        ESP_LOGE("rdm_client", "illegal personality");
                    }

                    rdm_client_parameters_t* client_params = &rdm_client_parameters[dmx_num];
                    const uint16_t footprint = client_params->personalities[personalityIdx].footprint;
                    const uint16_t footprintSwapped = bswap16(footprint);
                    const char* description = client_params->personalities[personalityIdx].description;
                    const size_t descriptionLen = client_params->personalities[personalityIdx].description_len;

                    uint8_t data[35];
                    data[0] = requestedPersonality;
                    memcpy(&data[1], &footprintSwapped, 2);
                    memcpy(&data[3], description, descriptionLen);

                    const size_t bytesSent = rdm_send_get_param_response(dmx_num, header.source_uid, header.tn,
                                                                         RDM_PID_DMX_PERSONALITY_DESCRIPTION, header.sub_device, data, 3 + descriptionLen);

                    ESP_LOGI("RDM DBG", "Sent Get RDM_PID_DMX_PERSONALITY_DESCRIPTION response. personality: %d, footprint: %d, description: %s ", requestedPersonality, footprint, description);
                }
                break;
                default:
                    ESP_LOGI("RDM DBG", "RDM_CC_GET_COMMAND unknown get pid: %04x", header.pid);
                    ESP_LOG_BUFFER_HEX("RDM", data, size);
                }
            }
            else if (header.cc == RDM_CC_SET_COMMAND)
            {
                switch (header.pid)
                {
                case RDM_PID_IDENTIFY_DEVICE:
                {
                    rdm_parameters[dmx_num].identify_device = ((uint8_t *)data)[24]; // FIXME find better way to get the address
                    const size_t bytesSent = rdm_send_set_command_ack_response(dmx_num, header.source_uid, header.tn, header.sub_device, RDM_PID_IDENTIFY_DEVICE);
                    ESP_LOGI("RDM DBG", "Set identify: %d", rdm_parameters[dmx_num].identify_device);
                    if (rdm_client_parameters[dmx_num].identify_cb)
                    {
                        rdm_client_parameters[dmx_num].identify_cb(rdm_parameters[dmx_num].identify_device);
                    }
                }
                break;
                case RDM_PID_DMX_START_ADDRESS:
                {
                    rdm_parameters_t *params = &rdm_parameters[dmx_num];
                    uint16_t addr;
                    memcpy(&addr, data + 24, 2); // FIXME find better way to get the address
                    params->device_info.start_address = bswap16(addr);
                    const size_t bytesSent = rdm_send_set_command_ack_response(dmx_num, header.source_uid, header.tn, header.sub_device, RDM_PID_DMX_START_ADDRESS);
                    ESP_LOGI("RDM DBG", "Set start address: %d", params->device_info.start_address);
                    if (rdm_client_parameters[dmx_num].address_cb)
                    {
                        rdm_client_parameters[dmx_num].address_cb(params->device_info.start_address);
                    }
                }
                break;
                case RDM_PID_DEVICE_LABEL:
                {
                    if (header.pdl > 32)
                    {
                        ESP_LOGE("RDM", "header.pdl too large: %d", header.pdl);
                        return;
                    }
                    rdm_parameters_t *params = &rdm_parameters[dmx_num];
                    memset(params->device_label, 0, sizeof(params->device_label));
                    memcpy(params->device_label, data + 24, header.pdl); // FIXME find better way to get the address
                    params->device_label_len = header.pdl;
                    rdm_send_set_command_ack_response(dmx_num, header.source_uid, header.tn, header.sub_device, RDM_PID_DEVICE_LABEL);
                    ESP_LOGI("RDM DBG", "Set device label: %s", params->device_label);
                    if (rdm_client_parameters[dmx_num].label_cb)
                    {
                        rdm_client_parameters[dmx_num].label_cb(params->device_label, params->device_label_len);
                    }
                }
                break;
                case RDM_PID_DMX_PERSONALITY:
                {
                    if(header.pdl != 1)
                    {
                        ESP_LOGE("RDM", "header.pdl invalid: %d", header.pdl);
                        return;
                    }
                    rdm_parameters_t *params = &rdm_parameters[dmx_num];
                    uint8_t personality = -1;
                    memcpy(&personality, data + 24, header.pdl);
                    rdm_send_set_command_ack_response(dmx_num, header.source_uid, header.tn, header.sub_device, RDM_PID_DMX_PERSONALITY);

                    rdm_client_set_personality(dmx_num, personality);

                    if(rdm_client_parameters[dmx_num].personality_cb != NULL)
                    {
                        rdm_client_parameters[dmx_num].personality_cb(params->device_info.current_personality);
                    }

                    ESP_LOGI("RDM DBG", "Set personality: %d", params->device_info.current_personality);
                }
                break;
                default:
                    ESP_LOGI("RDM DBG", " RDM_CC_SET_COMMAND unknown set pid: %04x", header.pid);
                    ESP_LOG_BUFFER_HEX("RDM", data, size);
                }
            }
            else
            {
                // debugPrintRdm(header);
            }
        }
    }
}
