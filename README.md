<p align="center"><img src="https://user-images.githubusercontent.com/29150943/135769411-5f3ec076-1856-414a-bb72-7c28f793265d.jpg" height="300"></p>
<h2>Thermostat</h2>
<p>Thermostat has 2 modes - pid, manual. In manual mode, you can set the idle interval between heating. So the boiler is not clocking and does not work hard. Warm floor / radiators have time to give off heat. This behavior was peeped from the original Bosch CR10 thermostat that I had before.</p>
<p align="center">This is what my thermostat looks like in Home Assistant</p>
<p align="center"><img src="https://user-images.githubusercontent.com/29150943/196198138-7d824a3b-bd71-4fc2-b4f5-d7c89bddb40f.gif" height="700"></p>

<h2>How it works</h2>
<p>Communication with the boiler is handled by the native ESPHome <a href="https://esphome.io/components/opentherm/">OpenTherm component</a>. The local <code>components/boiler_climate</code> external component provides the thermostat: it calculates the CH setpoint in PID or manual mode and writes it to the OpenTherm <code>t_set</code> number. Requires ESPHome 2026.10 or newer.</p>

<h2>What do you need</h2>

- OpenTherm Adapter + Shield (buy here http://ihormelnyk.com/shop)
- Wemos D1 Mini
- Two-core wire
- Case (optional)

<h2>ToDo</h2>
<p>temperature curve mode</p>
