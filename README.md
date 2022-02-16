<p align="center"><img src="https://user-images.githubusercontent.com/29150943/135769411-5f3ec076-1856-414a-bb72-7c28f793265d.jpg" height="300"></p>
<p>Этот термостат имеет на выбор 3 алгоритма работы: авто, pid, ручной. Пид или температурные кривые пока не реализован т.к сам не знаю как к нему подступится. Кто хочет заняться полезные ссылки <a href="https://wdn.su/blog/1154" target="_blank">1</a>, <a href="https://esphome.io/components/climate/pid.html" target="_blank">2</a>.
Реализован интервал простоя между нагревами. Так котел не тактует и не работает на износ, а теплый пол/радиаторы успевают отдать тепло. Такое поведение подсмотрено у оригинального термостата Bosch CR10 который стоял у меня до этого.</p>
<p align="center">Так выглядит мой термостат в Home Assistant</p>
<p align="center"><img src="https://user-images.githubusercontent.com/29150943/135770499-696640e5-6881-4ac7-9aa3-831cae0480f9.gif" height="700"></p>

<h2>Что нужно</h2>

* OpenTherm Adapter + Shield (купить можно здесь http://ihormelnyk.com/shop)
* Wemos D1 Mini
* Провод двужильный
* Корпус (опционально)

<h2>Настройка</h2>

Перед прошивкой в ESPHome в файле `heater.yaml` необходимо задать свой Wi-Fi и сенсор из HA который будет отдавать фактическую температуру помещения:

```
sensor:
  - platform: homeassistant
    id: house_temp_sensor
    entity_id: sensor.mitemp_bt_temperature
```

Если у вас нет датчика температуры или он не выведен на пол необходимо отредактировать:

```
- platform: dallas
  address: 0x703C01F095AC1E28     #https://esphome.io/components/sensor/dallas.html
  name: "Warm floor temperature"
  filters:
  - sliding_window_moving_average:
      window_size: 2
      send_every: 2
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
Создаем автоматизацию что при изменении параметров что выше, дергаем службу которая передаст новые настройки на ESP `automations.yaml`:
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
    - service: esphome.beater_boiler_change_mode
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
