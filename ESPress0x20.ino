/* This file is part of ESPress0x20 and distributed under the terms of the
 * MIT license. See COPYING.
 */

#include <AsyncElegantOTA.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPLogger.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <QuickPID.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <max6675.h>

#include "PID_Tuner.h"
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
#define PID_KP_HEAT 2.0
#define PID_KI_HEAT 0.1
#define PID_KD_HEAT 0
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
TFT_eSprite g_sprite = TFT_eSprite(&tft);

ESPLogger     logger("/data.log", LittleFS);
machine_state current_state = MACHINE_INIT;
unsigned long last_state_change = 0;
float         boiler_temp_raw;
float         boiler_temp;
bool          boiler_stable;
float         boiler_control;
unsigned long boiler_window;
int           duty_cycle = 0;
float         setpoint_brew;
float         setpoint_steam;
float         brew_offset = 14.5;
bool          debug = false;
unsigned long brew_start;
unsigned long brew_end;
MAX6675       boiler_thermo(PIN_THERMO_CLK, PIN_THERMO_CS, PIN_THERMO_DO);
QuickPID      boiler_pid(&boiler_temp, &boiler_control, &setpoint_brew);
bool          autotuned = true;
PID_Tuner     pid_tuner(&boiler_temp, &boiler_control);

/* Utility functions */

void
boiler_pwm(void) {
    unsigned long now;
    const int     cycle_samples = 5;
    static float  cycle_total = 0.0;
    static float  cycle_data[ cycle_samples ] = {};
    static int    cycle_idx = 0;

    now = millis();

    if (boiler_control > 1000) {
        boiler_control = 1000;
    } else if (boiler_control < 0) {
        boiler_control = 0;
    }

    if (boiler_temp_raw > 310) {
        // E stop
        boiler_control = 0;
    }

    if (boiler_control >= now - boiler_window) {
        digitalWrite(PIN_RELAY_BOILER, HIGH);
    } else {
        digitalWrite(PIN_RELAY_BOILER, LOW);
    }

    // Calculate duty cycle for display
    if (now - boiler_window > PID_WINDOW) {
        boiler_window += PID_WINDOW;
        cycle_total -= cycle_data[ cycle_idx ];
        cycle_data[ cycle_idx ] = boiler_control;
        cycle_total += boiler_control;
        duty_cycle = cycle_total / 10 / cycle_samples;
        if (++cycle_idx >= cycle_samples) {
            cycle_idx = 0;
        }
    }
}

void
incr_setpoint(void) {
    if (current_state == MACHINE_STEAMING) {
        setpoint_steam += 1.0;
        preferences.putFloat("setpoint_steam", setpoint_steam);
    } else {
        setpoint_brew += 0.5;
        preferences.putFloat("setpoint_brew", setpoint_brew);
    }
}

void
decr_setpoint(void) {
    if (current_state == MACHINE_STEAMING) {
        setpoint_steam -= 1.0;
        preferences.putFloat("setpoint_steam", setpoint_steam);
    } else {
        setpoint_brew -= 0.5;
        preferences.putFloat("setpoint_brew", setpoint_brew);
    }
}

void
reset_timer(void) {
    brew_end = brew_start;
}

float
pref_or_def(const char *pref, float def) {
    float ret = preferences.getFloat(pref);
    if (isnan(ret)) {
        // Not initialized
        preferences.putFloat(pref, def);
        return def;
    }
    return ret;
}

/* Task functions */

void
read_sensors(void *parameter) {
    const float  temp_alpha = 0.1;
    static float temp_reading;
    static float temp_slope;
    char         log_line[ 19 ]; // "NNN.NN,NNN.NN,NNNN\0"

    // Initial value
    temp_reading = boiler_thermo.readFahrenheit();
    temp_slope = temp_reading;

    // Read loop
    for (;;) {
        boiler_temp_raw = boiler_thermo.readFahrenheit();
        // Double exponential moving average
        temp_reading = (temp_alpha * boiler_temp_raw) +
                       ((1 - temp_alpha) * temp_reading);
        temp_slope =
                (temp_alpha * temp_reading) + ((1 - temp_alpha) * temp_slope);
        boiler_temp = 2 * temp_reading - temp_slope;

        if ((fabs(temp_reading - temp_slope) < 0.25)) {
            boiler_stable = true;
        } else {
            boiler_stable = false;
        }

        // Log
        if (current_state == MACHINE_BREWING) {
            snprintf(log_line, 18, "%.2f,%.2f,%.0f", boiler_temp,
                    boiler_temp_raw, boiler_control);
            logger.append(log_line, true);
        }

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
    int          g_y_old = 0;

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
        if (touch_cal[ 0 ] > 1) {
            preferences.putBytes("touch_cal", touch_cal, sizeof(touch_cal));
        }
    }

    sprite.setColorDepth(4);
    sprite.createSprite(320, 480);
    g_sprite.setColorDepth(4);
    g_sprite.createSprite(300, 150);
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
        int           g_y;
        int16_t       used;
        unsigned long brew_timer;

        pressed = tft.getTouch(&touch_x, &touch_y);

        // Black
        sprite.fillSprite(0);

        // Temp
        if (boiler_stable) {
            // Green
            sprite.setTextColor(5, 0);
        } else {
            // Red
            sprite.setTextColor(2, 0);
        }
        sprite.setFreeFont(&FreeMonoBold24pt7b);
        snprintf(buf, buf_size, "%.0f F",
                current_state == MACHINE_STEAMING
                        ? boiler_temp
                        : (boiler_temp - brew_offset));
        sprite.setTextDatum(TC_DATUM);
        sprite.drawString(buf, width / 4, pos_y, 1);

        sprite.setFreeFont(&FreeMono9pt7b);
        sprite.setTextColor(5, 0);
        snprintf(buf, buf_size, "%.1f F",
                current_state == MACHINE_STEAMING
                        ? setpoint_steam
                        : (setpoint_brew - brew_offset));
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
        snprintf(buf, buf_size, "%02d:%02d.%02d", brew_timer / 60000,
                brew_timer % 60000 / 1000, brew_timer % 1000 / 10);
        sprite.drawString(buf, width / 2, pos_y, 1);
        pos_y += sprite.fontHeight(1);
        sprite.setFreeFont(&FreeMono9pt7b);
        if (key[ 2 ].needsInit()) {
            key[ 2 ].initButton(&sprite, width / 3, pos_y, width / 3,
                    sprite.fontHeight(1) + 6, 5, 0, "RESET", &FreeMono9pt7b,
                    &reset_timer);
        }

        // Debugging
        if (debug) {
            pos_y += sprite.fontHeight(1) * 2;
            snprintf(buf, buf_size, "p: %.2f", boiler_pid.GetKp());
            sprite.drawString(buf, width / 2, pos_y, 1);
            pos_y += sprite.fontHeight(1);
            snprintf(buf, buf_size, "i: %.2f", boiler_pid.GetKi());
            sprite.drawString(buf, width / 2, pos_y, 1);
            pos_y += sprite.fontHeight(1);
            snprintf(buf, buf_size, "d: %.2f", boiler_pid.GetKd());
            sprite.drawString(buf, width / 2, pos_y, 1);
            pos_y += sprite.fontHeight(1);
            snprintf(buf, buf_size, "raw temp: %.2f", boiler_temp_raw);
            sprite.drawString(buf, width / 2, pos_y, 1);
        } else {
            pos_y += sprite.fontHeight(1) * 2;
            g_sprite.scroll(-10, 0);
            g_y = g_sprite.height() - ((g_sprite.height() / 300) * boiler_temp);
            g_sprite.drawLine(290, g_y_old, 300, g_y, 2);
            g_sprite.drawLine(290, g_y_old + 1, 300, g_y + 1, 2);
            g_y_old = g_y;
            g_sprite.pushToSprite(&sprite, 10, pos_y);
            sprite.drawRect(10, pos_y, 310, pos_y + g_sprite.height(), 1);
        }

        // Handle button presses
        for (int i = 0; i < 3; i++) {
            key[ i ].press(touch_x, touch_y, pressed);
            key[ i ].drawButton();
        }

        // Status line
        sprite.setTextColor(5, 0);
        sprite.setFreeFont(&FreeSans9pt7b);
        snprintf(buf, buf_size, "boiler %d%% (%s)", duty_cycle,
                current_state == MACHINE_STEAMING
                        ? "steam"
                        : (current_state == MACHINE_BREWING ? "brew" : "heat"));
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
    setpoint_brew = pref_or_def("setpoint_brew", 214.5);
    setpoint_steam = pref_or_def("setpoint_steam", 300.0);

    LittleFS.begin(true); // automatically reformat FS on mount error

    logger.setSizeLimit(524288, true);
    logger.begin();

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

    webserver.on("/autotune", HTTP_GET, [](AsyncWebServerRequest *request) {
        autotuned = false;
        debug = true;
        pid_tuner.reset();
        request->send(200, "text/plain", "OK");
    });

    webserver.on(
            "/clear_preferences", HTTP_GET, [](AsyncWebServerRequest *request) {
                preferences.clear();
                request->send(200, "text/plain", "OK");
            });

    webserver.on("/log", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/data.log", "text/plain");
    });

    webserver.on("/metrics", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response =
                request->beginResponseStream("text/plain");
        response->print("# TYPE boiler_temp_raw gauge\n");
        response->printf(
                "boiler_temp_raw{sensor=\"0\"} %f\n\n", boiler_temp_raw);

        response->print("# TYPE boiler_temp gauge\n");
        response->printf("boiler_temp %f\n\n", boiler_temp);

        response->print("# TYPE boiler_control gauge\n");
        response->printf("boiler_control %f\n\n", boiler_control);

        request->send(response);
    });

    AsyncElegantOTA.begin(&webserver);
    webserver.begin();

    boiler_pid.SetOutputLimits(0, PID_WINDOW);
    boiler_pid.SetMode(boiler_pid.Control::manual);
    boiler_pid.SetTunings(PID_KP_HEAT, PID_KI_HEAT, PID_KD_HEAT);
    boiler_window = millis();

    pinMode(PIN_DIMMER_ZC, INPUT_PULLUP);
    pinMode(PIN_DIMMER_PSM, OUTPUT);

    xTaskCreate(read_sensors, "Read Sensors", 4096, NULL, 2, NULL);
}

void
loop() {
    unsigned long now;
    machine_state new_state;
    float         setpoint;

    now = millis();

    if (digitalRead(PIN_SWITCH_STEAM) == LOW) {
        new_state = MACHINE_STEAMING;
    } else if (digitalRead(PIN_SWITCH_BREW) == LOW) {
        new_state = MACHINE_BREWING;
    } else {
        new_state = MACHINE_HEATING;
    }

    if (new_state != current_state && now - last_state_change > 50) {
        last_state_change = now;

        if (current_state == MACHINE_BREWING) {
            brew_end = now;
        }

        if (new_state == MACHINE_BREWING) {
            brew_start = now;
            digitalWrite(PIN_DIMMER_PSM, HIGH);
            logger.reset();
        } else {
            digitalWrite(PIN_DIMMER_PSM, LOW);
        }

        if (new_state == MACHINE_HEATING) {
            boiler_pid.SetMode(boiler_pid.Control::automatic);
        } else {
            boiler_pid.SetMode(boiler_pid.Control::manual);
        }

        current_state = new_state;
    }

    // Calculate duty cycle for this window
    if (!autotuned) {
        if (pid_tuner.run()) {
            autotuned = true;
            boiler_pid.SetTunings(
                    pid_tuner.get_p(), pid_tuner.get_i(), pid_tuner.get_d());
        }
    } else if (current_state == MACHINE_HEATING) {
        boiler_pid.Compute();
    } else {
        setpoint = current_state == MACHINE_STEAMING ? setpoint_steam
                                                     : setpoint_brew;
        // 100% until we get within 2 degrees, then a linear ramp down to 0.
        if (boiler_temp < setpoint) {
            boiler_control = (setpoint - boiler_temp) * 500;
            if (boiler_control > 1000) {
                boiler_control = 1000;
            }
        } else {
            boiler_control = 0;
        }
    }

    boiler_pwm();
}
