/**
 * @file disp_spi.h
 *
 */

#ifndef DISP_SPI_H
#define DISP_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include <stdint.h>
#include "esp_system.h"
#include "driver/spi_master.h"


/*********************
 *      DEFINES
 *********************/

#define DISP_SPI_MOSI CONFIG_ILI9341_MOSI 
#define DISP_SPI_CLK  CONFIG_ILI9341_MISO
#define DISP_SPI_CS   CONFIG_ILI9341_CS 


/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/
void disp_spi_init(spi_host_device_t spihost);
void disp_spi_send_data(uint8_t * data, uint16_t length);
void disp_spi_send_colors(uint8_t * data, uint16_t length);

/**********************
 *      MACROS
 **********************/


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*DISP_SPI_H*/
