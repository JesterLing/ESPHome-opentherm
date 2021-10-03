#include "esphome.h"
#include "OpenTherm.h"

static const unsigned int AIR_TEMP_MIN = 10;
static const unsigned int AIR_TEMP_MAX = 30;
static const float AIR_TEMP_STEP = 0.5;
static const unsigned int TIMEOUT_TRESHOLD = 10;

static const char* TAG = "OT"; // Logging tag

  OpenTherm ot(4, 5);


class BoilerDHWSwitch : public switch_::Switch, public Component {
 public:
 BoilerDHWSwitch(const std::string &name) : Switch(name)
  { }
  void setup() override {
  	//this->restore_state_ = true;
  	// auto restored = this->get_initial_state();
  	// ESP_LOGE(TAG, "  Restored state %s", ONOFF(*restored));
   // 	if (restored.has_value()) {
   		
   //  	  if (*restored) {
		 //     this->turn_on();
		 //   } else {
		 //     this->turn_off();
		 //   }
   //  }
    
  }

 void write_state(bool state) override {
    publish_state(state);
  }

};

class Boiler : public Component, public Climate, public CustomAPIDevice {
 private:


  // timer for pause after response
  unsigned long last_ot_send;
  // timer for OT
  unsigned long now_m;
  // increments if received no response
  int8_t timeout_count;
  // what request send in case
  unsigned int request;
  // enable/disable ch from ha
  bool enableCentralHeating;
  // bolier id; need to send in response
  uint8_t slaveMemberIDcode;
  // ch temp set for now
  int8_t chTempSet;
  // heating mode
  int method;
  // 
  int manualChTempSet;
  // interval in manual heating mode
  int interval = 10;

 	bool t_wait = false;
 	bool temp_reach = false;
 	float difference = 0;
 	float tmp_target = 0;
 	unsigned long ts_wait = 0;
 public:
  
  Sensor *house_temp{nullptr};
  Sensor *boiler_water_temp = new Sensor("Boiler CH Temp");
  Sensor *dhw_temperature = new Sensor("Boiler DHW Temp");
  Sensor *dhw_flow_rate = new Sensor("Boiler Water Flow Rate");
  Sensor *rel_mod_level = new Sensor("Boiler Rel Mod Level");

  BinarySensor *flame = new BinarySensor("Boiler flame");
  BinarySensor *connection = new BinarySensor("Boiler OT connection");

  TextSensor *fault = new TextSensor("Boiler Status");
  TextSensor *heat_interval = new TextSensor("Boiler Heat Interval");

  BoilerDHWSwitch *hot_water = new BoilerDHWSwitch("Boiler hot water");


  void setup() override {
    last_ot_send = (millis() + 15000); // first wait 15 sec before start ot
    timeout_count = 0;
    request = 0;
    enableCentralHeating = false; // default off
    chTempSet = 0;
    method = 1;

    this->hot_water->set_icon("mdi:coolant-temperature");

    this->boiler_water_temp->set_unit_of_measurement("°C");
    this->boiler_water_temp->set_accuracy_decimals(1);

    this->dhw_temperature->set_icon("mdi:coolant-temperature");
    this->dhw_temperature->set_unit_of_measurement("°C");
    this->dhw_temperature->set_accuracy_decimals(1);

    this->rel_mod_level->set_unit_of_measurement("%");
    this->rel_mod_level->set_icon("mdi:percent");

    this->dhw_flow_rate->set_unit_of_measurement("л/мин");
    this->dhw_flow_rate->set_icon("mdi:approximately-equal");
    this->dhw_flow_rate->set_accuracy_decimals(1);

    this->fault->set_icon("mdi:sync");
    this->connection->set_device_class("connectivity");
    
	this->heat_interval->publish_state("0");
    this->heat_interval->set_icon("mdi:camera-timer");


    if (this->house_temp) {
          this->house_temp->add_on_state_callback([this](float state) {
            this->current_temperature = state;
            this->publish_state();
          });
          this->current_temperature = this->house_temp->state;
    } else {
          this->current_temperature = NAN;
    }

	auto restore = this->restore_state_();
	    if (restore.has_value()) {
	      restore->apply(this);
		if(this->mode == climate::CLIMATE_MODE_OFF) {
			enableCentralHeating = false;
		} esle {
			enableCentralHeating = true;
		}
	    } else {
	      this->mode = climate::CLIMATE_MODE_OFF;
	      this->target_temperature = AIR_TEMP_MIN;
	    }

    register_service(&Boiler::change_mode, "boiler_change_mode", {"method", "ch_temp", "interval"});

    ot.begin(std::bind(&Boiler::handleInterrupt, this), std::bind(&Boiler::responseCallback, this, std::placeholders::_1, std::placeholders::_2));
 }
 
  void loop() override {
	if(!ot.isReady())
	{
 		ot.process();
 		return;
	}

	now_m = millis();
	if((last_ot_send < now_m) && (now_m - last_ot_send) > 700) {
		ESP_LOGD(TAG, "REQUEST №%i", request);
	    last_ot_send = now_m;
	    switch(request) {
	    	case 0:
          ot.sendRequestAync(ot.buildRequest(OpenThermRequestType::READ, OpenThermMessageID::SConfigSMemberIDcode, 0));
  				request = 10;
	    	break;
	    	case 10:
 			    ot.sendRequestAync(ot.buildRequest(OpenThermRequestType::READ, OpenThermMessageID::SlaveVersion, 0));
		      request = 11;
	    	break;
	    	case 11:
 			    ot.sendRequestAync(ot.buildRequest(OpenThermRequestType::WRITE, OpenThermMessageID::MConfigMMemberIDcode, slaveMemberIDcode));
				  request = 1;
	    	break;
	    	case 1:
	    		ot.sendRequestAync(ot.buildSetBoilerStatusRequest(enableCentralHeating, this->hot_water->state, false, false, false));
				  request = 2;
	    	break;
	    	case 2:
		        if(this->fault->state != "OK") {
		          request = 7;
		          break;
		        }
		        if(this->current_temperature == NAN) break;
				temp_calculation();
		    	ot.sendRequestAync(ot.buildSetBoilerTemperatureRequest(chTempSet));
		    	request = 3;
	    	break;
	    	case 3:
	    		ot.sendRequestAync(ot.buildGetBoilerTemperatureRequest());
	    		request = 4;
	    	break;
	    	case 4:
	    		ot.sendRequestAync(ot.buildRequest(OpenThermRequestType::READ, OpenThermMessageID::Tdhw, 0));
	    		request = 5;
	    	break;
	    	case 5:
	    		ot.sendRequestAync(ot.buildRequest(OpenThermRequestType::READ, OpenThermMessageID::DHWFlowRate, 0));
	    		if(this->flame->state || this->rel_mod_level->state != 0) request = 6;
	    		else request = 1;
	    	break;
	    	case 6:
	    		ot.sendRequestAync(ot.buildRequest(OpenThermRequestType::READ, OpenThermMessageID::RelModLevel, 0));
	    		request = 1;
	    	break;
	    	case 7:
	    		ot.sendRequestAync(ot.buildRequest(OpenThermRequestType::READ, OpenThermMessageID::ASFflags, 0));
	    		request = 1;
	    	break;
	    }
	}

	}


	void temp_calculation() {
		difference = this->target_temperature - this->current_temperature;
        if(!this->flame->state && temp_reach) {
            if(method == 1) {
             	if(difference >= 3) {
                    ts_wait = now_m + 1200000; // 20 min wait
                } else if(difference >= 2 && difference < 3) {
                       ts_wait = now_m + 1080000; // 18 min wait
                } else if(difference >= 1 && difference < 2) {
                       ts_wait = now_m + 900000; // 15 min wait
                } else if(difference >= 0.5 && difference < 1) {
                       ts_wait = now_m + 720000; // 12 min wait
                } else if(difference > 0 && difference < 0.5){
                       ts_wait = now_m + 600000; // 8 min wait
                }
            } else if (method == 2) {
              	ts_wait = now_m + interval * 60000;
            }
            t_wait = true;
            temp_reach = false;
            tmp_target = this->target_temperature;
        }
        if(t_wait) {
        	 chTempSet = 0;
            char text[17];
            unsigned int min = ((ts_wait - now_m) / 60000) % 60;
            unsigned int sec = ((ts_wait - now_m) / 1000) % 60;
            sprintf(text, "%d m %d s", min, sec);
            this->heat_interval->publish_state(text);
            if(tmp_target != this->target_temperature || now_m >= ts_wait) {
              t_wait = false;
              this->heat_interval->publish_state("0");
            }
	    }
	    if(difference > 0 && !t_wait) {
            if(this->heat_interval->state != "0") this->heat_interval->publish_state("0");
            if(method == 1) {
                if(difference >= 3) {
                    chTempSet = 59;
                } else if(difference >= 2 && difference < 3) {
                    chTempSet = 55;
                } else if(difference >= 1 && difference < 2) {
                    chTempSet = 50;
                } else if(difference >= 0.5 && difference < 1) {
                    chTempSet = 45;
                } else if(difference > 0 && difference < 0.5){
                    chTempSet = 43;
                 }
            } else if (method == 2) {
              	chTempSet = manualChTempSet;
            }
            if(chTempSet >= 40) {
               	if(this->boiler_water_temp->state >= chTempSet) temp_reach = true;
            }
        } else {
            if(!this->flame->state) chTempSet = 0;
        }
	}
  // method 1 - auto curves; 2 - auto PID; 3 - manual;
  // ch_temp - water temp for manual method
  // interval - pause between heating in minutes for manual mode
  void change_mode(int method, int ch_temp, int interval) {
    this->method = method;
    if(ch_temp > 80) {
      this->manualChTempSet = 80;
    } else if(ch_temp < 40) {
      this->manualChTempSet = 40;
    } else {
      this->manualChTempSet = ch_temp;
    }
    this->interval = interval;
        ESP_LOGI(TAG, "New mode: %i, Temp: %i, Interval: %i", this->method, this->manualChTempSet, this->interval);
  }

  void control(const ClimateCall &call) override {
    if (call.get_mode().has_value()) {
      // User requested mode change
      ClimateMode mode = *call.get_mode();

      if(mode == climate::CLIMATE_MODE_HEAT) {
      	enableCentralHeating = true;
      } else if(mode == climate::CLIMATE_MODE_OFF) {
        enableCentralHeating = false;
      }


    }
    if (call.get_target_temperature().has_value()) {
      this->target_temperature = *call.get_target_temperature();
      this->publish_state();
    }

  }

  ClimateTraits traits() override {
    // The capabilities of the climate device
    auto traits = climate::ClimateTraits();
    traits.set_supports_current_temperature(this->house_temp != nullptr);
 	traits.set_supported_modes({
          climate::CLIMATE_MODE_OFF,
          climate::CLIMATE_MODE_HEAT
      });
    traits.set_supports_action(true);
    traits.set_visual_min_temperature(AIR_TEMP_MIN);
    traits.set_visual_max_temperature(AIR_TEMP_MAX);
    traits.set_visual_temperature_step(AIR_TEMP_STEP);
    return traits;
  }


 void ICACHE_RAM_ATTR handleInterrupt() {
    ot.handleInterrupt();
  }



 void responseCallback(uint32_t result, OpenThermResponseStatus status) {
    switch (status)
    {
    case OpenThermResponseStatus::INVALID:
      ESP_LOGE(TAG, "Wrong boiler response via OT bus");
      break;
    case OpenThermResponseStatus::TIMEOUT:
      if (timeout_count <= TIMEOUT_TRESHOLD)
      {
      	timeout_count++;
        ESP_LOGW(TAG, "Boiler response timeout via OT bus [%i]", timeout_count);
      } else {
        ESP_LOGE(TAG, "There is no communication with the boiler via the OT bus, сheck the connection");
        this->connection->publish_state(false);
        request = 0;
      }
      break;
    case OpenThermResponseStatus::SUCCESS:
     	  ESP_LOGD(TAG, "Result of response: %u", result);
        if(timeout_count != 0 || !this->connection->state) {
          this->connection->publish_state(true);
          timeout_count = 0;
        }
        HandleReply(result);
      break;
    default:
      break;
    }
  }

 void HandleReply(uint32_t response) {
    OpenThermMessageID id = ot.getDataID(response);
    switch (id)
    {
    case OpenThermMessageID::SConfigSMemberIDcode:
          slaveMemberIDcode = response >> 0 & 0xFF; 
        break;
    case OpenThermMessageID::Status: {
       
    	bool updt = false;
        if(enableCentralHeating) {
          if(this->mode != climate::CLIMATE_MODE_HEAT) {
          	this->mode = climate::CLIMATE_MODE_HEAT;
          	updt = true;
          }
          if(ot.isCentralHeatingActive(response)) {
	          if(this->action != climate::CLIMATE_ACTION_HEATING) {
	          	 this->action = climate::CLIMATE_ACTION_HEATING;
	          	updt = true;
	          }
          } else {
	          if(this->action != climate::CLIMATE_ACTION_IDLE) {
	          	 this->action = climate::CLIMATE_ACTION_IDLE;
	          	updt = true;
	          }
          }
        } else {
	        if(this->mode != climate::CLIMATE_MODE_OFF || this->action != CLIMATE_ACTION_OFF) {
         		 this->mode = climate::CLIMATE_MODE_OFF;
         		 this->action = climate::CLIMATE_ACTION_OFF;
	          	updt = true;
	        }

        }

        if(updt) this->publish_state();

        flame->publish_state(ot.isFlameOn(response));

        if(!ot.isFault(response)) {
          char text[256];
          if(ot.isDiagnostic(response)) {
            sprintf(text, "Режим диагностики");
            fault->publish_state(text);
          } else {
            sprintf(text, "OK");
            fault->publish_state(text);
          }
        }

        break;
      }
    case OpenThermMessageID::Tboiler:
       		this->boiler_water_temp->publish_state(ot.getFloat(response));
        break;
    case OpenThermMessageID::Tdhw:
          this->dhw_temperature->publish_state(ot.getFloat(response));
        break;
    case OpenThermMessageID::DHWFlowRate:
          this->dhw_flow_rate->publish_state(ot.getFloat(response));
      break;
    case OpenThermMessageID::RelModLevel:
          this->rel_mod_level->publish_state((int8_t)ot.getFloat(response));
      break;
    case OpenThermMessageID::ASFflags: {
          char text[256];

          uint8_t flags = (response & 0xFFFF) >> 8;
          if(flags & 0x01) {
            sprintf(text, "Ошибка E%d. Требуется сервис", (response & 0xFF));
          } else if(flags & 0x04) {
            sprintf(text, "Ошибка E%d. Низкое давление воды", (response & 0xFF));
          } else if(flags & 0x08) {
            sprintf(text, "Ошибка E%d. Ошибка по газу/огню", (response & 0xFF));
          } else if(flags & 0x10) {
            sprintf(text, "Ошибка E%d. Ошибка по тяге воздуха", (response & 0xFF));  
          } else if(flags & 0x20) {
            sprintf(text, "Ошибка E%d. Перегрев теплоносителя", (response & 0xFF));
          } else {
            sprintf(text, "Ошибка E%d", (response & 0xFF));
          }

          // (flags & 0x02) - lockout_reset (удаленный сброс?)

          this->fault->publish_state(text);
        break;
      }
    case OpenThermMessageID::SlaveVersion:
    	ESP_LOGI(TAG, "Boiler type: %d version %d", ((response & 0xFFFF) >> 8), (response & 0xFF));
        break;
    case OpenThermMessageID::MasterVersion:
		  ESP_LOGI(TAG, "Termostat type: %d version %d", ((response & 0xFFFF) >> 8), (response & 0xFF));
        break;
    case OpenThermMessageID::MaxTSetUBMaxTSetLB:
        //  maxCHsetpUpp = (response & 0xFFFF) >> 8;
        // maxCHsetpLow = response & 0xFF;
    default:
      break;
    }

  }

};
