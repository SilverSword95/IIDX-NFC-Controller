/*
 * Pico Game Controller
 * @author SpeedyPotato
 */
#define PICO_GAME_CONTROLLER_C

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/board.h"
#include "pico/bootrom.h"
#include "board_defs.h"
#include "encoders.pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "usb_descriptors.h"
// clang-format off
#include "debounce/debounce_include.h"
#include "rgb/rgb_include.h"
// clang-format on

#include "nfc/nfc_defs.h"
#include "nfc/nfc.h"

#include "controller_simulator.h" //PlayStation mode
#define TUD_MOUNT_TIMEOUT	1500
#define ENCODER_Z_MILLIS_TOLERANCE 300  // Amount of miliseconds to wait and change state of turntable buttons

PIO pio, pio_1;
uint32_t enc_val[ENC_GPIO_SIZE];
uint32_t prev_enc_val[ENC_GPIO_SIZE];
int cur_enc_val[ENC_GPIO_SIZE];
uint8_t joy_prev;
bool tt1_but = 0;
bool tt2_but = 0;

bool prev_sw_val[SW_GPIO_SIZE];
uint64_t sw_timestamp[SW_GPIO_SIZE];

bool kbm_report;

uint64_t reactive_timeout_timestamp;

//void (*ws2812b_mode)();
void (*loop_mode)();
uint16_t (*debounce_mode)();
bool joy_mode_check = true;
bool turbo = false;
bool tud_mount_status = false;
bool rgb_nfc_enabled = false;
bool psx_enabled = false;
bool lr2_enabled = false;
bool nfc_enabled = false;

volatile int32_t encZmillis = 0;

union {
  struct {
    uint8_t buttons[LED_GPIO_SIZE];
    RGB_t rgb[WS2812B_LED_ZONES];
  } lights;
  uint8_t raw[LED_GPIO_SIZE + WS2812B_LED_ZONES * 3];
} lights_report;

/**
 * WS2812B Lighting
 * @param counter Current number of WS2812B cycles
 **/
void ws2812b_update(uint32_t counter, bool turbo) {
  if (time_us_64() - reactive_timeout_timestamp >= REACTIVE_TIMEOUT_MAX) {
	if (turbo) {
		turbocharger_color_cycle(counter);
	} else {
		ws2812b_color_cycle(counter);
	}
    //ws2812b_mode(counter);
  } else {
    for (int i = 0; i < WS2812B_LED_ZONES; i++) {
      for (int j = 0; j < WS2812B_LEDS_PER_ZONE; j++) {
        put_pixel(urgb_u32(lights_report.lights.rgb[i].r,
                           lights_report.lights.rgb[i].g,
                           lights_report.lights.rgb[i].b));
      }
    }
  }
}

/**
 * HID/Reactive Lights
 **/
void update_lights() {
  for (int i = 0; i < LED_GPIO_SIZE; i++) {
    if (time_us_64() - reactive_timeout_timestamp >= REACTIVE_TIMEOUT_MAX) {
      if (!gpio_get(SW_GPIO[i])) {
        gpio_put(LED_GPIO[i], 1);
      } else {
        gpio_put(LED_GPIO[i], 0);
      }
    } else {
      if (lights_report.lights.buttons[i] == 0) {
        gpio_put(LED_GPIO[i], 0);
      } else {
        gpio_put(LED_GPIO[i], 1);
      }
    }
  }
}

struct report {
  uint16_t buttons;
  uint8_t joy0 = 0;
  uint8_t joy1;
} report;

/**
 * NFC Related Commands
 **/
 
static struct {
    uint8_t current[9];
    uint8_t reported[9];
    uint64_t report_time;
} hid_cardio;

void report_hid_cardio()
{
    if (!tud_hid_ready()) {
        return;
    }

    uint64_t now = time_us_64();

    if ((memcmp(hid_cardio.current, hid_cardio.reported, 9) != 0) &&
        (now - hid_cardio.report_time > 1000000)) {

        tud_hid_n_report(1, hid_cardio.current[0], hid_cardio.current + 1, 8);
        memcpy(hid_cardio.reported, hid_cardio.current, 9);
        hid_cardio.report_time = now;
    }
}

void detect_card()
{
    static nfc_card_t old_card = { NFC_CARD_NULL };

    nfc_card_t card = nfc_detect_card();
    switch (card.card_type) {
        case NFC_CARD_MIFARE:
            hid_cardio.current[0] = REPORT_ID_EAMU;
            hid_cardio.current[1] = 0xe0;
            hid_cardio.current[2] = 0x04;
            if (card.len == 4) {
                memcpy(hid_cardio.current + 3, card.uid, 4);
                memcpy(hid_cardio.current + 7, card.uid, 2);
            } else if (card.len == 7) {
                memcpy(hid_cardio.current + 3, card.uid + 1, 6);
            }
            break;
        case NFC_CARD_FELICA:
            hid_cardio.current[0] = REPORT_ID_FELICA;
            memcpy(hid_cardio.current + 1, card.uid, 8);
            break;
        case NFC_CARD_VICINITY:
            hid_cardio.current[0] = REPORT_ID_EAMU;
            memcpy(hid_cardio.current + 1, card.uid, 8);
            break;
        default:
            memset(hid_cardio.current, 0, 9);
    }
    if (memcmp(&old_card, &card, sizeof(card)) == 0) {
        return;
    }

    if (card.card_type != NFC_CARD_NULL) {
        const char *card_type_str[3] = { "MIFARE", "FeliCa", "15693" };
        printf("\n%s:", card_type_str[card.card_type - 1]);
        for (int i = 0; i < card.len; i++) {
            printf(" %02x", hid_cardio.current[i]);
        }
    }

    old_card = card;
}

void wait_loop()
{
    tud_task();
}

/**
 * Gamepad Mode
 **/
void joy_mode() {
	joy_prev = report.joy0;
    // find the delta between previous and current enc_val
    for (int i = 0; i < ENC_GPIO_SIZE; i++) {
      cur_enc_val[i] +=
          ((enc_val[i] - prev_enc_val[i]));
      while (cur_enc_val[i] < 0) cur_enc_val[i] = ENC_PULSE + cur_enc_val[i];
      cur_enc_val[i] %= ENC_PULSE;
      prev_enc_val[i] = enc_val[i];
    }
	
    report.joy0 = ((double)cur_enc_val[0] / ENC_PULSE) * (UINT8_MAX + 1);
	report.joy1 = 127;
	
	//Virtual turntable buttons (mainly for LR2 or PlayStation)
	if (lr2_enabled or psx_enabled){
	if (((joy_prev < report.joy0) and (joy_prev != 0 and report.joy0 != 254)) or (joy_prev == 254 and report.joy0 == 0)){
		tt1_but = 1;
		tt2_but = 0;
		encZmillis = to_ms_since_boot(get_absolute_time());
	} else if (((joy_prev > report.joy0) and (joy_prev != 254 and report.joy0 != 0)) or (joy_prev == 0 and report.joy0 == 254)){
		tt1_but = 0;
		tt2_but = 1;
		encZmillis = to_ms_since_boot(get_absolute_time());
	} else {
		if (to_ms_since_boot(get_absolute_time()) - encZmillis > ENCODER_Z_MILLIS_TOLERANCE) {
			tt1_but = 0;
			tt2_but = 0;
		}
	}
	
	//Modifiying report
	report.buttons |= (uint16_t)tt1_but << 10;
	report.buttons |= (uint16_t)tt2_but << 11;
	}

    if (tud_hid_ready()) { tud_hid_n_report(0x00, REPORT_ID_JOYSTICK, &report, sizeof(report)); }
}

/**
 * Keyboard Mode
 **/
void key_mode() {
  if (tud_hid_ready()) {  // Wait for ready, updating mouse too fast hampers
                          // movement
    if (kbm_report) {
      /*------------- Keyboard -------------*/
      uint8_t nkro_report[32] = {0};
      for (int i = 0; i < SW_GPIO_SIZE; i++) {
        if ((report.buttons >> i) % 2 == 1) {
          uint8_t bit = SW_KEYCODE[i] % 8;
          uint8_t byte = (SW_KEYCODE[i] / 8) + 1;
          if (SW_KEYCODE[i] >= 240 && SW_KEYCODE[i] <= 247) {
            nkro_report[0] |= (1 << bit);
          } else if (byte > 0 && byte <= 31) {
            nkro_report[byte] |= (1 << bit);
          }
        }
      }
      tud_hid_n_report(0x00, REPORT_ID_KEYBOARD, &nkro_report,
                       sizeof(nkro_report));
    } else {
      /*------------- Mouse -------------*/
      // find the delta between previous and current enc_val
      int delta[ENC_GPIO_SIZE] = {0};
      for (int i = 0; i < ENC_GPIO_SIZE; i++) {
        delta[i] = (enc_val[i] - prev_enc_val[i]);
        prev_enc_val[i] = enc_val[i];
      }
      tud_hid_mouse_report(REPORT_ID_MOUSE, 0x00, delta[0] * MOUSE_SENS, 0, 0,
                           0);
    }
    // Alternate reports
    kbm_report = !kbm_report;
  }
}

/**
 * Update Input States
 * Note: Switches are pull up, negate value
 **/
void update_inputs() {
  for (int i = 0; i < SW_GPIO_SIZE; i++) {
    // If switch gets pressed, record timestamp
    if (prev_sw_val[i] == false && !gpio_get(SW_GPIO[i]) == true) {
      sw_timestamp[i] = time_us_64();
    }
    prev_sw_val[i] = !gpio_get(SW_GPIO[i]);
  }
}

/**
 * DMA Encoder Logic For 2 Encoders
 **/
void dma_handler() {
  uint i = 1;
  int interrupt_channel = 0;
  while ((i & dma_hw->ints0) == 0) {
    i = i << 1;
    ++interrupt_channel;
  }
  dma_hw->ints0 = 1u << interrupt_channel;
  if (interrupt_channel < 4) {
    dma_channel_set_read_addr(interrupt_channel, &pio->rxf[interrupt_channel],
                              true);
  }
}

/**
 * Second Core Runnable
 **/
void core1_entry() {
  uint32_t counter = 0;
  while (1) {
    ws2812b_update(++counter, turbo);
	if (nfc_enabled) {
		detect_card();
		report_hid_cardio();
	}
    sleep_ms(5);
  }
}

/**
 * Initialize Board Pins
 **/
void init() {
  // Set up the state machine for encoders
  pio = pio0;
  uint offset = pio_add_program(pio, &encoders_program);

  // Setup Encoders
  for (int i = 0; i < ENC_GPIO_SIZE; i++) {
    enc_val[i], prev_enc_val[i], cur_enc_val[i] = 0;
    encoders_program_init(pio, i, offset, ENC_GPIO[i], ENC_DEBOUNCE);

    dma_channel_config c = dma_channel_get_default_config(i);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, i, false));

    dma_channel_configure(i, &c,
                          &enc_val[i],   // Destination pointer
                          &pio->rxf[i],  // Source pointer
                          0x10,          // Number of transfers
                          true           // Start immediately
    );
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    dma_channel_set_irq0_enabled(i, true);
  }

  reactive_timeout_timestamp = time_us_64();

  // Set up WS2812B
  pio_1 = pio1;
  uint offset2 = pio_add_program(pio_1, &ws2812_program);
  ws2812_program_init(pio_1, ENC_GPIO_SIZE, offset2, WS2812B_GPIO, 800000,
                      false);

  // Setup Button GPIO
  for (int i = 0; i < SW_GPIO_SIZE; i++) {
    prev_sw_val[i] = false;
    sw_timestamp[i] = 0;
    gpio_init(SW_GPIO[i]);
    gpio_set_function(SW_GPIO[i], GPIO_FUNC_SIO);
    gpio_set_dir(SW_GPIO[i], GPIO_IN);
    gpio_pull_up(SW_GPIO[i]);
  }

  // Setup LED GPIO
  for (int i = 0; i < LED_GPIO_SIZE; i++) {
    gpio_init(LED_GPIO[i]);
    gpio_set_dir(LED_GPIO[i], GPIO_OUT);
  }

  // Set listener bools
  kbm_report = false;

  // Joy/KB Mode Switching
  if (!gpio_get(SW_GPIO[0])) {
    loop_mode = &key_mode;
    joy_mode_check = false;
  } else {
    loop_mode = &joy_mode;
    joy_mode_check = true;
  }

  // RGB Mode Switching
  if (!gpio_get(SW_GPIO[1])) {
	turbo = true;
  }
  
  // LR2 Mode Switching
  if (!gpio_get(SW_GPIO[5])) {
	lr2_enabled = true;
  }
  
  // Update Mode Switching
  if (!gpio_get(SW_GPIO[6])) {
    reset_usb_boot(0, 2);
  }

  // Debouncing Mode
  debounce_mode = &debounce_eager;

  // Disable RGB
  if (gpio_get(SW_GPIO[3])) {
    rgb_nfc_enabled = true;
  }
}

/**
 * Main Loop Function
 **/
int main(void) {
  board_init();
  init();
  tusb_init();
  stdio_init_all();

  while (1) {
    tud_task();  // tinyusb device task
    update_inputs();
    report.buttons = debounce_mode();
	if(to_ms_since_boot(get_absolute_time()) > TUD_MOUNT_TIMEOUT && !tud_mount_status && !psx_enabled) { psx_init(); psx_enabled = true; } //PlayStation Mode Switch
    loop_mode();
	if (psx_enabled == true) { psx_task(report.buttons); }
    update_lights();
  }

  return 0;
}

// Invoked when device is mounted
void tud_mount_cb(void) {
	tud_mount_status = true;
	if (rgb_nfc_enabled) {
		nfc_enabled = nfc_init(wait_loop);
		multicore_launch_core1(core1_entry);
	}
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t* buffer,
                               uint16_t reqlen) {
  // TODO not Implemented
  (void)itf;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;

  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const* buffer,
                           uint16_t bufsize) {
  (void)itf;
  if (report_id == 2 && report_type == HID_REPORT_TYPE_OUTPUT &&
      bufsize >= sizeof(lights_report))  // light data
  {
    size_t i = 0;
    for (i; i < sizeof(lights_report); i++) {
      lights_report.raw[i] = buffer[i];
    }
    reactive_timeout_timestamp = time_us_64();
  }
}
