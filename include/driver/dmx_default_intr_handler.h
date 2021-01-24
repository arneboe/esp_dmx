#pragma once

#include "driver/dmx_ctrl.h"
#include "esp_system.h"
#include "hal/dmx_hal.h"
#include "hal/uart_hal.h"

#include "driver/gpio.h" // TODO: for debugging

#ifdef CONFIG_UART_ISR_IN_IRAM
#define DMX_ISR_ATTR IRAM_ATTR
#else
#define DMX_ISR_ATTR
#endif

#define DMX_ENTER_CRITICAL_ISR(mux) portENTER_CRITICAL_ISR(mux)
#define DMX_EXIT_CRITICAL_ISR(mux)  portEXIT_CRITICAL_ISR(mux)

#define DMX_INTR_RX_BRK (UART_INTR_FRAM_ERR | UART_INTR_RS485_FRM_ERR | UART_INTR_BRK_DET)
#define DMX_INTR_RX_ERR (UART_INTR_RXFIFO_OVF | UART_INTR_PARITY_ERR | UART_INTR_RS485_PARITY_ERR)
#define DMX_INTR_RX_ALL (UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT | DMX_INTR_RX_BRK | DMX_INTR_RX_ERR)

#define DMX_INTR_TX_ALL (UART_INTR_TXFIFO_EMPTY | UART_INTR_TX_BRK_IDLE | UART_INTR_TX_DONE | UART_INTR_TX_BRK_DONE | UART_INTR_RS485_CLASH)

#define RX_ANALYZE_ON   1
#define RX_ANALYZE_BRK  2
#define RX_ANALYZE_MAB  3
#define RX_ANALYZE_DONE 4

#define BRK_TO_BRK_IS_INVALID(brk_to_brk) (brk_to_brk < DMX_RX_MIN_BRK_TO_BRK_US || brk_to_brk > DMX_RX_MAX_BRK_TO_BRK_US)

void DMX_ISR_ATTR dmx_default_intr_handler(void *arg) {
  const int64_t now = esp_timer_get_time();
  dmx_obj_t *const p_dmx = (dmx_obj_t *)arg;
  const dmx_port_t dmx_num = p_dmx->dmx_num;
  portBASE_TYPE task_awoken = pdFALSE;

  while (true) {
    const uint32_t uart_intr_status = uart_hal_get_intsts_mask(&(dmx_context[dmx_num].hal));
    if (uart_intr_status == 0) break;

    // DMX Transmit #####################################################
    if (uart_intr_status & UART_INTR_TXFIFO_EMPTY) {
      // this interrupt is triggered when the tx FIFO is empty

      uint32_t bytes_written;
      const uint32_t slots_rem = p_dmx->buf_size - p_dmx->slot_idx;
      const uint8_t *offset = p_dmx->buffer[0] + p_dmx->slot_idx;
      uart_hal_write_txfifo(&(dmx_context[dmx_num].hal), offset, slots_rem,
        &bytes_written);
      p_dmx->slot_idx += bytes_written;

      if (p_dmx->slot_idx == p_dmx->buf_size) {
        // allow tx FIFO to empty - break and idle will be written
        DMX_ENTER_CRITICAL_ISR(&(dmx_context[dmx_num].spinlock));
        uart_hal_disable_intr_mask(&(dmx_context[dmx_num].hal), UART_INTR_TXFIFO_EMPTY);
        DMX_EXIT_CRITICAL_ISR(&(dmx_context[dmx_num].spinlock));
      }

      uart_hal_clr_intsts_mask(&(dmx_context[dmx_num].hal), UART_INTR_TXFIFO_EMPTY);
    } else if (uart_intr_status & UART_INTR_TX_DONE) {
      // this interrupt is triggered when the last byte in tx fifo is written

      // switch buffers, signal end of frame, and track breaks
      memcpy(p_dmx->buffer[1], p_dmx->buffer[0], p_dmx->buf_size);
      xSemaphoreGiveFromISR(p_dmx->tx_done_sem, &task_awoken);
      p_dmx->tx_last_brk_ts = now;

      uart_hal_clr_intsts_mask(&(dmx_context[dmx_num].hal), UART_INTR_TX_DONE);
    } else if (uart_intr_status & UART_INTR_TX_BRK_DONE) {
      // this interrupt is triggered when the break is done

      uart_hal_clr_intsts_mask(&(dmx_context[dmx_num].hal), UART_INTR_TX_BRK_DONE);
    } else if (uart_intr_status & UART_INTR_TX_BRK_IDLE) {
      // this interrupt is triggered when the mark after break is done

      uart_hal_clr_intsts_mask(&(dmx_context[dmx_num].hal), UART_INTR_TX_BRK_IDLE);
    } else if (uart_intr_status & UART_INTR_RS485_CLASH) {
      // this interrupt is triggered if there is a bus collision
      // this code should only run when using RDM

      uart_hal_clr_intsts_mask(&(dmx_context[dmx_num].hal), UART_INTR_RS485_CLASH);
    }

    // DMX Recieve ####################################################
    else if (uart_intr_status & DMX_INTR_RX_ALL) {
      // this interrupt is triggered when any rx event occurs
      
      const bool rx_frame_err = (p_dmx->slot_idx == (uint16_t)-1);

      /* Check if there is data in the rx FIFO and if there is, it either reads
      the data into the driver buffer, or if there is not enough space in the
      buffer, discards it. In either case, the slot counter is incremented by
      the number of bytes received. Breaks are received as null slots so in the
      event of a break the slot counter is decremented by one. If there is a 
      frame error, discard the data and do not increment the slot counter. */

      const uint32_t rxfifo_len = dmx_hal_get_rxfifo_len(&(dmx_context[dmx_num].hal));
      if (rxfifo_len) {
        if (p_dmx->slot_idx < p_dmx->buf_size) {
          // read data from rx FIFO into the buffer
          const uint16_t slots_rem = p_dmx->buf_size - p_dmx->slot_idx + 1;
          uint8_t *offset = p_dmx->buffer[p_dmx->buf_idx] + p_dmx->slot_idx;
          int slots_rd = dmx_hal_readn_rxfifo(&(dmx_context[dmx_num].hal), 
            offset, slots_rem);
          p_dmx->slot_idx += slots_rd;
          if (uart_intr_status & DMX_INTR_RX_BRK) 
            --p_dmx->slot_idx; // break is not a slot
        } else {
          // discard bytes that can't be read into the buffer
          uart_hal_rxfifo_rst(&(dmx_context[dmx_num].hal));
          if (!rx_frame_err) {
            p_dmx->slot_idx += rxfifo_len;
            if (uart_intr_status & DMX_INTR_RX_BRK)
              --p_dmx->slot_idx; // break is not a slot
          }
        }
      }
      
      // handle data received condition
      if (uart_intr_status & UART_INTR_RXFIFO_TOUT) {
        // disable the rxfifo tout interrupt
        DMX_ENTER_CRITICAL_ISR(&(dmx_context[dmx_num].spinlock));
        uart_hal_disable_intr_mask(&(dmx_context[dmx_num].hal), UART_INTR_RXFIFO_TOUT);
        DMX_EXIT_CRITICAL_ISR(&(dmx_context[dmx_num].spinlock));
      } else {
        // enable the rxfifo tout interrupt
        DMX_ENTER_CRITICAL_ISR(&(dmx_context[dmx_num].spinlock));
        uart_hal_ena_intr_mask(&(dmx_context[dmx_num].hal), UART_INTR_RXFIFO_TOUT);
        DMX_EXIT_CRITICAL_ISR(&(dmx_context[dmx_num].spinlock));
      }

      // handle end-of-frame conditions
      if (uart_intr_status & (DMX_INTR_RX_BRK | DMX_INTR_RX_ERR)) {
        dmx_event_t event;
        
        if (uart_intr_status & DMX_INTR_RX_BRK) {
          // handle rx break
          if (p_dmx->queue != NULL && !rx_frame_err) {
            // report end-of-frame to the queue
            if (p_dmx->slot_idx <= 0 || p_dmx->slot_idx > DMX_MAX_PACKET_SIZE) {
              // invalid packet length
              event.type = DMX_ERR_PACKET_SIZE;
              event.start_code = -1;
            } else if (p_dmx->slot_idx > p_dmx->buf_size) {
              // driver buffer is too small
              event.type = DMX_ERR_BUFFER_SIZE;
              event.start_code = p_dmx->buffer[p_dmx->buf_idx][0];
            } else {
              // dmx ok
              event.type = DMX_OK;
              event.start_code = p_dmx->buffer[p_dmx->buf_idx][0];
            }

            event.size = p_dmx->slot_idx;
            if (p_dmx->rx_last_brk_ts != INT64_MIN) {
              event.packet_len = now - p_dmx->rx_last_brk_ts;
            }
            else {
              event.packet_len = -1;
            }
            if (p_dmx->rx_analyze_state == RX_ANALYZE_DONE) {
              event.brk_len = p_dmx->rx_brk_len;
              event.mab_len = p_dmx->rx_mab_len;
            } else {
              event.brk_len = -1;
              event.mab_len = -1;
            }

            xQueueSendFromISR(p_dmx->queue, (void *)&event, &task_awoken);
          }

          // tell the rx analyzer we are in a break
          if (p_dmx->rx_analyze_state == RX_ANALYZE_ON)
            p_dmx->rx_analyze_state = RX_ANALYZE_BRK;

          // switch buffers, set break timestamp, and reset slot counter
          p_dmx->buf_idx = !p_dmx->buf_idx;
          p_dmx->rx_last_brk_ts = now;
          p_dmx->slot_idx = 0;

        } else {
          // handle rx FIFO overflow or parity error
          if (p_dmx->queue != NULL && !rx_frame_err) {
            // report error condition to the queue
            event.size = p_dmx->slot_idx;
            if (uart_intr_status & UART_INTR_RXFIFO_OVF) {
              // rx FIFO overflowed
              event.type = DMX_ERR_DATA_OVERFLOW;
              event.start_code = -1;
            } else {
              // data parity error
              event.type = DMX_ERR_IMPROPER_SLOT;
              event.start_code = -1;
            }
            xQueueSendFromISR(p_dmx->queue, (void *)&event, &task_awoken);
          }

          // set frame error, don't switch buffers until break rx'd
          p_dmx->slot_idx = (uint16_t)-1;

        }
      }
      uart_hal_clr_intsts_mask(&(dmx_context[dmx_num].hal), DMX_INTR_RX_ALL);
    } else {
      // disable interrupts that shouldn't be handled
      DMX_ENTER_CRITICAL_ISR(&(dmx_context[dmx_num].spinlock));
      uart_hal_disable_intr_mask(&(dmx_context[dmx_num].hal), uart_intr_status);
      DMX_EXIT_CRITICAL_ISR(&(dmx_context[dmx_num].spinlock));
      uart_hal_clr_intsts_mask(&(dmx_context[dmx_num].hal), uart_intr_status);
    }
  }
  
  
  if (task_awoken == pdTRUE) portYIELD_FROM_ISR();
}

void IRAM_ATTR dmx_rx_analyze_isr(void *arg) {
  gpio_set_level(33, 1);
  const int64_t now = esp_timer_get_time();
  dmx_obj_t *const p_dmx = (dmx_obj_t *)arg;

  /* If the UART rx line is high and we are in a break, then the break has
  ended so we should record its length. If the UART rx line is low and we
  are in a mark after break, then the mark after break has ended and we 
  should record its length. */

  if (dmx_hal_get_rx_level(&(dmx_context[p_dmx->dmx_num].hal))) {
    if (p_dmx->rx_analyze_state == RX_ANALYZE_BRK && p_dmx->rx_last_neg_edge_ts > 0) {
      p_dmx->rx_brk_len = now - p_dmx->rx_last_neg_edge_ts;
      p_dmx->rx_analyze_state = RX_ANALYZE_MAB;
    }
    p_dmx->rx_last_pos_edge_ts = now;
  } else {
    if (p_dmx->rx_analyze_state == RX_ANALYZE_MAB) {
      p_dmx->rx_mab_len = now - p_dmx->rx_last_pos_edge_ts;
      p_dmx->rx_analyze_state = RX_ANALYZE_DONE;
    }
    p_dmx->rx_last_neg_edge_ts = now;
  }
  gpio_set_level(33, 0);
}