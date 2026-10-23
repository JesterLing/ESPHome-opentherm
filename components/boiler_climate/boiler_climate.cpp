#include "boiler_climate.h"
#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/util.h"

namespace esphome {
namespace boiler_climate {
	static const char* const TAG = "boiler_climate";

	// how often the CH setpoint is recalculated and sent to the opentherm hub
	static const uint32_t UPDATE_INTERVAL = 1000;

	void BoilerClimate::setup()
	{
		auto restore = this->restore_state_();
		if(restore.has_value()) {
			restore->to_call(this).perform();
		} else {
			this->target_temperature = 10;
		}

		this->current_temperature = this->house_temp_->state;
		this->house_temp_->add_on_state_callback([this](float state) {
			ESP_LOGD(TAG, "Inhouse temp changed %f", state);
			this->current_temperature = state;
			this->publish_state();
		});

		this->ch_active_->add_on_state_callback([this](bool state) { this->update_action(); });

		this->publish_heat_interval("0");

		this->set_interval("update_setpoint", UPDATE_INTERVAL, [this]() { this->update_setpoint(); });
	}

	void BoilerClimate::update_setpoint()
	{
		this->now_timesmap = millis();
		float ch_temp = calcutateTemp();
		if(this->ch_setpoint_->state != ch_temp) {
			ESP_LOGD(TAG, "CH water temperature setpoint (°C): %f", ch_temp);
			this->ch_setpoint_->make_call().set_value(ch_temp).perform();
		}
	}

	void BoilerClimate::update_action()
	{
		climate::ClimateAction new_action = this->ch_active_->state ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_IDLE;
		if(this->mode == climate::CLIMATE_MODE_OFF)
			new_action = climate::CLIMATE_ACTION_OFF;

		if(new_action != this->action) {
			this->action = new_action;
			this->publish_state();
		}
	}

	bool BoilerClimate::mode_is(const char* option)
	{
		auto active = this->mode_select_->active_index();
		auto index = this->mode_select_->index_of(option);
		return active.has_value() && index.has_value() && *active == *index;
	}

	void BoilerClimate::publish_heat_interval(const std::string& text)
	{
		if(!this->heat_interval_->has_state() || this->heat_interval_->get_state() != text)
			this->heat_interval_->publish_state(text);
	}

	float BoilerClimate::calcutateTemp()
	{
		if(!this->mode_select_->active_index().has_value() || this->mode == climate::CLIMATE_MODE_OFF || !network::is_connected() || !remote_is_connected())
			return 0.0f;

		if(mode_is("Автоматический PID")) {
			return this->pid_output_->get_state() * 85;
		} else if(mode_is("Ручной")) {
			float ch_temp = 0;

			if(!this->flame_on_->state && this->temp_reach && this->interval_number_->state != 0) { // calculate the waiting interval
				this->interval_wait = true;
				this->interval_timesmap = this->now_timesmap + this->interval_number_->state * 60000;
				this->temp_reach = false;
				this->heatup = false;
			}

			if(this->interval_wait) { // if now is a waiting interval, count the time until the end of the interval
				if(this->now_timesmap >= this->interval_timesmap) { // if wait interval is over
					this->interval_wait = false;
					this->publish_heat_interval("0");
				} else {
					char text[17];
					unsigned int min = ((this->interval_timesmap - this->now_timesmap) / 60000) % 60;
					unsigned int sec = ((this->interval_timesmap - this->now_timesmap) / 1000) % 60;
					snprintf(text, sizeof(text), "%u m %u s", min, sec);
					this->publish_heat_interval(text);
				}
			} else { // if it's time to heat up the water
				ch_temp = this->ch_temp_number_->state;
				this->publish_heat_interval("0");
				if(!std::isnan(this->current_temperature)) { // if we have a room temperature sensor decide heat or not
					float difference = this->target_temperature - this->current_temperature;
					if(difference > 0)
						this->heatup = true;
					if(difference <= 0 && (!this->heatup || difference < -1.5)) {
						ch_temp = 0;
						this->heatup = false;
					}
				}
				if(ch_temp >= 40) {
					if(this->boiler_water_temp_->state >= ch_temp && this->flame_on_->state) { // if boiler has heated the water to the set temperature, start the waiting interval
						this->temp_reach = true;
					}
				}
			}
			return ch_temp;
		} else {
			return 0.0f;
		}
	}

	void BoilerClimate::control(const climate::ClimateCall& call)
	{
		if(call.get_mode().has_value()) {
			this->mode = *call.get_mode();
			this->pid_calc_->make_call().set_mode(this->mode).perform();
		}
		if(call.get_target_temperature().has_value()) {
			this->target_temperature = *call.get_target_temperature();
			this->pid_calc_->make_call().set_target_temperature(this->target_temperature).perform();
		}
		this->interval_wait = false;
		this->publish_heat_interval("0");
		this->publish_state();
		this->update_action();
	}

	climate::ClimateTraits BoilerClimate::traits()
	{
		auto traits = climate::ClimateTraits();
		traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
		traits.set_supported_modes({ climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT });
		traits.set_visual_min_temperature(10);
		traits.set_visual_max_temperature(30);
		traits.set_visual_temperature_step(0.5);
		return traits;
	}

	void BoilerClimate::dump_config() { LOG_CLIMATE("", "Boiler Climate", this); }

} // namespace boiler_climate
} // namespace esphome
