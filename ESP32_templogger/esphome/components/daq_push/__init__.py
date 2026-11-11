"""
daq_push – eigene ESPHome-Komponente des DAQ-Loggers (Studienarbeit).

Sammelt alle 10 s die DS18B20-Temperaturen (dallas_temp) und die Modbus-Rohregister
(modbus_controller-Textsensoren mit raw_encode: HEXBYTES) und sendet sie als EINEN
JSON-Request im Format API_SCHNITTSTELLE.md v0.3 an die Python-API.

Damit nichts doppelt gepflegt werden muss, liest die Konfigurationsprüfung aus den
übrigen Blöcken der YAML ab:
  - ROM-ID eines Fühlers      <- 'address' des dallas_temp-Sensors
  - Zählernummer (meter)      <- 'address' des zugehörigen modbus_controller
  - abzufragende Zähler       <- alle modbus_controller der angegebenen Register
und prüft, dass diese Bausteine 'update_interval: never' haben (sonst würden sie
zusätzlich in ihrem eigenen Takt messen).
"""

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import http_request, time as time_
from esphome.components.dallas_temp.sensor import DallasTemperatureSensor
from esphome.components.modbus_controller import ModbusController
from esphome.components.modbus_controller.text_sensor import ModbusTextSensor
from esphome.const import CONF_ADDRESS, CONF_ID, CONF_TIME_ID, CONF_URL, SCHEDULER_DONT_RUN
from esphome.core import CORE

CODEOWNERS = ["@studienarbeit"]
DEPENDENCIES = ["network", "http_request", "time"]
AUTO_LOAD = ["json", "sensor", "text_sensor"]

CONF_DEVICE_ID = "device_id"
CONF_FIRMWARE = "firmware"
CONF_API_KEY = "api_key"
CONF_COLLECT_TIME = "collect_time"
CONF_TEMP_MIN = "temp_min"
CONF_TEMP_MAX = "temp_max"
CONF_PLAUSIBLE_DELTA_85 = "plausible_delta_85"
CONF_TEMPERATURES = "temperatures"
CONF_SENSOR_ID = "sensor_id"
CONF_NAME = "name"
CONF_METER_REGISTERS = "meter_registers"
CONF_TEXT_SENSOR_ID = "text_sensor_id"
CONF_QUANTITY = "quantity"
CONF_UNIT = "unit"
CONF_TYPE = "type"
CONF_WORD_ORDER = "word_order"
CONF_SCALE = "scale"

daq_push_ns = cg.esphome_ns.namespace("daq_push")
DaqPush = daq_push_ns.class_("DaqPush", cg.PollingComponent)

daq_ns = cg.global_ns.namespace("daq")
RegType = daq_ns.enum("RegType", is_class=True)
WordOrder = daq_ns.enum("WordOrder", is_class=True)
REG_TYPES = {
    "FLOAT32": RegType.FLOAT32,
    "UINT32": RegType.UINT32,
    "INT32": RegType.INT32,
    "UINT16": RegType.UINT16,
    "INT16": RegType.INT16,
}
REG_WORDS = {"FLOAT32": 2, "UINT32": 2, "INT32": 2, "UINT16": 1, "INT16": 1}
WORD_ORDERS = {"HIGH_FIRST": WordOrder.HIGH_FIRST, "LOW_FIRST": WordOrder.LOW_FIRST}


def _max_len(n):
    def validator(value):
        value = cv.string_strict(value)
        if not 0 < len(value) <= n:
            raise cv.Invalid(f"1 bis {n} Zeichen erlaubt (API_SCHNITTSTELLE.md)")
        return value

    return validator


def _http_url(value):
    value = cv.string_strict(value)
    if not value.startswith(("http://", "https://")):
        raise cv.Invalid("URL muss mit http:// oder https:// beginnen")
    return value


TEMP_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SENSOR_ID): cv.use_id(DallasTemperatureSensor),
        cv.Optional(CONF_NAME): _max_len(31),
    }
)

METER_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_TEXT_SENSOR_ID): cv.use_id(ModbusTextSensor),
        cv.Required(CONF_QUANTITY): _max_len(31),
        cv.Required(CONF_UNIT): _max_len(8),
        cv.Required(CONF_TYPE): cv.enum(REG_TYPES, upper=True),
        cv.Optional(CONF_WORD_ORDER, default="HIGH_FIRST"): cv.enum(WORD_ORDERS, upper=True),
        cv.Optional(CONF_SCALE, default=1.0): cv.float_,
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(DaqPush),
        cv.Required(CONF_DEVICE_ID): _max_len(32),
        cv.Optional(CONF_FIRMWARE, default="2.0.0-esphome"): cv.string_strict,
        cv.Required(CONF_URL): _http_url,
        cv.Required(CONF_API_KEY): cv.string_strict,
        cv.GenerateID(CONF_TIME_ID): cv.use_id(time_.RealTimeClock),
        cv.GenerateID(http_request.CONF_HTTP_REQUEST_ID): cv.use_id(http_request.HttpRequestComponent),
        cv.Optional(CONF_COLLECT_TIME, default="2s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_TEMP_MIN, default=-40.0): cv.float_,
        cv.Optional(CONF_TEMP_MAX, default=110.0): cv.float_,
        cv.Optional(CONF_PLAUSIBLE_DELTA_85, default=5.0): cv.float_,
        cv.Optional(CONF_TEMPERATURES, default=[]): cv.ensure_list(TEMP_SCHEMA),
        cv.Optional(CONF_METER_REGISTERS, default=[]): cv.ensure_list(METER_SCHEMA),
    }
).extend(cv.polling_component_schema("10s"))


# --- Nachschlagen in der vollständigen Konfiguration -------------------------------
def _find(full_config, domain, ident):
    for conf in full_config.get(domain, []):
        if conf[CONF_ID].id == ident.id:
            return conf
    return None


def _never(conf):
    return conf["update_interval"].total_milliseconds == SCHEDULER_DONT_RUN


def _rom_hex(address):
    """ESPHome-Adresse (Familiencode im niederwertigsten Byte) -> ROM-ID wie in der API."""
    return "".join(f"{(address >> (8 * i)) & 0xFF:02X}" for i in range(8))


def _final_validate(config):
    full = fv.full_config.get()
    for i, t in enumerate(config[CONF_TEMPERATURES]):
        path = [CONF_TEMPERATURES, i, CONF_SENSOR_ID]
        s = _find(full, "sensor", t[CONF_SENSOR_ID])
        if CONF_ADDRESS not in s:
            raise cv.Invalid("dallas_temp-Sensor braucht 'address' (nicht 'index'), "
                             "damit die ROM-ID feststeht", path=path)
        if not _never(s):
            raise cv.Invalid("dallas_temp-Sensor muss 'update_interval: never' haben "
                             "(daq_push stößt die Messung an)", path=path)
    for i, m in enumerate(config[CONF_METER_REGISTERS]):
        path = [CONF_METER_REGISTERS, i, CONF_TEXT_SENSOR_ID]
        ts = _find(full, "text_sensor", m[CONF_TEXT_SENSOR_ID])
        if str(ts.get("raw_encode")) != "HEXBYTES":
            raise cv.Invalid("Modbus-Textsensor braucht 'raw_encode: HEXBYTES'", path=path)
        need = 2 * REG_WORDS[m[CONF_TYPE]]
        if ts.get("response_size") != need:
            raise cv.Invalid(f"Typ {m[CONF_TYPE]} braucht 'response_size: {need}'", path=path)
        ctrl = _find(full, "modbus_controller", ts["modbus_controller_id"])
        if not 1 <= ctrl[CONF_ADDRESS] <= 247:
            raise cv.Invalid("Zähleradresse muss 1..247 sein", path=path)
        if not _never(ctrl):
            raise cv.Invalid(f"modbus_controller '{ctrl[CONF_ID].id}' muss 'update_interval: never' "
                             "haben (daq_push stößt die Abfrage an)", path=path)
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_device_id(config[CONF_DEVICE_ID]))
    cg.add(var.set_firmware(config[CONF_FIRMWARE]))
    cg.add(var.set_url(config[CONF_URL]))
    cg.add(var.set_api_key(config[CONF_API_KEY]))
    cg.add(var.set_collect_time(config[CONF_COLLECT_TIME]))
    cg.add(var.set_temp_limits(config[CONF_TEMP_MIN], config[CONF_TEMP_MAX], config[CONF_PLAUSIBLE_DELTA_85]))
    cg.add(var.set_time(await cg.get_variable(config[CONF_TIME_ID])))
    cg.add(var.set_http(await cg.get_variable(config[http_request.CONF_HTTP_REQUEST_ID])))

    for t in config[CONF_TEMPERATURES]:
        s = await cg.get_variable(t[CONF_SENSOR_ID])
        rom = _rom_hex(_find(CORE.config, "sensor", t[CONF_SENSOR_ID])[CONF_ADDRESS])
        cg.add(var.add_temperature(s, s, rom, t.get(CONF_NAME, "")))

    controllers = []
    for m in config[CONF_METER_REGISTERS]:
        ts = await cg.get_variable(m[CONF_TEXT_SENSOR_ID])
        ctrl_conf = _find(CORE.config, "modbus_controller",
                          _find(CORE.config, "text_sensor", m[CONF_TEXT_SENSOR_ID])["modbus_controller_id"])
        # Skalierung als double-Literal ausgeben (codegen macht aus Python-float
        # sonst "0.001f" -> float-Rundungsfehler bei großen Zählerständen).
        scale = cg.RawExpression(repr(float(m[CONF_SCALE])))
        cg.add(var.add_meter_register(ts, ctrl_conf[CONF_ADDRESS], m[CONF_QUANTITY], m[CONF_UNIT],
                                      m[CONF_TYPE], m[CONF_WORD_ORDER], scale))
        if ctrl_conf[CONF_ID].id not in controllers:
            controllers.append(ctrl_conf[CONF_ID].id)
            cg.add(var.add_modbus_controller(await cg.get_variable(ctrl_conf[CONF_ID])))
