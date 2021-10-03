<img src="https://user-images.githubusercontent.com/29150943/135769411-5f3ec076-1856-414a-bb72-7c28f793265d.jpg" height="300">

Так выглядит мой термостат в Home Assistant <b><a href="https://user-images.githubusercontent.com/29150943/135770499-696640e5-6881-4ac7-9aa3-831cae0480f9.gif" target="_blank">135770499-696640e5-6881-4ac7-9aa3-831cae0480f9.gif</a></b>

Этот термостат имеет режимы работы и интервал простоя между нагревами. Так котел не тактует и не работает на износ, а теплый пол\радиаторы успевают отдать тепло. Такое поведение подсмотрено у оригинального термостата Bosch CR10(стоял у меня до этого, хотя в его основе похоже обычный pid). Проект еще находится в разработке, не все режимы работают.

В файле `bosch6000_heater.yaml` необходимо задать свой wi-fi и сенсор из HA который будет отдавать на ESP фактическую температуру помещения:

```
sensor:
  - platform: homeassistant
    id: mitemp_bt_temperature
    entity_id: sensor.mitemp_bt_temperature
```

Для того чтобы можно было выбирать несколько режимов работы, а также настройки ручного режима в HA в `configuration.yaml`:

```
input_number:
  heater_ch:
    name: "Нагрев до"
    initial: 40
    min: 40
    max: 80
    step: 1
    mode: slider
    unit_of_measurement: "°C"
    icon: mdi:thermometer
  heater_interval:
    name: "Интервал между"
    initial: 10
    min: 0
    max: 60
    step: 1
    mode: slider
    unit_of_measurement: "мин"
    icon: mdi:clock
input_select:
  heater_mode:
    name: Режим работы
    options:
      - Автоматический
      - Автоматический по PID алгоритму
      - Ручная установка
    initial: Автоматический
    icon: mdi:home-thermometer
```
При изменении параметров что выше, дергаем службу которая передаст новые настройки на ESP `automations.yaml`:
```
- id: heater_settings_change
  alias: Изменены настройки котла
  trigger:
    - platform: state
      entity_id: input_select.heater_mode
    - platform: state
      entity_id: input_number.heater_ch
    - platform: state
      entity_id: input_number.heater_interval
  action:
    - service: esphome.bosch6000_heater_boiler_change_mode
      data:
        method: >
          {% if states('sensor.boiler_status') == 'Автоматический' %}
          1
          {% elif states('sensor.boiler_status') == 'Автоматический по PID алгоритму' %}
          0
          {% else %}
          2
          {% endif %}
        ch_temp: > 
          {{ states('input_number.heater_ch') }}
        interval: >
          {{ states('input_number.heater_interval') }}
 ```

Уведомление на телефон если какая-то ошибка котла `automations.yaml`:
```
- id: push_when_boiler_error
  alias: Отправить уведомление если котел выдал ошибку
  trigger:
    platform: state
    entity_id: sensor.boiler_status
  condition:
    condition: not
    conditions:
    - condition: state
      entity_id: sensor.boiler_status
      state: OK
  action:
  - service: notify.notify
    data_template:
      data:
        presentation_options:
        - alert
        - badge
      message: >
        {% if states('sensor.boiler_status') == 'unavailable' %}
            Потеряно соединение с платой управления котлом. Проверьте качество сигнала и работоспособность роутера
        {% elif states('sensor.boiler_status') == 'unknown' %}
            Неизвестная ошибка с платой упраления котлом
        {% else %}
          Котел выдал ошибку! {{ states('sensor.boiler_status') }}
        {% endif %}
```

<ul>
  <li>Source OpenThem Library https://github.com/ihormelnyk/opentherm_library</li>
  <li>OpenThem Shield http://ihormelnyk.com/shop</li>
</ul>
