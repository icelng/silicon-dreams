#include "rtc_ds3231.h"

#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "common.h"

namespace sd {

RTCDS3231::RTCDS3231(bool adjust_system_time): adjust_system_time_(adjust_system_time) {}

RTCDS3231::~RTCDS3231() {
    if (is_initiated_) {
        if (adjust_timer_handle_ != nullptr) {
            xTimerStop(adjust_timer_handle_,0);
            while (xTimerIsTimerActive(adjust_timer_handle_)) vTaskDelay(1);
            xTimerDelete(adjust_timer_handle_, 0);
        }

        ESP_ERROR_CHECK(i2c_driver_delete(kI2CMasterPort));
    }
}

esp_err_t RTCDS3231::Init() {
    ESP_LOGI(LOG_TAG_RTC_DS3231, "[INIT RTC DS3231 START]\n");

    // TODO(yangliang), make the codes about i2c initialization independent, to support more various i2c peripherals
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = kI2CSDAIONum,
        .scl_io_num = kI2CSCLIONum,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE
    };
    conf.master.clk_speed = kI2CFreqHZ;

    RETURN_IF_ERROR(i2c_param_config(kI2CMasterPort, &conf));
    RETURN_IF_ERROR(i2c_driver_install(kI2CMasterPort, conf.mode, 0, 0, 0));


    if (adjust_system_time_) {
        ESP_ERROR_CHECK(AdjustSystemTime());
        adjust_timer_handle_ = xTimerCreate("refresh-system-time",
                                             60000 / portTICK_PERIOD_MS,
                                             pdTRUE, this,
                                             [](TimerHandle_t timer_handle) {
                                                auto* rtc = (RTCDS3231*)pvTimerGetTimerID(timer_handle);
                                                ESP_ERROR_CHECK(rtc->AdjustSystemTime());
                                             });
        if (adjust_timer_handle_ == nullptr) {
            xTimerDelete(adjust_timer_handle_, 0);
            return ESP_ERR_NO_MEM;
        }
        assert(xTimerStart(adjust_timer_handle_, 0));
    }

    ESP_LOGI(LOG_TAG_RTC_DS3231, "[INIT RTC DS3231 END]\n");

    is_initiated_ = true;

    return ESP_OK;
}

esp_err_t RTCDS3231::AdjustSystemTime() {
    RTCDS3231::Time time;
    RETURN_IF_ERROR(GetCurrentTime(&time));
    set_system_time(time.timestamp_s);
    return ESP_OK;
}

esp_err_t RTCDS3231::GetCurrentTime(Time* time) {
    uint8_t reg_secs = 0;
    uint8_t reg_minutes = 0;
    uint8_t reg_hours = 0;
    uint8_t reg_date = 0;
    uint8_t reg_century_month = 0;
    uint8_t reg_year = 0;

    RETURN_IF_ERROR(ReadRegisterByte(kDS3231RegAddrSeconds, &reg_secs));
    RETURN_IF_ERROR(ReadRegisterByte(kDS3231RegAddrMinutes, &reg_minutes));
    RETURN_IF_ERROR(ReadRegisterByte(kDS3231RegAddrHours, &reg_hours));
    RETURN_IF_ERROR(ReadRegisterByte(kDS3231RegAddrDate, &reg_date));
    RETURN_IF_ERROR(ReadRegisterByte(kDS3231RegAddrCenturyMonth, &reg_century_month));
    RETURN_IF_ERROR(ReadRegisterByte(kDS3231RegAddrYear, &reg_year));

    ESP_LOGI(LOG_TAG_RTC_DS3231, "[RTC DS3231 RAW] reg_secs: %x, reg_minutes: %x, reg_hours: %x, reg_date: %x, reg_month: %x, reg_year: %x\n",
             reg_secs, reg_minutes, reg_hours, reg_date, reg_century_month, reg_year);

    time->seconds = ((reg_secs & kDS3231RegMaskTenSeconds) >> kDS3231RegBitStartTenSeconds) * 10 +
                    ((reg_secs & kDS3231RegMaskSeconds) >> kDS3231RegBitStartSeconds);

    time->minutes = ((reg_minutes & kDS3231RegMaskTenMinutes) >> kDS3231RegBitStartTenMinutes) * 10 +
                      ((reg_minutes & kDS3231RegMaskMinutes) >> kDS3231RegBitStartMinutes);

    if (reg_hours & kDS3231RegMaskIs12Hour) {
        time->hours = ((reg_hours & kDS3231RegMaskPMTenHours) >> kDS3231RegBitStartTenHours) * 10 +
                ((reg_hours & kDS3231RegMaskHours) >> kDS3231RegBitStartHours);
        if (reg_hours & kDS3231RegMaskIsPM) {
            time->hours += 12;
        }
    } else {
        time->hours = ((reg_hours & kDS3231RegMaskTenHours) >> kDS3231RegBitStartTenHours) * 10 +
                ((reg_hours & kDS3231RegMaskHours) >> kDS3231RegBitStartHours);
    }

    time->date = ((reg_date & kDS3231RegMaskTenDate) >> kDS3231RegBitStartTenDate) * 10 +
                    ((reg_date & kDS3231RegMaskDate) >> kDS3231RegBitStartDate);

    time->month = ((reg_century_month & kDS3231RegMaskTenMonth) >> kDS3231RegBitStartTenMonth) * 10 +
                     ((reg_century_month & kDS3231RegMaskMonth) >> kDS3231RegBitStartMonth);

    time->year = ((reg_year & kDS3231RegMaskTenYear) >> kDS3231RegBitStartTenYear) * 10 +
                        ((reg_year & kDS3231RegMaskYear) >> kDS3231RegBitStartYear) + kYearFrom;

    time->timestamp_s = to_timestamp(time->year, time->month, time->date,
                                     time->hours, time->minutes, time->seconds);
    time->timestamp_s -= (3600 * kTimeZone);

    ESP_LOGI(LOG_TAG_RTC_DS3231, "[GET RTC DS3231 TIME] %d %d %d %d:%d:%d\n",
                    time->year, time->month, time->date, time->hours, time->minutes, time->seconds);

    return ESP_OK;
}


esp_err_t RTCDS3231::SetTime(time_t timestamp_s) {
    timestamp_s += (3600 * kTimeZone);

    struct tm ts;
    ts = *localtime(&timestamp_s);

    Time t;
    t.year = ts.tm_year + 1900;
    t.month = ts.tm_mon + 1;
    t.date = ts.tm_mday;
    t.hours = ts.tm_hour;
    t.minutes = ts.tm_min;
    t.seconds = ts.tm_sec;

    ESP_LOGI(LOG_TAG_RTC_DS3231, "[SET RTC DS3231 TIME] %d %d %d %d:%d:%d\n",
                    t.year, t.month, t.date, t.hours, t.minutes, t.seconds);

    return SetTime(&t);
}


esp_err_t RTCDS3231::SetTime(Time* time) {
    if (time->year < kYearFrom) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg_secs    = time->seconds / 10 << kDS3231RegBitStartTenSeconds | time->seconds % 10;
    uint8_t reg_minutes = time->minutes / 10 << kDS3231RegBitStartTenMinutes | time->minutes % 10;
    uint8_t reg_hours   = time->hours / 10 << kDS3231RegBitStartTenHours | time->hours % 10;
    uint8_t reg_date    = time->date / 10 << kDS3231RegBitStartTenYear | time->date % 10;
    uint8_t reg_month   = time->month / 10 << kDS3231RegBitStartTenMonth | time->month % 10;
    uint8_t reg_year    = (time->year - kYearFrom) / 10 << kDS3231RegBitStartTenYear | (time->year - kYearFrom) % 10;

    RETURN_IF_ERROR(WriteRegisterByte(kDS3231RegAddrSeconds, reg_secs));
    RETURN_IF_ERROR(WriteRegisterByte(kDS3231RegAddrMinutes, reg_minutes));
    RETURN_IF_ERROR(WriteRegisterByte(kDS3231RegAddrHours, reg_hours));
    RETURN_IF_ERROR(WriteRegisterByte(kDS3231RegAddrDate, reg_date));
    RETURN_IF_ERROR(WriteRegisterByte(kDS3231RegAddrCenturyMonth, reg_month));
    RETURN_IF_ERROR(WriteRegisterByte(kDS3231RegAddrYear, reg_year));

    return ESP_OK;
}

esp_err_t RTCDS3231::ReadRegister(uint8_t reg_addr, uint8_t* data, size_t len) {
    return i2c_master_write_read_device(kI2CMasterPort, kDS3231DeviceAddr,
                                        &reg_addr, 1, data, len, kI2CMasterTimeoutMs / portTICK_PERIOD_MS);;
}

esp_err_t RTCDS3231::ReadRegisterByte(uint8_t reg_addr, uint8_t* data) {
    return i2c_master_write_read_device(kI2CMasterPort, kDS3231DeviceAddr,
                                        &reg_addr, 1, data, 1, kI2CMasterTimeoutMs / portTICK_PERIOD_MS);;
}

esp_err_t RTCDS3231::WriteRegisterByte(uint8_t reg_addr, uint8_t data) {
    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_write_to_device(kI2CMasterPort, kDS3231DeviceAddr,
                                      write_buf, sizeof(write_buf), kI2CMasterTimeoutMs / portTICK_PERIOD_MS);
}

}  // namespace sd
