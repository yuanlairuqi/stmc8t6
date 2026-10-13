#include "battery.h"
#include "sdkconfig.h"
#include "esp_log.h"
#ifdef CONFIG_ROBOT_BATTERY_ENABLED
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
static adc_oneshot_unit_handle_t unit;
static adc_cali_handle_t cal;
static bool ready;
void battery_init(void) {
    adc_oneshot_unit_init_cfg_t cfg = {.unit_id=ADC_UNIT_1};
    adc_oneshot_chan_cfg_t channel = {.atten=ADC_ATTEN_DB_12, .bitwidth=ADC_BITWIDTH_DEFAULT};
    adc_cali_curve_fitting_config_t cc = {.unit_id=ADC_UNIT_1,
        .chan=CONFIG_ROBOT_BATTERY_CHANNEL, .atten=ADC_ATTEN_DB_12, .bitwidth=ADC_BITWIDTH_DEFAULT};
    if (adc_oneshot_new_unit(&cfg, &unit) != ESP_OK ||
        adc_oneshot_config_channel(unit, CONFIG_ROBOT_BATTERY_CHANNEL, &channel) != ESP_OK ||
        adc_cali_create_scheme_curve_fitting(&cc, &cal) != ESP_OK) {
        ESP_LOGE("battery", "ADC/calibration unavailable; voltage stays unknown"); return;
    }
    ready = true;
}
bool battery_read(uint16_t *mv) {
    if (!ready) return false;
    int total = 0;
    for (int i=0; i<16; ++i) {
        int raw, pin_mv;
        if (adc_oneshot_read(unit, CONFIG_ROBOT_BATTERY_CHANNEL, &raw) != ESP_OK ||
            raw >= 4090 || adc_cali_raw_to_voltage(cal, raw, &pin_mv) != ESP_OK) return false;
        total += pin_mv;
    }
    uint64_t value = (uint64_t)(total/16) *
        (CONFIG_ROBOT_BATTERY_R_TOP+CONFIG_ROBOT_BATTERY_R_BOTTOM) / CONFIG_ROBOT_BATTERY_R_BOTTOM;
    if (value >= 65535) return false;
    *mv = (uint16_t)value;
    return true;
}
#else
void battery_init(void) { ESP_LOGI("battery", "Battery ADC disabled: voltage unknown"); }
bool battery_read(uint16_t *mv) { (void)mv; return false; }
#endif
