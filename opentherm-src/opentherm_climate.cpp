#include "opentherm_climate.h"
#include "esphome/core/log.h"

namespace esphome {
namespace opentherm {

static const char *TAG = "OT";

OpenThermGWClimate::OpenThermGWClimate(int pin_in, int pin_out)
     : ot(pin_in, pin_out, false)
{
}

void OpenThermGWClimate::setup() {

  this->last_request_timesmap = (millis() + 15000); // wait 15 sec before start ot
  this->timeout_count = 0;
  this->next_request = 0;
  this->is_fault = false;
  this->interval_wait = false;
  this->temp_reach = false;

  auto restore = this->restore_state_();
  if(restore.has_value()) {
      restore->to_call(this).perform();
  } else {
    this->target_temperature = 10;
  }

  if(this->house_temp != nullptr) {
    this->current_temperature = this->house_temp->state;
    this->house_temp->add_on_state_callback([this](float state) {
      ESP_LOGD(TAG, "Inhouse temp changed %f", state);
      this->current_temperature = state;
      this->publish_state();
    });
  }

  this->interval_number->set_name("Интервал");
  this->interval_number->traits.set_min_value(0);
  this->interval_number->traits.set_max_value(60);
  this->interval_number->traits.set_unit_of_measurement("min");
  this->interval_number->traits.set_mode(number::NumberMode::NUMBER_MODE_SLIDER);
  this->interval_number->traits.set_step(1);
  if(!this->interval_number->has_state())
    this->interval_number->make_call().set_value(10).perform();

  this->ch_temp_number->set_name("Нагрев до");
  this->ch_temp_number->traits.set_min_value(40);
  this->ch_temp_number->traits.set_max_value(85);
  this->ch_temp_number->traits.set_unit_of_measurement("°C");
  this->ch_temp_number->traits.set_mode(number::NumberMode::NUMBER_MODE_SLIDER);
  this->ch_temp_number->traits.set_step(1);
  if(!this->ch_temp_number->has_state())
    this->ch_temp_number->make_call().set_value(40).perform();

  this->mode_select->set_name("Режим работы");
  this->mode_select->set_icon("mdi:home-thermometer");
  this->mode_select->traits.set_options(std::vector<std::string> { "Автоматический PID", "Ручной" });
  if(!this->mode_select->has_state())
    this->mode_select->make_call().set_option("Автоматический PID").perform();
  this->mode_select->add_on_state_callback([this](const std::string &value, size_t index) {
    ESP_LOGD(TAG, "Working mode changed %s", value);
  });

  this->pid_calc->make_call().set_target_temperature_low(10).set_target_temperature_high(30).perform();

  this->hot_water->set_name("Boiler hot water");
  this->hot_water->set_icon("mdi:coolant-temperature");

  this->flame_on->set_name("Boiler flame");
  this->flame_on->set_icon("mdi:fire");

  if(this->heat_interval != nullptr) {
    this->heat_interval->set_name("Boiler Heat Interval");
    this->heat_interval->publish_state("0");
    this->heat_interval->set_icon("mdi:camera-timer");
  }
  if(this->fault != nullptr) {
    this->fault->set_name("Boiler Status");
    this->fault->set_icon("mdi:sync");
    this->fault->set_entity_category(EntityCategory::ENTITY_CATEGORY_DIAGNOSTIC);
  }
  if(this->connection != nullptr) {
    this->connection->set_name("Boiler OT connection");
    this->connection->set_device_class("connectivity");
    this->connection->set_entity_category(EntityCategory::ENTITY_CATEGORY_DIAGNOSTIC);
  }
  if(this->boiler_water_temp != nullptr) {
    this->boiler_water_temp->set_name("Boiler CH Temp");
    this->boiler_water_temp->set_device_class("temperature");
    this->boiler_water_temp->set_unit_of_measurement("°C");
    this->boiler_water_temp->set_accuracy_decimals(1);
    this->boiler_water_temp->add_filter(new sensor::SlidingWindowMovingAverageFilter(3, 3, 1));
  }
  if(this->dhw_temperature != nullptr) {
    this->dhw_temperature->set_name("Boiler DHW Temp");
    this->dhw_temperature->set_device_class("temperature");
    this->dhw_temperature->set_unit_of_measurement("°C");
    this->dhw_temperature->set_accuracy_decimals(1);
    this->dhw_temperature->add_filter(new sensor::SlidingWindowMovingAverageFilter(3, 3, 1));
  }
  if(this->relative_modulation_level != nullptr) {
    this->relative_modulation_level->set_name("Boiler Rel Mod Level");
    this->relative_modulation_level->set_unit_of_measurement("%");
    this->relative_modulation_level->set_icon("mdi:percent");
  }
  if(this->dhw_flow_rate != nullptr) {
    this->dhw_flow_rate->set_name("Boiler Water Flow Rate");
    this->dhw_flow_rate->set_unit_of_measurement("л/мин");
    this->dhw_flow_rate->set_icon("mdi:approximately-equal");
    this->dhw_flow_rate->set_accuracy_decimals(1);
  }
  if(this->return_water_temperature != nullptr) {
    this->return_water_temperature->set_name("Boiler Return Water Temp");
    this->return_water_temperature->set_device_class("temperature");
    this->return_water_temperature->set_unit_of_measurement("°C");
    this->return_water_temperature->set_accuracy_decimals(1);
    this->return_water_temperature->add_filter(new sensor::SlidingWindowMovingAverageFilter(3, 3, 1));
  }
  if(this->outside_air_temperature != nullptr) {
    this->outside_air_temperature->set_name("Boiler Outside Air Temp");
    this->outside_air_temperature->set_device_class("temperature");
    this->outside_air_temperature->set_unit_of_measurement("°C");
    this->outside_air_temperature->set_accuracy_decimals(1);
    this->outside_air_temperature->add_filter(new sensor::SlidingWindowMovingAverageFilter(3, 3, 1));
  }
  if(this->ch_water_pressure != nullptr) {
    this->ch_water_pressure->set_name("Boiler Water Pressure");
    this->ch_water_pressure->set_unit_of_measurement("bar");
    this->ch_water_pressure->set_icon("mdi:gauge");
    this->ch_water_pressure->set_accuracy_decimals(1);
  }

  ot.begin(std::bind(&OpenThermGWClimate::handleInterrupt, this), std::bind(&OpenThermGWClimate::processResponse, this, std::placeholders::_1, std::placeholders::_2)); 
}

void IRAM_ATTR OpenThermGWClimate::handleInterrupt() {
  ot.handleInterrupt();
}

void OpenThermGWClimate::loop()
{
  if(!ot.isReady()) // needed for asynchronous requests
  {
    ot.process();  // if the OT bus receives or sends data, transfer control to the OT library
    return;
  }
 
  this->now_timesmap = millis();

  if((this->last_request_timesmap < this->now_timesmap) && (this->now_timesmap - this->last_request_timesmap) > 700) { // Before sending something you must wait at least 100ms according to the OT protocol specification
     this->last_request_timesmap = this->now_timesmap;
    switch(this->next_request) {
    case 0: {
      unsigned long response = ot.sendRequest(ot.buildRequest(OpenThermRequestType::READ_DATA, OpenThermMessageID::SConfigSMemberIDcode, 0xFFFF));
      if(ot.getLastResponseStatus() == OpenThermResponseStatus::SUCCESS && ot.getDataID(response) == OpenThermMessageID::SConfigSMemberIDcode) 
      {
        ot.sendRequest(ot.buildRequest(OpenThermRequestType::WRITE_DATA, OpenThermMessageID::MConfigMMemberIDcode, response >> 0 & 0xFF));
      }
      this->next_request = 1;
    break;
    }
    case 1:
      ot.sendRequestAync(ot.buildRequest(OpenThermRequestType::READ_DATA, OpenThermMessageID::SlaveVersion, 0));
      this->next_request = 2;
    break;
    case 2:
      ot.sendRequestAync(ot.buildRequest(OpenThermRequestType::WRITE_DATA, OpenThermMessageID::MasterVersion, 0x013F));
      this->next_request = 3;
    break;
    case 3:
      request_Status(this->mode == climate::CLIMATE_MODE_HEAT ? true : false, this->hot_water->state);
      this->next_request = 4;
    break;
    case 4:
      if(this->is_fault) {
        this->next_request = 12;
        break;
      }
      request_TSet(calcutateTemp());
      this->next_request = 5;
    break;
    case 5:
      if (this->boiler_water_temp != nullptr) request_Tboiler();
      this->next_request = 6;
    break;
    case 6:
      if (this->dhw_temperature != nullptr) request_Tdhw();
      this->next_request = 7;
    break;
    case 7:
      if (this->dhw_flow_rate != nullptr) request_DHWFlowRate();
      this->next_request = 8;
    break;
    case 8:
      if (this->ch_water_pressure != nullptr) request_CHPressure();
      this->next_request = 9;
    break;
    case 9:
      if (this->relative_modulation_level != nullptr) request_RelModLevel();
      this->next_request = 10;
    break;
    case 10:
      if (this->outside_air_temperature != nullptr) request_Toutside();
      this->next_request = 11;
    break;
    case 11:
      if (this->return_water_temperature != nullptr) request_Tret();
      this->next_request = 3;
    break;
    case 12:
      request_ASFflags();
      this->next_request = 3;
    break;
    }
  }
}

float OpenThermGWClimate::calcutateTemp() {
  if(!this->mode_select->has_state() || this->mode == climate::CLIMATE_MODE_OFF) return 0.0f;

  if(this->mode_select->state == "Автоматический PID") {

    return this->pid_output->get_state() * 85;

  } else if(this->mode_select->state == "Ручной") {
    float ch_temp = 0;

    if(!this->flame_on->state && this->temp_reach && this->interval_number->state != 0) { // calculate the waiting interval
      this->interval_wait = true;
      this->interval_timesmap = this->now_timesmap + this->interval_number->state * 60000;
      this->temp_reach = false;
    }

    if(this->interval_wait) { // if now is a waiting interval, count the time until the end of the interval
      if(this->heat_interval != nullptr) {
        char text[17];
        unsigned int min = ((this->interval_timesmap - this->now_timesmap) / 60000) % 60;
        unsigned int sec = ((this->interval_timesmap - this->now_timesmap) / 1000) % 60;
        sprintf(text, "%d m %d s", min, sec);
        this->heat_interval->publish_state(text);
      }
      if(this->now_timesmap >= this->interval_timesmap) { // if wait interval is over
        this->interval_wait = false;
        if(this->heat_interval != nullptr) this->heat_interval->publish_state("0");
      }

    } else {   // if it's time to heat up the water
      ch_temp = this->ch_temp_number->state;
      if(this->heat_interval != nullptr && this->heat_interval->state != "0") this->heat_interval->publish_state("0");
      if(!std::isnan(this->current_temperature) && !std::isnan(this->target_temperature)) { // if we have a room temperature sensor and have reached target, we do not heat
        if((this->target_temperature - this->current_temperature) < 0) {
          ch_temp = 0;
        }
      } 
      if(ch_temp >= 40) {
        if(this->boiler_water_temp->state >= ch_temp && this->flame_on->state) { // if boiler has heated the water to the set temperature, start the waiting interval 
          this->temp_reach = true;
        }
      }
    }
    return ch_temp;
  } else {
    return 0.0f;
  }
}

void OpenThermGWClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    this->mode = *call.get_mode();
    this->pid_calc->make_call().set_mode(this->mode).perform();
  }
  if (call.get_target_temperature().has_value()) {
    this->target_temperature = *call.get_target_temperature();
    this->pid_calc->make_call().set_target_temperature(this->target_temperature).perform();
  }
  this->interval_wait = false;
  if(this->heat_interval != nullptr) this->heat_interval->publish_state("0");
  this->publish_state();
}

climate::ClimateTraits OpenThermGWClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_supports_current_temperature(this->house_temp != nullptr);
  traits.set_supported_modes({ climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT });
  traits.set_supports_action(true);
  traits.set_visual_min_temperature(10);
  traits.set_visual_max_temperature(30);
  traits.set_visual_temperature_step(1);
  return traits;
}

void OpenThermGWClimate::dump_config() {
  LOG_CLIMATE("", "OpenTherm Gateway Climate", this);
}

void OpenThermGWClimate::processResponse(uint32_t response, OpenThermResponseStatus status) {
  switch (status)
  {
    case OpenThermResponseStatus::INVALID:
      ESP_LOGW(TAG, "Invalid boiler response to some request");
      break;
    case OpenThermResponseStatus::TIMEOUT:
      if (this->timeout_count < 10)
      {
        this->timeout_count++;
        ESP_LOGW(TAG, "Boiler response timeout via OT bus [%i]", this->timeout_count);
      } else {
        ESP_LOGE(TAG, "There is no communication with the boiler via the OT bus, сheck the connection");
        if(this->connection != nullptr) this->connection->publish_state(false);
      }
      break;
    case OpenThermResponseStatus::SUCCESS:
        ESP_LOGD(TAG, "Response B %03d %04x", ot.getDataID(response), (response & 0xffff));
        if(this->connection != nullptr) this->connection->publish_state(true);
        if(this->timeout_count != 0) this->timeout_count = 0;
        handleReply(response);
      break;
    default:
      break;
  }
}

void OpenThermGWClimate::handleReply(uint32_t &response) {
    OpenThermMessageID id = ot.getDataID(response);
    switch (id) {
      case CHPressure:
        process_CHPressure(response);
        break;
      case DHWFlowRate:
        process_DHWFlowRate(response);
        break;
      case RelModLevel:
        process_RelModLevel(response);
        break;
      case SlaveVersion:
        process_SlaveVersion(response);
        break;
      case Status:
        process_Status(response);
        break;
      case SConfigSMemberIDcode:
        process_SConfigSMemberIDcode(response);
        break;
      case MConfigMMemberIDcode:
        process_MConfigMMemberIDcode(response);
        break;
      case TSet:
        process_TSet(response);
        break;
      case Tboiler:
        process_Tboiler(response);
        break;
      case Tdhw:
        process_Tdhw(response);
        break;
      case Toutside:
        process_Toutside(response);
        break;
      case Tret:
        process_Tret(response);
        break;
      case ASFflags:
        process_ASFflags(response);
        break;
      default:
        ESP_LOGW(TAG, "Response %d not handled", id);
        break;
    }
}

void OpenThermGWClimate::process_Status(uint32_t &response) {
  uint8_t lb = response & 0xff;

  bool slave_fault_indication = lb & (1 << 0);
  bool slave_ch_active        = lb & (1 << 1);
  bool slave_dhw_active       = lb & (1 << 2);
  bool slave_flame_on         = lb & (1 << 3);
  bool slave_cooling_active   = lb & (1 << 4);
  bool slave_ch2_active       = lb & (1 << 5);
  bool slave_diagnostic_event = lb & (1 << 6);

  climate::ClimateAction new_action;
  if(slave_ch_active)
    new_action = climate::CLIMATE_ACTION_HEATING;
  if(!slave_ch_active)
    new_action = climate::CLIMATE_ACTION_IDLE;
  if(this->mode == climate::CLIMATE_MODE_OFF)
    new_action = climate::CLIMATE_ACTION_OFF;

  if (new_action != this->action) {
    this->action = new_action;
    this->publish_state();
  }

    if(this->flame_on->state != slave_flame_on || !this->flame_on->has_state()) this->flame_on->publish_state(slave_flame_on);

    if(slave_fault_indication) {
      ESP_LOGE(TAG, "Fault indication. Trying to get details...");
      this->is_fault = true;
    } else {
      if(this->fault != nullptr && this->fault->state != "OK") this->fault->publish_state("OK");
    }
    
    if(slave_diagnostic_event && this->fault != nullptr) {
      char text[256];
      sprintf(text, "Режим диагностики");
      this->fault->publish_state(text);
    }

}

void OpenThermGWClimate::process_ASFflags(uint32_t &response) {
    uint8_t ub = (response >> 8) & 0xff;
    uint8_t oem_fault_code = response & 0xff;

    bool slave_service_request = ub & (1 << 0);
    bool slave_lockout_reset   = ub & (1 << 1);
    bool slave_low_water_press = ub & (1 << 2);
    bool slave_gas_flame_fault = ub & (1 << 3);
    bool slave_air_press_fault = ub & (1 << 4);
    bool slave_water_over_temp = ub & (1 << 5);

    ESP_LOGE(TAG, "Service required: %s", YESNO(slave_service_request));
    ESP_LOGE(TAG, "Remote reset: %s", YESNO(slave_lockout_reset));
    ESP_LOGE(TAG, "Water pressure fault: %s", YESNO(slave_low_water_press));
    ESP_LOGE(TAG, "Gas/flame fault: %s", YESNO(slave_gas_flame_fault));
    ESP_LOGE(TAG, "Air pressure fault: %s", YESNO(slave_air_press_fault));
    ESP_LOGE(TAG, "Over-temperature fault: %s", YESNO(slave_water_over_temp));

    if(this->fault != nullptr) {
      char text[256];
      sprintf(text, "Ошибка E%d", oem_fault_code);
      if(slave_service_request) {
        strcat(text, " Требуется сервис");
      }
      if(slave_low_water_press) {
        strcat(text, " Низкое давление воды");
      }
      if(slave_gas_flame_fault) {
        strcat(text, " Ошибка по газу/огню");
      }
      if(slave_air_press_fault) {
        strcat(text, " Ошибка по тяге воздуха");
      } 
      if(slave_water_over_temp) {
        strcat(text, " Перегрев теплоносителя");
      }
      this->fault->publish_state(text);
    }
}

void OpenThermGWClimate::process_SConfigSMemberIDcode(uint32_t &response) {
    uint8_t slave_configuration = (response >> 8) & 0xff;
    uint8_t slave_member_id_code = response & 0xff;

    bool slave_dhw_present                   = slave_configuration & (1 << 0);
    bool slave_control_type                  = slave_configuration & (1 << 1);
    bool slave_cooling_config                = slave_configuration & (1 << 2);
    bool slave_dhw_config                    = slave_configuration & (1 << 3);
    bool slave_low_off_pump_control_function = slave_configuration & (1 << 4);
    bool slave_ch2_present                   = slave_configuration & (1 << 5);

    ESP_LOGD(TAG, "Dhw is present: %s", YESNO(slave_dhw_present));
    ESP_LOGD(TAG, "Modulating: %s", YESNO(slave_control_type));
    ESP_LOGD(TAG, "Cooling supported: %s", YESNO(slave_cooling_config));
    ESP_LOGD(TAG, "dhw config: %s", YESNO(slave_dhw_config));
    ESP_LOGD(TAG, "Off pump control function: %s", YESNO(slave_low_off_pump_control_function));
    ESP_LOGD(TAG, "Ch2 present: %s", YESNO(slave_ch2_present));
}

void OpenThermGWClimate::process_MConfigMMemberIDcode(uint32_t &response) {
    uint8_t master_configuration = (response >> 8) & 0xff;
    uint8_t master_memberid_code = response & 0xff;
    ESP_LOGI(TAG, "Termostat memberid code: %d", master_memberid_code);
}

void OpenThermGWClimate::process_SlaveVersion(uint32_t &response) {
    uint8_t product_type = (response >> 8) & 0xff;
    uint8_t product_version = response & 0xff;
    ESP_LOGI(TAG, "Boiler type %d version %d", product_type, product_version);
}

void OpenThermGWClimate::process_TSet(uint32_t &request) {
    float control_setpoint = ot.getFloat(request);
    ESP_LOGD(TAG, "CH water temperature setpoint (°C): %f", control_setpoint);
}

void OpenThermGWClimate::process_RelModLevel(uint32_t &response) {
    float relative_modulation_level = ot.getFloat(response);
    ESP_LOGD(TAG, "Relative modulation level: %f", relative_modulation_level);
    if (this->relative_modulation_level != nullptr) {
      this->relative_modulation_level->publish_state(relative_modulation_level);
    }
}

void OpenThermGWClimate::process_CHPressure(uint32_t &response) {
    float ch_water_pressure = ot.getFloat(response);
    ESP_LOGD(TAG, "Ch water pressure: %f bar", ch_water_pressure);
    if (this->ch_water_pressure != nullptr) {
      this->ch_water_pressure->publish_state(ch_water_pressure);
    }
}

void OpenThermGWClimate::process_DHWFlowRate(uint32_t &response) {
    float dhw_flow_rate = ot.getFloat(response);
    ESP_LOGD(TAG, "Dhw flow rate: %f l/min", dhw_flow_rate);
    if (this->dhw_flow_rate != nullptr) {
      this->dhw_flow_rate->publish_state(dhw_flow_rate);
    }
}

void OpenThermGWClimate::process_Tboiler(uint32_t &response) {
    float boiler_water_temp = ot.getFloat(response);
    ESP_LOGD(TAG, "Boiler water temp: %f", boiler_water_temp);
    if (this->boiler_water_temp != nullptr) {
      this->boiler_water_temp->publish_state(boiler_water_temp);
    }
}

void OpenThermGWClimate::process_Tdhw(uint32_t &response) {
    float dhw_temperature = ot.getFloat(response);
    ESP_LOGD(TAG, "Dhw temperature: %f", dhw_temperature);
    if (this->dhw_temperature != nullptr) {
      this->dhw_temperature->publish_state(dhw_temperature);
    }
}

void OpenThermGWClimate::process_Toutside(uint32_t &response) {
    float outside_air_temperature = ot.getFloat(response);
    ESP_LOGD(TAG, "Outside air temperature: %f", outside_air_temperature);
    if (this->outside_air_temperature != nullptr) {
      this->outside_air_temperature->publish_state(outside_air_temperature);
    }
}

void OpenThermGWClimate::process_Tret(uint32_t &response) {
    float return_water_temperature = ot.getFloat(response);
    ESP_LOGD(TAG, "Return water temperature: %f", return_water_temperature);
    if (this->return_water_temperature != nullptr) {
      this->return_water_temperature->publish_state(return_water_temperature);
    }
}

void OpenThermGWClimate::request_Status(bool ch, bool dhw) {
  ot.sendRequestAync(ot.buildSetBoilerStatusRequest(ch, dhw, false, false, false));
}

void OpenThermGWClimate::request_TSet(float temp) {
  ot.sendRequestAync(ot.buildSetBoilerTemperatureRequest(temp));
}

void OpenThermGWClimate::request_Tboiler() {
  ot.sendRequestAync(ot.buildGetBoilerTemperatureRequest());
}

void OpenThermGWClimate::request_Tdhw() {
  ot.sendRequestAync(ot.buildRequest(OpenThermMessageType::READ_DATA, OpenThermMessageID::Tdhw, 0));
}

void OpenThermGWClimate::request_DHWFlowRate() {
  ot.sendRequestAync(ot.buildRequest(OpenThermRequestType::READ_DATA, OpenThermMessageID::DHWFlowRate, 0));
}

void OpenThermGWClimate::request_RelModLevel() {
  ot.sendRequestAync(ot.buildRequest(OpenThermRequestType::READ_DATA, OpenThermMessageID::RelModLevel, 0));
}

void OpenThermGWClimate::request_ASFflags() {
  ot.sendRequestAync(ot.buildRequest(OpenThermRequestType::READ_DATA, OpenThermMessageID::ASFflags, 0));
}

void OpenThermGWClimate::request_Toutside() {
  ot.sendRequestAync(ot.buildRequest(OpenThermRequestType::READ_DATA, OpenThermMessageID::Toutside, 0));
}

void OpenThermGWClimate::request_CHPressure() {
  ot.sendRequestAync(ot.buildRequest(OpenThermRequestType::READ_DATA, OpenThermMessageID::CHPressure, 0));
}

void OpenThermGWClimate::request_Tret() {
  ot.sendRequestAync(ot.buildRequest(OpenThermRequestType::READ_DATA, OpenThermMessageID::Tret, 0));
}

}  // namespace opentherm
}  // namespace esphome