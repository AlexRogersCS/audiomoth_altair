/****************************************************************************
 * main.c
 * openacousticdevices.info
 * March 2025
 *****************************************************************************/

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "em_usb.h"

#include "audiomoth.h"
#include "usbserial.h"

#include "i8080.h"
#include "basic8k20.h"

/* Sleep and LED constants */

#define DEFAULT_WAIT_INTERVAL                   200
#define LED_FLASH_THRESHOLD                     50000
#define SWITCH_CHANGE_THRESHOLD                 200000

/* USB CDC constants */

#define CDC_BULK_EP_SIZE                        USB_FS_BULK_EP_MAXSIZE
#define CDC_USB_RX_BUF_SIZ                      CDC_BULK_EP_SIZE

#define CDC_USB_BUF_SIZ                         128

/* Altair 8800 constants */

#define MEMORY_SIZE                             (8 * 1024)

#define RECEIVE_BUFFER_SIZE                     128

/* Altair 8800 state */

static struct i8080 cpu;

static uint8_t memory[MEMORY_SIZE];

static volatile bool sendingToTeleprinter;

static volatile uint32_t receiveBufferReadIndex;

static volatile uint32_t receiveBufferWriteIndex;

static char receiveBuffer[RECEIVE_BUFFER_SIZE];

/* USB CDC state */

STATIC_UBUF(usbRxBuffer, CDC_USB_BUF_SIZ);
STATIC_UBUF(usbTxBuffer, CDC_USB_BUF_SIZ);

/* USB CDC data structures */

SL_PACK_START(1)
typedef struct {
    uint32_t dwDTERate;               
    uint8_t  bCharFormat;             
    uint8_t  bParityType;             
    uint8_t  bDataBits;               
    uint8_t  dummy;                   
} SL_ATTRIBUTE_PACKED cdcLineCoding_TypeDef;
SL_PACK_END()

SL_ALIGN(4)
SL_PACK_START(1)
static cdcLineCoding_TypeDef SL_ATTRIBUTE_ALIGN(4) cdcLineCoding = {9600, 0, 0, 8, 0};
SL_PACK_END()

/* USB CDC line coding handler */

static int LineCodingReceived(USB_Status_TypeDef status, uint32_t xferred, uint32_t remaining) {

    if ((status == USB_STATUS_OK) && (xferred == 7)) return USB_STATUS_OK;

    return USB_STATUS_REQ_ERR;

}

/* USB CDC function prototypes */

static int UsbDataSent(USB_Status_TypeDef status, uint32_t xferred, uint32_t remaining);

static int UsbDataReceived(USB_Status_TypeDef status, uint32_t xferred, uint32_t remaining);

/* USB CDC functions */

static int setupCmd(const USB_Setup_TypeDef *setup) {

    int retVal = USB_STATUS_REQ_UNHANDLED;
   
    if ( ( setup->Type == USB_SETUP_TYPE_CLASS) && ( setup->Recipient == USB_SETUP_RECIPIENT_INTERFACE)) {

        switch (setup->bRequest) {

            case USB_CDC_GETLINECODING:

                if ((setup->wValue == 0) && (setup->wIndex == CDC_CTRL_INTERFACE_NO) && (setup->wLength == 7) && (setup->Direction == USB_SETUP_DIR_IN)) {
            
                    USBD_Write(0, (void*) &cdcLineCoding, 7, NULL);
    
                    retVal = USB_STATUS_OK;

                }

                break;
                
            case USB_CDC_SETLINECODING:

                if ((setup->wValue == 0) && (setup->wIndex == CDC_CTRL_INTERFACE_NO) && (setup->wLength == 7) && (setup->Direction != USB_SETUP_DIR_IN)) {

                    USBD_Read(0, (void*) &cdcLineCoding, 7, LineCodingReceived);
        
                    retVal = USB_STATUS_OK;
                
                }

                break;

            case USB_CDC_SETCTRLLINESTATE:

                if ((setup->wIndex == CDC_CTRL_INTERFACE_NO) && (setup->wLength == 0)) {

                    retVal = USB_STATUS_OK;

                }

                break;
                
        }
            
    }
 
    return retVal;

}

static void stateChange(USBD_State_TypeDef oldState, USBD_State_TypeDef newState) {

    if (newState == USBD_STATE_CONFIGURED) {

        USBD_Read(CDC_EP_DATA_OUT, (void*)usbRxBuffer, CDC_USB_RX_BUF_SIZ, UsbDataReceived);

    }

}

/* USB data sent and receive callbacks */

static int UsbDataSent(USB_Status_TypeDef status, uint32_t xferred, uint32_t remaining) {

    sendingToTeleprinter = false;

    return USB_STATUS_OK;

}

static int UsbDataReceived(USB_Status_TypeDef status, uint32_t xferred, uint32_t remaining) {
    
    if ((status == USB_STATUS_OK) && (xferred > 0)) {

        uint32_t index = 0;

        while (index < xferred) {

            receiveBuffer[receiveBufferWriteIndex] = usbRxBuffer[index];

            receiveBufferWriteIndex = (receiveBufferWriteIndex + 1) % RECEIVE_BUFFER_SIZE;

            index += 1;

        }

        USBD_Read(CDC_EP_DATA_OUT, (void*)usbRxBuffer, CDC_USB_RX_BUF_SIZ, UsbDataReceived);

    }

    return USB_STATUS_OK;

}

/* Firmware version and description */

static uint8_t firmwareVersion[AM_FIRMWARE_VERSION_LENGTH] = {1, 0, 0};

static uint8_t firmwareDescription[AM_FIRMWARE_DESCRIPTION_LENGTH] = "AudioMoth-Altair-8800";

/* AudioMoth interrupt handlers */

inline void AudioMoth_handleSwitchInterrupt() { }

inline void AudioMoth_handleMicrophoneChangeInterrupt() { }

inline void AudioMoth_handleMicrophoneInterrupt(int16_t sample) { }

inline void AudioMoth_handleDirectMemoryAccessInterrupt(bool isPrimaryBuffer, int16_t **nextBuffer) { }

/* AudioMoth USB message handlers */

inline void AudioMoth_usbFirmwareVersionRequested(uint8_t **firmwareVersionPtr) {

    *firmwareVersionPtr = firmwareVersion;

}

inline void AudioMoth_usbFirmwareDescriptionRequested(uint8_t **firmwareDescriptionPtr) {

    *firmwareDescriptionPtr = firmwareDescription;

}

inline void AudioMoth_usbApplicationPacketRequested(uint32_t messageType, uint8_t *transmitBuffer, uint32_t size) { }

inline void AudioMoth_usbApplicationPacketReceived(uint32_t messageType, uint8_t* receiveBuffer, uint8_t *transmitBuffer, uint32_t size) { }

/* Intel 8080 IN and OUT handlers */

uint handle_input(struct i8080 *cpu, uint device) {

    bool noCharacterAvailable = receiveBufferReadIndex == receiveBufferWriteIndex;

    if (device == 0x00) {
        
        //if (simulationType == BASIC_4k_10) {

            //return (sendingToTeleprinter ? 0x00 : 0x02) | (noCharacterAvailable ? 0x00 : 0x20);

        //} else {

            return (sendingToTeleprinter ? 0x80 : 0x00) | (noCharacterAvailable ? 0x01 : 0x00);

        //}

    } else if (device == 0x01) {
        
        if (noCharacterAvailable) return 0x00;

        uint8_t data = receiveBuffer[receiveBufferReadIndex];
        
        if (data >= 'a' && data <= 'z') data += 'A' - 'a';

        receiveBufferReadIndex = (receiveBufferReadIndex + 1) % RECEIVE_BUFFER_SIZE;

        return data;

    }

    return 0x00;

}
  
void handle_output(struct i8080 *cpu, uint device, uint data) {

    if (device == 0x01) {

        sendingToTeleprinter = true;

        usbTxBuffer[0] = data & 0x7F;

        USBD_Write(CDC_EP_DATA_IN, (void*)usbTxBuffer, 1, UsbDataSent);

    }

}

/* Main function */

int main(void) {

    /* Initialise device */

    AudioMoth_initialise();

    /* Respond to switch state */

    AM_switchPosition_t switchPosition = AudioMoth_getSwitchPosition();

    if (switchPosition == AM_SWITCH_USB) {

        /* Use conventional USB routine */

        AudioMoth_handleUSB();

        /* Power down */

        AudioMoth_powerDownAndWakeMilliseconds(DEFAULT_WAIT_INTERVAL);

    }

    /* Enable the serial USB interface */

    USBD_Init(&initstruct);

    /* Main loop */

    while (true) {

        /* Show starting text */

        AudioMoth_delay(DEFAULT_WAIT_INTERVAL);

        uint32_t length = sprintf((char*)usbTxBuffer, "\033[2J\033[H");

        USBD_Write(CDC_EP_DATA_IN, (void*)usbTxBuffer, length, UsbDataSent);

        AudioMoth_delay(DEFAULT_WAIT_INTERVAL);

        /* Initialise Intel 8080 */
        
        cpu.memsize = MEMORY_SIZE;
        
        cpu.memory = (char*)memory;

        i8080_reset(&cpu);

        cpu.input_handler = handle_input;
        
        cpu.output_handler = handle_output;

        receiveBufferReadIndex = 0;

        receiveBufferWriteIndex = 0;

        sendingToTeleprinter = false;

        /* Initialise the memory */

        memset(memory, 0, sizeof(memory));

        /* Copy program to memory */

        memcpy(memory, basic8k20, sizeof(basic8k20));

        /* Main loop */

        bool ledState = false;

        uint32_t ledFlashCounter = 0;

        uint32_t switchChangeCounter = 0;

        while (true) {

            /* Check switch positions */

            AM_switchPosition_t currentSwitchPosition = AudioMoth_getSwitchPosition();

            switchChangeCounter = currentSwitchPosition != switchPosition ? switchChangeCounter + 1 : 0;
            
            if (switchChangeCounter > SWITCH_CHANGE_THRESHOLD) {

                switchPosition = currentSwitchPosition;

                break;

            }

            /* Flash LED */

            ledFlashCounter += 1;

            if (ledFlashCounter > LED_FLASH_THRESHOLD) {

                ledState = !ledState;

                if (switchPosition == AM_SWITCH_DEFAULT) {

                    AudioMoth_setGreenLED(ledState);

                } else {

                    AudioMoth_setRedLED(ledState);

                }

                ledFlashCounter = 0;

            }

            /* Perform Intel 8080 step */

            i8080_step(&cpu);

            /* Feed watchdog */

            AudioMoth_feedWatchdog();

        }

        /* Turn off LED */

        AudioMoth_setBothLED(false);

        /* Exit if switch position is USB/OFF */

        if (switchPosition == AM_SWITCH_USB) break;

    }

    /* Power down */

    AudioMoth_powerDownAndWakeMilliseconds(DEFAULT_WAIT_INTERVAL);

}