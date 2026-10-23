#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/number/number.h"
#include "esphome/components/output/float_output.h"
#include "esphome/components/pid/pid_climate.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

namespace esphome {
namespace boiler_climate {

	class BoilerPidOutput : public Component, public output::FloatOutput {
	public:
		float get_state() const { return this->state_; }
		void setup() override { }

	protected:
		float state_ { 0.0f };
		void write_state(float state) override { this->state_ = state; }
	};

	// Thermostat on top of the native ESPHome opentherm component: computes the CH setpoint
	// in PID or manual mode and writes it to the opentherm t_set number
	class BoilerClimate : public climate::Climate, public Component {
	public:
		void setup() override;
		void dump_config() override;

		void set_house_temp(sensor::Sensor* house_temp) { this->house_temp_ = house_temp; }
		void set_pid_climate(pid::PIDClimate* pid_climate) { this->pid_calc_ = pid_climate; }
		void set_pid_output(BoilerPidOutput* pid_output) { this->pid_output_ = pid_output; }
		void set_mode_select(select::Select* mode_select) { this->mode_select_ = mode_select; }
		void set_interval_number(number::Number* interval_number) { this->interval_number_ = interval_number; }
		void set_ch_temp_number(number::Number* ch_temp_number) { this->ch_temp_number_ = ch_temp_number; }
		void set_ch_setpoint(number::Number* ch_setpoint) { this->ch_setpoint_ = ch_setpoint; }
		void set_boiler_temp(sensor::Sensor* boiler_temp) { this->boiler_water_temp_ = boiler_temp; }
		void set_flame_on(binary_sensor::BinarySensor* flame_on) { this->flame_on_ = flame_on; }
		void set_ch_active(binary_sensor::BinarySensor* ch_active) { this->ch_active_ = ch_active; }
		void set_heat_interval(text_sensor::TextSensor* heat_interval) { this->heat_interval_ = heat_interval; }

	protected:
		void control(const climate::ClimateCall& call) override;
		climate::ClimateTraits traits() override;

		sensor::Sensor* house_temp_ { nullptr };
		pid::PIDClimate* pid_calc_ { nullptr };
		BoilerPidOutput* pid_output_ { nullptr };
		select::Select* mode_select_ { nullptr };
		number::Number* interval_number_ { nullptr };
		number::Number* ch_temp_number_ { nullptr };
		number::Number* ch_setpoint_ { nullptr };
		sensor::Sensor* boiler_water_temp_ { nullptr };
		binary_sensor::BinarySensor* flame_on_ { nullptr };
		binary_sensor::BinarySensor* ch_active_ { nullptr };
		text_sensor::TextSensor* heat_interval_ { nullptr };

	private:
		void update_setpoint();
		void update_action();
		float calcutateTemp();
		bool mode_is(const char* option);
		void publish_heat_interval(const std::string& text);

		unsigned long now_timesmap { 0 };

		// wait between heats in manual mode
		bool heatup { false };
		bool temp_reach { false };
		bool interval_wait { false };
		unsigned long interval_timesmap { 0 };
	};

} // namespace boiler_climate
} // namespace esphome
