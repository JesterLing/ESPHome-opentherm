import esphome.codegen as cg
from esphome.components import binary_sensor, climate, number, output, select, sensor, text_sensor
from esphome.components.pid.climate import PIDClimate
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["opentherm"]
AUTO_LOAD = ["output", "text_sensor"]

CONF_HOUSE_TEMP = "house_temp"
CONF_PID_CLIMATE = "pid_climate"
CONF_PID_OUTPUT = "pid_output"
CONF_MODE_SELECT = "mode_select"
CONF_INTERVAL_NUMBER = "interval_number"
CONF_CH_TEMP_NUMBER = "ch_temp_number"
CONF_CH_SETPOINT = "ch_setpoint"
CONF_BOILER_TEMP = "boiler_temp"
CONF_FLAME_ON = "flame_on"
CONF_CH_ACTIVE = "ch_active"
CONF_HEAT_INTERVAL = "heat_interval"

boiler_climate_ns = cg.esphome_ns.namespace("boiler_climate")
BoilerClimate = boiler_climate_ns.class_("BoilerClimate", climate.Climate, cg.Component)
BoilerPidOutput = boiler_climate_ns.class_("BoilerPidOutput", output.FloatOutput, cg.Component)

CONFIG_SCHEMA = climate.climate_schema(BoilerClimate).extend(
    {
        cv.Required(CONF_HOUSE_TEMP): cv.use_id(sensor.Sensor),
        cv.Required(CONF_PID_CLIMATE): cv.use_id(PIDClimate),
        cv.Required(CONF_PID_OUTPUT): output.FLOAT_OUTPUT_SCHEMA.extend(
            {
                cv.Required(CONF_ID): cv.declare_id(BoilerPidOutput),
            }
        ),
        cv.Required(CONF_MODE_SELECT): cv.use_id(select.Select),
        cv.Required(CONF_INTERVAL_NUMBER): cv.use_id(number.Number),
        cv.Required(CONF_CH_TEMP_NUMBER): cv.use_id(number.Number),
        cv.Required(CONF_CH_SETPOINT): cv.use_id(number.Number),
        cv.Required(CONF_BOILER_TEMP): cv.use_id(sensor.Sensor),
        cv.Required(CONF_FLAME_ON): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_CH_ACTIVE): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_HEAT_INTERVAL): text_sensor.text_sensor_schema(),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)

    out_conf = config[CONF_PID_OUTPUT]
    out = cg.new_Pvariable(out_conf[CONF_ID])
    await output.register_output(out, out_conf)
    await cg.register_component(out, out_conf)
    cg.add(var.set_pid_output(out))

    cg.add(var.set_house_temp(await cg.get_variable(config[CONF_HOUSE_TEMP])))
    cg.add(var.set_pid_climate(await cg.get_variable(config[CONF_PID_CLIMATE])))
    cg.add(var.set_mode_select(await cg.get_variable(config[CONF_MODE_SELECT])))
    cg.add(var.set_interval_number(await cg.get_variable(config[CONF_INTERVAL_NUMBER])))
    cg.add(var.set_ch_temp_number(await cg.get_variable(config[CONF_CH_TEMP_NUMBER])))
    cg.add(var.set_ch_setpoint(await cg.get_variable(config[CONF_CH_SETPOINT])))
    cg.add(var.set_boiler_temp(await cg.get_variable(config[CONF_BOILER_TEMP])))
    cg.add(var.set_flame_on(await cg.get_variable(config[CONF_FLAME_ON])))
    cg.add(var.set_ch_active(await cg.get_variable(config[CONF_CH_ACTIVE])))
    cg.add(var.set_heat_interval(await text_sensor.new_text_sensor(config[CONF_HEAT_INTERVAL])))
