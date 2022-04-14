/* This file is part of ESPress0x20 and distributed under the terms of the
 * MIT license. See COPYING.
 */

#include <AsyncElegantOTA.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <QuickPID.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <max6675.h>

#include "SpriteButton.h"

#define PIN_THERMO_CLK 5
#define PIN_THERMO_CS 17
#define PIN_THERMO_DO 16
#define PIN_RELAY_BOILER 23
#define PIN_SWITCH_BREW 22
#define PIN_SWITCH_STEAM 21
#define PIN_DIMMER_ZC 18
#define PIN_DIMMER_PSM 19

#define PID_WINDOW 1000
#define PID_KP_HEAT 3
#define PID_KI_HEAT 24
#define PID_KD_HEAT 6
#define PID_KP_BREW 3
#define PID_KI_BREW 40
#define PID_KD_BREW 10
#define SENSOR_READ_TIME 500

enum machine_state {
    MACHINE_INIT,
    MACHINE_HEATING,
    MACHINE_BREWING,
    MACHINE_STEAMING,
};


Preferences    preferences;
AsyncWebServer webserver(80);

TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

machine_state current_state = MACHINE_INIT;
unsigned long last_state_change = 0;
float         boiler_temp;
float         boiler_control;
unsigned long boiler_window;
bool          boiler_on = false;
float         setpoint;
float         setpoint_steam = 248;
float         brew_offset = 14.5;
unsigned long brew_start;
unsigned long brew_end;
MAX6675       boiler_thermo(PIN_THERMO_CLK, PIN_THERMO_CS, PIN_THERMO_DO);
QuickPID      boiler_pid(&boiler_temp, &boiler_control, &setpoint);

/* Utility functions */

void
boiler_state(bool s) {
    if (s) {
        digitalWrite(PIN_RELAY_BOILER, HIGH);
        if (!boiler_on) {
            Serial.println("Boiler on");
        }
        boiler_on = true;
    } else {
        digitalWrite(PIN_RELAY_BOILER, LOW);
        if (boiler_on) {
            Serial.println("Boiler off");
        }
        boiler_on = false;
    }
}

void
incr_setpoint(void) {
    setpoint += 0.5;
    preferences.putFloat("setpoint", setpoint);
}

void
decr_setpoint(void) {
    setpoint -= 0.5;
    preferences.putFloat("setpoint", setpoint);
}

void
reset_timer(void) {
    brew_end = brew_start;
}

/* Task functions */

void
read_sensors(void *parameter) {
    const int temp_samples = 5;
    float     temp_readings[ temp_samples ];
    float     temp_sorted[ temp_samples ];
    int       temp_idx = 0;

    for (int i = 0; i < temp_samples; i++) {
        temp_readings[ i ] = boiler_thermo.readFahrenheit();
    }

    for (;;) {
        // Read temperature
        if (++temp_idx >= temp_samples) {
            temp_idx = 0;
        }
        temp_readings[ temp_idx ] = boiler_thermo.readFahrenheit();
        for (int i = 0; i < temp_samples; i++) {
            float tmp = temp_readings[ i ];
            int   j = i;
            while (j > 0 && temp_sorted[ j - 1 ] > tmp) {
                temp_sorted[ j ] = temp_sorted[ j - 1 ];
                j--;
            }
            temp_sorted[ j ] = tmp;
        }
        temp_sorted[ 0 ] = 0;
        for (int i = 1; i < temp_samples - 1; i++) {
            temp_sorted[ 0 ] += temp_sorted[ i ];
        }
        boiler_temp = temp_sorted[ 0 ] / (temp_samples - 2);

        // Sleep
        vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_TIME));
    }
}

void
display(void *parameter) {
    const int    width = 320;
    const int    height = 480;
    const int    buf_size = 128;
    char         buf[ buf_size ];
    uint16_t     touch_cal[ 5 ];
    bool         pressed;
    uint16_t     touch_x = 0;
    uint16_t     touch_y = 0;
    SpriteButton key[ 3 ];

    tft.init();

    if (preferences.getBytesLength("touch_cal") == sizeof(touch_cal)) {
        preferences.getBytes("touch_cal", touch_cal, sizeof(touch_cal));
        tft.setTouch(touch_cal);
    } else {
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(20, 0);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextFont(2);
        tft.setTextSize(1);
        tft.println("Touch corners as indicated");
        tft.calibrateTouch(touch_cal, TFT_GREEN, TFT_BLACK, 15);
        preferences.putBytes("touch_cal", touch_cal, sizeof(touch_cal));
    }

    sprite.setColorDepth(4);
    sprite.createSprite(320, 480);
    tft.fillScreen(TFT_BLACK);

    // Draw splash screen
    tft.setTextDatum(TC_DATUM);
    while (current_state == MACHINE_INIT) {
        tft.setFreeFont(&FreeMono12pt7b);
        int lum = random(0, 191);
        for (int i = random(0, 3); i < height / tft.fontHeight(1); i++) {
            int j = random(0, (width / (tft.textWidth("X", 1) + 4)) + 1);
            if (lum > 15) {
                if (lum < 255) {
                    lum += random(0, 16);
                }
            }
            if (lum < 256) {
                tft.setTextColor(tft.color565(0, lum, 0));
                tft.drawChar(random(33, 127), j * (tft.textWidth("X", 1) + 4),
                        i * tft.fontHeight(1), 1);
            }
        }
        tft.setTextColor(TFT_GREEN);
        tft.setFreeFont(&FreeMono24pt7b);
        tft.drawString("ESPress0x20", width / 2, height / 3, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Make the transition less abrupt
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Real display loop
    for (;;) {
        int           pos_y = 32;
        int16_t       used;
        unsigned long brew_timer;

        pressed = tft.getTouch(&touch_x, &touch_y);

        // Black
        sprite.fillSprite(0);

        // Temp
        sprite.setTextColor(5, 0);
        sprite.setFreeFont(&FreeMonoBold24pt7b);
        snprintf(buf, 128, "%.0f F", boiler_temp - brew_offset);
        sprite.setTextDatum(TC_DATUM);
        sprite.drawString(buf, width / 4, pos_y, 1);

        sprite.setFreeFont(&FreeMono9pt7b);
        snprintf(buf, buf_size, "%.1f F", setpoint - brew_offset);
        sprite.drawString(buf, (width / 2) + (width / 4), pos_y, 1);

        /* Control keys */
        pos_y += sprite.fontHeight(1) + 4;
        if (key[ 0 ].needsInit()) {
            key[ 0 ].initButton(&sprite, (width / 2) + 8, pos_y,
                    (width / 4) - 16, sprite.fontHeight(1) + 6, 5, 0, "-",
                    &FreeMono9pt7b, &decr_setpoint);
        }
        if (key[ 1 ].needsInit()) {
            key[ 1 ].initButton(&sprite, (width / 2) + (width / 4) + 8, pos_y,
                    (width / 4) - 16, sprite.fontHeight(1) + 6, 5, 0, "+",
                    &FreeMono9pt7b, &incr_setpoint);
        }

        // Timer
        sprite.setTextColor(5, 0);
        sprite.setFreeFont(&FreeMonoBold24pt7b);
        pos_y += sprite.fontHeight(1) * 2;
        if (current_state == MACHINE_BREWING) {
            brew_timer = millis() - brew_start;
        } else {
            brew_timer = brew_end - brew_start;
        }
        sprite.setTextDatum(TC_DATUM);
        snprintf(buf, 128, "%02d:%02d.%02d", brew_timer / 60000,
                brew_timer % 60000 / 1000, brew_timer % 1000 / 10);
        sprite.drawString(buf, width / 2, pos_y, 1);
        pos_y += sprite.fontHeight(1);
        sprite.setFreeFont(&FreeMono9pt7b);
        if (key[ 2 ].needsInit()) {
            key[ 2 ].initButton(&sprite, width / 3, pos_y, width / 3,
                    sprite.fontHeight(1) + 6, 5, 0, "RESET", &FreeMono9pt7b,
                    &reset_timer);
        }

        // Handle button presses
        for (int i = 0; i < 3; i++) {
            key[ i ].press(touch_x, touch_y, pressed);
            key[ i ].drawButton();
        }

        // Status line
        sprite.setTextColor(5, 0);
        sprite.setFreeFont(&FreeSans9pt7b);
        snprintf(buf, 128, "boiler %s", boiler_on ? "on" : "off");
        sprite.setTextDatum(BL_DATUM);
        sprite.drawString(buf, 10, 480 - 10, 1);
        sprite.setTextDatum(BR_DATUM);
        sprite.drawString(WiFi.localIP().toString(), 320 - 10, 480 - 10, 1);

        sprite.pushSprite(0, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* Core task functions */
void
setup() {
    unsigned long ts_start;

    Serial.begin(115200);

    pinMode(PIN_RELAY_BOILER, OUTPUT);
    pinMode(PIN_SWITCH_BREW, INPUT_PULLUP);
    pinMode(PIN_SWITCH_STEAM, INPUT_PULLUP);

    preferences.begin("esp32resso");
    setpoint = preferences.getFloat("setpoint");
    if (isnan(setpoint)) {
        // Not initialized
        setpoint = 204.5;
        preferences.putFloat("setpoint", setpoint);
    }

    xTaskCreate(display, "Update Display", 4096, NULL, 1, NULL);

    Serial.print("Attempting to connect to wifi...");
    ts_start = millis();
    WiFi.begin("Wayhaught ", "Korrasami");
    while (WiFi.status() != WL_CONNECTED && millis() < ts_start + 15000) {
        delay(500);
        Serial.print('.');
    }
    Serial.println('.');
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected to wifi");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("Failed to connect to wifi");
    }

    webserver.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "ESPress0x20");
    });
    AsyncElegantOTA.begin(&webserver);
    webserver.begin();

    boiler_pid.SetOutputLimits(0, PID_WINDOW);
    boiler_pid.SetMode(boiler_pid.Control::automatic);
    boiler_window = millis();

    pinMode(PIN_DIMMER_ZC, INPUT_PULLUP);
    pinMode(PIN_DIMMER_PSM, OUTPUT);

    xTaskCreate(read_sensors, "Read Sensors", 2048, NULL, 2, NULL);
}

void
loop() {
    unsigned long now;
    machine_state new_state;

    now = millis();

    if (digitalRead(PIN_SWITCH_BREW) == LOW) {
        new_state = MACHINE_BREWING;
    } else if (digitalRead(PIN_SWITCH_STEAM) == LOW) {
        new_state = MACHINE_STEAMING;
    } else {
        new_state = MACHINE_HEATING;
    }

    if (new_state != current_state && now - last_state_change > 50) {
        last_state_change = now;

        if (current_state == MACHINE_BREWING) {
            brew_end = now;
        }

        if (new_state == MACHINE_BREWING) {
            boiler_pid.SetTunings(PID_KP_BREW, PID_KI_BREW, PID_KD_BREW);
            brew_start = now;
            digitalWrite(PIN_DIMMER_PSM, HIGH);
        } else {
            digitalWrite(PIN_DIMMER_PSM, LOW);
        }

        if (new_state == MACHINE_HEATING) {
            boiler_pid.SetTunings(PID_KP_HEAT, PID_KI_HEAT, PID_KD_HEAT);
        }

        current_state = new_state;
    }

    if (now - boiler_window > PID_WINDOW) {
        boiler_window += PID_WINDOW;
    }
    if (current_state == MACHINE_STEAMING) {
        boiler_control = (setpoint_steam - boiler_temp > 0.5) ? 1000 : 0;
    } else {
        boiler_pid.Compute();
    }
    boiler_state(boiler_control >= now - boiler_window);
}
