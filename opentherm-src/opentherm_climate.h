#pragma once

#include "esphome.h"
#include "opentherm.h"

namespace esphome {
namespace opentherm {

	class OpenThermGWOutput : public Component, public output::FloatOutput {
	public:
		float get_state() const { return this->state_; }
		void setup() override { }

	protected:
		float state_ { 0.0f };
		void write_state(float state) override { this->state_ = state; }
	};

	class OpenThermGWClimate : public climate::Climate, public Component {
	public:
		OpenThermGWClimate(int pin_in, int pin_out);
		void setup() override;
		void dump_config() override;
		void loop() override;
		void handleInterrupt();

		sensor::Sensor* house_temp { nullptr };
		text_sensor::TextSensor* fault { nullptr };
		text_sensor::TextSensor* heat_interval { nullptr };
		binary_sensor::BinarySensor* connection { nullptr };
		sensor::Sensor* boiler_water_temp { nullptr };
		sensor::Sensor* ch_water_pressure { nullptr };
		sensor::Sensor* dhw_flow_rate { nullptr };
		sensor::Sensor* dhw_temperature { nullptr };
		sensor::Sensor* outside_air_temperature { nullptr };
		sensor::Sensor* relative_modulation_level { nullptr };
		sensor::Sensor* return_water_temperature { nullptr };
		pid::PIDClimate* pid_calc { nullptr };
		OpenThermGWOutput* pid_output { nullptr };
		template_::TemplateSwitch* hot_water { nullptr };
		binary_sensor::BinarySensor* flame_on { nullptr };
		template_::TemplateSelect* mode_select { nullptr };
		template_::TemplateNumber* interval_number { nullptr };
		template_::TemplateNumber* ch_temp_number = { nullptr };

	protected:
		OpenTherm ot;

		void control(const climate::ClimateCall& call) override;
		climate::ClimateTraits traits() override;

		void processResponse(uint32_t response, OpenThermResponseStatus status);
		void handleReply(uint32_t& response);

		void process_ASFflags(uint32_t& response);
		void process_CHPressure(uint32_t& response);
		void process_DHWFlowRate(uint32_t& response);
		void process_RelModLevel(uint32_t& response);
		void process_SlaveVersion(uint32_t& response);
		void process_Status(uint32_t& response);
		void process_SConfigSMemberIDcode(uint32_t& response);
		void process_Tboiler(uint32_t& response);
		void process_Tdhw(uint32_t& response);
		void process_Toutside(uint32_t& response);
		void process_Tret(uint32_t& response);
		void process_MConfigMMemberIDcode(uint32_t& response);
		void process_TSet(uint32_t& response);

		void request_Status(bool ch, bool dhw);
		void request_TSet(float temp);
		void request_Tboiler();
		void request_Tdhw();
		void request_DHWFlowRate();
		void request_RelModLevel();
		void request_ASFflags();
		void request_Toutside();
		void request_CHPressure();
		void request_Tret();

	private:
		float calcutateTemp();

		// timer for OT between requests
		unsigned long now_timesmap;
		unsigned long last_request_timesmap;

		// increments if received no response
		uint8_t timeout_count;

		// current request
		uint8_t next_request;

		// wait between heats in manual mode
		bool heatup;
		bool temp_reach;
		bool interval_wait;
		unsigned long interval_timesmap;

		// if msg_status give fault next request asf_flasg_oem_fault_code
		bool is_fault;
	};

} // namespace opentherm
} // namespace esphome