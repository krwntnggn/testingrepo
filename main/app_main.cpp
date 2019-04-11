/* Deep sleep wake up example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp32/ulp.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/touch_pad.h"
#include "driver/adc.h"
#include "driver/rtc_io.h"
#include "driver/i2c.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include "soc/rtc.h"
#include "hdc1080.h"
#include "TheThingsNetwork.h"
#include "CayenneLPP.h"


#define _I2C_NUMBER(num) I2C_NUM_##num
#define I2C_NUMBER(num) _I2C_NUMBER(num)


#define I2C_MASTER_SCL_IO CONFIG_I2C_MASTER_SCL               /*!< gpio number for I2C master clock */
#define I2C_MASTER_SDA_IO CONFIG_I2C_MASTER_SDA               /*!< gpio number for I2C master data  */
#define I2C_MASTER_NUM I2C_NUMBER(CONFIG_I2C_MASTER_PORT_NUM) /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ CONFIG_I2C_MASTER_FREQUENCY        /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE 0                           /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0                           /*!< I2C master doesn't need buffer */


// Pins and other resources
#define TTN_SPI_HOST      VSPI_HOST
#define TTN_SPI_DMA_CHAN  1
#define TTN_PIN_SPI_SCLK  CONFIG_RFM95W_SCLK 
#define TTN_PIN_SPI_MOSI  CONFIG_RFM95W_MOSI
#define TTN_PIN_SPI_MISO  CONFIG_RFM95W_MISO 
#define TTN_PIN_NSS       CONFIG_RFM95W_NSS
#define TTN_PIN_RXTX      TTN_NOT_CONNECTED
#define TTN_PIN_RST       CONFIG_RFM95W_RST
#define TTN_PIN_DIO0      CONFIG_RFM95W_DIO0
#define TTN_PIN_DIO1      CONFIG_RFM95W_DIO1

// NOTE:
// Lorawan specification defines the maximum payload (num. of bytes)
// to send depending on effecitive modulation rate. At SF12, this translates
// to 51 bytes.
#define MAX_LORA_PAYLOAD 51

// NOTE:
// The LoRaWAN frequency and the radio chip must be configured by running 'make menuconfig'.
// Go to Components / The Things Network, select the appropriate values and save.

// Copy the below hex string from the "Device EUI" field
// on your device's overview page in the TTN console.
const char *devEui = CONFIG_TTN_DEV_EUI;

// Copy the below two lines from bottom of the same page
const char *appEui = CONFIG_TTN_APP_EUI;
const char *appKey = CONFIG_TTN_APP_KEY;

static TheThingsNetwork ttn;
static CayenneLPP       lpp(MAX_LORA_PAYLOAD);

static esp_err_t i2c_master_init(void);
static void gpio_led_init(void);
static int32_t get_restart_counter(uint32_t);
static void lora_module_init(bool);
void ttn_send_thread(void* pvParameter);

void send_ttn_message(void);
void receive_ttn_message(const uint8_t* message, size_t length, port_t port);

extern "C" void app_main()
{
    int restart_counter = get_restart_counter( -1 );
    bool do_provision = (restart_counter <= 0);
    i2c_master_init();
    gpio_led_init();

    // Initialize TTN, do provisioning
    // if restart_counter is zero (or invalid = -1)
    lora_module_init(do_provision);

    if ( hdc1080_init(I2C_MASTER_NUM) != ESP_OK)
    {
        printf("Failed to initialize HDC1080 sensor!\n");
    }


    printf("Joining TTN ...\n");
    if (ttn.join())
    {
        printf("Starting lorawan senter thread! ...\n");
        // Start the lorawan sender
        xTaskCreate(ttn_send_thread, "ttn_send_thread", 1024 * 4, (void* )0, 3, NULL);
    }else
    {
        printf("Join failed!!\n");
    }

}

void ttn_send_thread(void* pvParameter)
{
    while (1) {
        /* Set LED on (output low) */
        gpio_set_level((gpio_num_t) CONFIG_WAKEUP_LED, 0);

        send_ttn_message();

        gpio_set_level((gpio_num_t) CONFIG_WAKEUP_LED, 1);
        printf("Sleep (%d) seconds ... \n", CONFIG_TX_WAIT_INTERVAL);
        vTaskDelay(CONFIG_TX_WAIT_INTERVAL * 1000 / portTICK_PERIOD_MS);
    }
}


/**
* @brief i2c master initialization
*/
static esp_err_t i2c_master_init()
{
   i2c_port_t i2c_master_port = I2C_MASTER_NUM;
   i2c_config_t conf;
   conf.mode = I2C_MODE_MASTER;
   conf.sda_io_num = (gpio_num_t) I2C_MASTER_SDA_IO;
   conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
   conf.scl_io_num = (gpio_num_t) I2C_MASTER_SCL_IO;
   conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
   conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
   i2c_param_config( i2c_master_port, &conf);
   return i2c_driver_install(i2c_master_port, conf.mode,
                             I2C_MASTER_RX_BUF_DISABLE,
                             I2C_MASTER_TX_BUF_DISABLE, 0);
}

static void gpio_led_init()
{
    gpio_pad_select_gpio((gpio_num_t) CONFIG_WAKEUP_LED);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction((gpio_num_t) CONFIG_WAKEUP_LED, GPIO_MODE_OUTPUT);
}

static int32_t get_restart_counter(uint32_t initval)
{
    int32_t restart_counter = initval;
    esp_err_t err;

    // Initialize the NVS (non-volatile storage) for saving and restoring the keys
    err = nvs_flash_init();
    ESP_ERROR_CHECK(err);

    nvs_handle my_handle;
    printf("Opening Non-Volatile Storage (NVS) handle ...");
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
    {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return -1;
    }
    else
    {
        printf("Done!\n");

        // Read 
        printf("Reading restart counter from NVS ...");
        err = nvs_get_i32(my_handle, "restart_counter", &restart_counter);
        switch(err)
        {
            case ESP_OK:
                printf("Done\n");
                printf("Restart counter = %d\n", restart_counter);
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                printf("The value is not initialized yet!\n");
                break;
            default:
                printf("Error (%s) reading!\n", esp_err_to_name(err));
        }

        // Write
        printf("Updating restart counter in NVS ...");
        restart_counter ++;
        err = nvs_set_i32(my_handle, "restart_counter", restart_counter);
        printf((err != ESP_OK) ? "Failed!\n" : "Done!\n");

        // Commit written value.
        // After setting any values, nvs_commit() must be called to ensure changes
        // are written to flash storage. Implementations may write to storage at other times.
        // but this is not guaranteed.
        printf("Committing updates in NVS ... ");
        err = nvs_commit(my_handle);
        printf((err != ESP_OK) ? "Failed!\n" : "Done!\n");

        // Close
        nvs_close(my_handle);

    }
    // Return the value of the restart counter
    return restart_counter;


}

static void lora_module_init(bool do_provision)
{
    esp_err_t err;
    // Initialize the GPIO ISR handler service
    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    ESP_ERROR_CHECK(err);

    // Initialize SPI bus
    spi_bus_config_t spi_bus_config;
    spi_bus_config.miso_io_num = TTN_PIN_SPI_MISO;
    spi_bus_config.mosi_io_num = TTN_PIN_SPI_MOSI;
    spi_bus_config.sclk_io_num = TTN_PIN_SPI_SCLK;
    spi_bus_config.quadwp_io_num = -1;
    spi_bus_config.quadhd_io_num = -1;
    spi_bus_config.max_transfer_sz = 0;
    err = spi_bus_initialize(TTN_SPI_HOST, &spi_bus_config, TTN_SPI_DMA_CHAN);
    ESP_ERROR_CHECK(err);

    // Configure the SX127x pins
    ttn.configurePins(TTN_SPI_HOST, TTN_PIN_NSS, TTN_PIN_RXTX, TTN_PIN_RST, TTN_PIN_DIO0, TTN_PIN_DIO1);

    if (do_provision)
    {
        // Only do provision if we are starting for the first time.
        printf("Starting TTN provisioning!\n");
        ttn.provision(devEui, appEui, appKey);
    }

    ttn.onMessage(receive_ttn_message);
}

void send_ttn_message()
{
    float currTemp = 0.0;
    float currHumid = 0.0;
    float waterLevel = 0.0;

    lpp.reset();

    printf("Waiting for next available transmit event ...\n");
    if ( hdc1080_read_temperature(I2C_MASTER_NUM, &currTemp, &currHumid) == ESP_OK)
    {

        printf("Current temperature = %.2f C, Relative Humidity = %.2f %%\n", 
                currTemp, currHumid);
    }

    // for now random values for water level
    waterLevel = rand() % 10 + 1;
    lpp.addTemperature(0, currTemp);
    lpp.addRelativeHumidity(1, currHumid);    
    lpp.addAnalogInput(2, waterLevel); 


    TTNResponseCode res = ttn.transmitMessage(lpp.getBuffer(), lpp.getSize());
    printf(res == kTTNSuccessfulTransmission ? "Message sent.\n" : "Transmission failed.\n");

}

void receive_ttn_message(const uint8_t* message, size_t length, port_t port)
{
    printf("Message of %d bytes received on port %d:", length, port);
    for (int i = 0; i < length; i++)
        printf(" %02x", message[i]);
    printf("\n");
}


