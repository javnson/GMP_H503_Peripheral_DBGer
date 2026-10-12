#include "gmp_standard_cli.h"
#include "gmp_host.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
const std::vector<std::string> names = {"gpio", "i2c", "i3c", "spi", "qep", "uart", "rs232", "rs485", "can"};

bool known(const std::string &name) {
  return std::find(names.begin(), names.end(), name) != names.end();
}
json defaults(const std::string &name) {
  if (name == "gpio") return {{"pin","PA0"},{"mode","input"},{"value",0}};
  if (name == "i2c") return {{"role","master"},{"address",80},{"baud",100000},{"profile","generic"},{"capacity",256},{"page_size",8}};
  if (name == "i3c") return {{"role","i3c-master"},{"address",48}};
  if (name == "spi") return {{"role","master"},{"mode",0},{"divider",7}};
  if (name == "qep") return {{"role","counter"}};
  if (name == "can") return {{"role","peer"},{"baud",500000},{"protocol","raw"}};
  return {{"role","master"},{"baud",115200},{"protocol","raw"}};
}
void fail_if(bool condition, const std::string &message) {
  if (condition) throw std::runtime_error(message);
}
unsigned value_uint(const json &v, const std::string &key) {
  fail_if(!v.contains(key) || !v[key].is_number_integer() ||
          v[key].get<int64_t>() < 0 || v[key].get<uint64_t>() > UINT32_MAX,
          "invalid unsigned integer: " + key);
  return v[key].get<unsigned>();
}
std::string value_text(const json &v, const std::string &key) {
  fail_if(!v.contains(key) || !v[key].is_string(), "invalid text: " + key);
  return v[key].get<std::string>();
}
void one_of(const std::string &value, const std::vector<std::string> &allowed, const std::string &key) {
  fail_if(std::find(allowed.begin(), allowed.end(), value) == allowed.end(), "invalid " + key + ": " + value);
}
void validate(const std::string &name, const json &v) {
  fail_if(!v.is_object(), name + " config must be a JSON object");
  json model = defaults(name);
  for (auto it = v.begin(); it != v.end(); ++it)
    fail_if(!model.contains(it.key()), name + " has unknown parameter: " + it.key());
  if (name == "gpio") {
    one_of(value_text(v,"pin"), {"PA0","PA1"}, "pin");
    one_of(value_text(v,"mode"), {"input","output","adc"}, "mode");
    fail_if(value_uint(v,"value") > 1, "GPIO value must be 0 or 1");
    fail_if(value_text(v,"pin") == "PA1" && value_text(v,"mode") == "output", "PA1 output conflicts with TPLD");
  } else if (name == "i2c") {
    one_of(value_text(v,"role"), {"master","slave"}, "role");
    one_of(value_text(v,"profile"), {"generic","hdc1080","eeprom"}, "profile");
    fail_if(value_uint(v,"address") < 8 || value_uint(v,"address") > 0x77, "I2C address must be 0x08..0x77");
    fail_if(value_uint(v,"baud") < 1000 || value_uint(v,"baud") > 400000, "I2C baud outside 1k..400k");
    fail_if(value_uint(v,"capacity") != 256 && value_uint(v,"capacity") != 16384 &&
            value_uint(v,"capacity") != 32768 && value_uint(v,"capacity") != 65536, "unsupported EEPROM capacity");
    fail_if(value_uint(v,"page_size") != 8 && value_uint(v,"page_size") != 64 &&
            value_uint(v,"page_size") != 128, "unsupported EEPROM page size");
    fail_if((value_uint(v,"capacity") == 256 && value_uint(v,"page_size") != 8) ||
            ((value_uint(v,"capacity") == 16384 || value_uint(v,"capacity") == 32768) &&
             value_uint(v,"page_size") != 64) ||
            (value_uint(v,"capacity") == 65536 && value_uint(v,"page_size") != 128),
            "EEPROM capacity/page size pair is unsupported");
  } else if (name == "i3c") {
    one_of(value_text(v,"role"), {"i3c-master","i3c-slave","i2c-master","i2c-slave"}, "role");
    fail_if(value_uint(v,"address") > 0x77, "I3C address out of range");
  } else if (name == "spi") {
    one_of(value_text(v,"role"), {"master","slave"}, "role");
    fail_if(value_uint(v,"mode") > 3 || value_uint(v,"divider") > 7, "SPI mode/divider out of range");
  } else if (name == "qep") {
    one_of(value_text(v,"role"), {"counter"}, "role");
  } else if (name == "can") {
    one_of(value_text(v,"role"), {"peer"}, "role");
    one_of(value_text(v,"protocol"), {"raw","canopen"}, "protocol");
    fail_if(value_uint(v,"baud") != 125000 && value_uint(v,"baud") != 250000 &&
            value_uint(v,"baud") != 500000 && value_uint(v,"baud") != 1000000, "unsupported CAN baud");
  } else {
    one_of(value_text(v,"role"), {"master","slave"}, "role");
    one_of(value_text(v,"protocol"), {"raw","modbus-rtu","at","register"}, "protocol");
    fail_if(value_uint(v,"baud") < 1200 || value_uint(v,"baud") > 2000000, "serial baud outside 1200..2M");
  }
}
void merge(json &base, const json &patch, const std::string &name) {
  fail_if(!patch.is_object(), name + " section must be a JSON object");
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    fail_if(!base.contains(it.key()), name + " has unknown parameter: " + it.key());
    base[it.key()] = it.value();
  }
  validate(name, base);
}
json parse_json(std::istream &stream) {
  json result = json::parse(stream, nullptr, true);
  fail_if(!result.is_object(), "config root must be a JSON object");
  return result;
}
json read_json_file(const std::string &path) {
  std::ifstream file(path);
  fail_if(!file, "cannot open JSON file: " + path);
  return parse_json(file);
}
json sections(const json &root) {
  if (root.contains("interfaces")) {
    fail_if(!root["interfaces"].is_object(), "interfaces must be a JSON object");
    return root["interfaces"];
  }
  return root;
}
unsigned number(const std::string &s) {
  std::string text = s;
  unsigned multiplier = 1;
  if (!text.empty() && (text.back() == 'k' || text.back() == 'K')) { multiplier = 1000; text.pop_back(); }
  else if (!text.empty() && (text.back() == 'm' || text.back() == 'M')) { multiplier = 1000000; text.pop_back(); }
  size_t n = 0;
  unsigned long value = std::stoul(text, &n, 0);
  fail_if(n != text.size() || value > UINT32_MAX / multiplier, "invalid number: " + s);
  return unsigned(value * multiplier);
}
json scalar(const std::string &key, const std::string &text, const json &model) {
  fail_if(!model.contains(key), "unknown parameter: " + key);
  if (model[key].is_string()) return text;
  return number(text);
}
std::string port_name(int argc, char **argv, int &first) {
  first = 1;
  if (argc > 2 && std::string(argv[1]) == "--port") { first = 3; return argv[2]; }
  const char *env = std::getenv("GMP_PORT");
  return env ? env : "";
}
fs::path state_path(const std::string &port) {
  const char *dir = std::getenv("GMP_STATE_DIR");
#ifdef _WIN32
  if (!dir) dir = std::getenv("LOCALAPPDATA");
#else
  if (!dir) dir = std::getenv("XDG_STATE_HOME");
  if (!dir) dir = std::getenv("HOME");
#endif
  fail_if(!dir, "set GMP_STATE_DIR for persistent CLI state");
  std::string safe = port;
  for (char &c : safe) if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
  return fs::path(dir) / "GMP_Peripheral_Host" / (safe + ".json");
}
json state_load(const std::string &port) {
  fs::path path = state_path(port);
  if (!fs::exists(path)) return json::object();
  json state = read_json_file(path.string());
  fail_if(!state.contains("interfaces") || !state["interfaces"].is_object(), "invalid saved CLI state");
  return state["interfaces"];
}
void state_save(const std::string &port, const json &state) {
  fs::path path = state_path(port);
  fs::create_directories(path.parent_path());
  fs::path temporary = path; temporary += ".tmp";
  { std::ofstream file(temporary, std::ios::trunc);
    fail_if(!file, "cannot write CLI state");
    file << json({{"version",1},{"interfaces",state}}).dump(2) << '\n';
    fail_if(!file, "cannot flush CLI state");
  }
  if (fs::exists(path)) fs::remove(path);
  fs::rename(temporary, path);
}
json config_of(const json &state, const std::string &name) {
  json current = defaults(name);
  if (state.contains(name) && state[name].is_object() && state[name].contains("config"))
    merge(current, state[name]["config"], name);
  return current;
}
bool bound(const json &state, const std::string &name) {
  return state.contains(name) && state[name].is_object() && state[name].value("bound", false);
}
void apply(gmp::Device &device, const std::string &name, const json &v) {
  if (name == "gpio") {
    uint8_t pin = value_text(v,"pin") == "PA0" ? 0 : 1;
    std::string mode = value_text(v,"mode");
    device.pin_config(pin, mode == "input" ? 0 : mode == "output" ? 1 : 2);
    if (mode == "output") device.pin_set(pin, uint16_t(value_uint(v,"value")));
  } else if (name == "i2c") {
    fail_if(value_uint(v,"baud") != 100000, "I2C software clock is fixed; configurable baud is not implemented");
    if (value_text(v,"role") == "master") device.i2c_target_config(false, 0);
    else {
      fail_if(value_text(v,"profile") == "eeprom" && value_uint(v,"capacity") != 256,
              "large EEPROM requires a continuous PC page service; use legacy eeprom serve");
      device.i2c_target_config(true, uint8_t(value_uint(v,"address")));
      if (value_text(v,"profile") == "eeprom")
        device.i2c_eeprom_config(8, uint8_t(value_uint(v,"page_size")));
      device.i2c_target_profile(value_text(v,"profile") == "generic" ? 0 :
                                value_text(v,"profile") == "hdc1080" ? 1 : 2, 0, 0);
    }
  } else if (name == "i3c") {
    std::string role = value_text(v,"role");
    fail_if(role == "i2c-master" || role == "i2c-slave", "I3C pins do not currently implement I2C mode");
    device.i3c_config(role == "i3c-slave" ? 1 : 0);
  } else if (name == "spi") {
    device.spi_config(value_text(v,"role") == "slave" ? 1 : 0,
                      uint8_t(value_uint(v,"mode")), uint8_t(value_uint(v,"divider")));
  } else if (name == "qep") device.qep_start();
  else if (name == "can") {
    fail_if(value_text(v,"protocol") != "raw", "CANopen state machine is not implemented on device");
    device.can_config(value_uint(v,"baud"));
  } else {
    fail_if(value_text(v,"protocol") != "raw" && value_text(v,"protocol") != "register",
            "selected serial protocol is a host preset, not firmware bindable yet");
    uint8_t index = name == "uart" ? 0 : name == "rs232" ? 1 : 2;
    device.serial_config(index, value_text(v,"role") == "slave" ? 1 : 0, value_uint(v,"baud"));
  }
}
void list() {
  static const char *description[] = {
    "PA0/PA1 digital input, PA0 output, ADC input", "software I2C master/target, generic/HDC1080/EEPROM",
    "I3C controller/target; I2C mode planned", "SPI controller/target, modes 0..3",
    "quadrature counter input", "TTL UART stream/register responder",
    "RS232 stream/register responder", "RS485 half-duplex stream/register responder",
    "classic CAN raw frames; CANopen parser on host"};
  for (size_t i = 0; i < names.size(); ++i)
    std::cout << names[i] << " : " << description[i] << "\n";
}
std::vector<uint8_t> bytes(const std::string &hex, size_t max_count) {
  fail_if(hex.size() % 2 || hex.size() > max_count * 2, "hex payload has invalid length");
  std::vector<uint8_t> data;
  for (size_t i = 0; i < hex.size(); i += 2) {
    auto digit = [](char ch) -> unsigned {
      if (ch >= '0' && ch <= '9') return unsigned(ch - '0');
      if (ch >= 'a' && ch <= 'f') return unsigned(ch - 'a' + 10);
      if (ch >= 'A' && ch <= 'F') return unsigned(ch - 'A' + 10);
      throw std::runtime_error("invalid hex payload");
    };
    data.push_back(uint8_t((digit(hex[i]) << 4) | digit(hex[i+1])));
  }
  return data;
}
std::string hex_of(const uint8_t *data, size_t length) {
  std::ostringstream out;
  for (size_t i = 0; i < length; ++i)
    out << std::hex << std::setfill('0') << std::setw(2) << unsigned(data[i]);
  return out.str();
}
uint8_t serial_port(const std::string &name) {
  return name == "uart" ? 0 : name == "rs232" ? 1 : 2;
}
void tx(gmp::Device &device, const std::string &name, const json &packet, bool json_output, const json &config) {
  fail_if(!packet.is_object(), "packet must be a JSON object");
  fail_if(!packet.contains("data") || !packet["data"].is_string(), "packet.data must be a hex string");
  auto data = bytes(packet["data"].get<std::string>(), name == "can" ? 8 : 64);
  json answer = {{"interface",name},{"data",""}};
  if (name == "spi") {
    fail_if(data.empty(), "SPI requires at least one byte");
    auto rx = device.spi_transfer(data); answer["data"] = hex_of(rx.data(),rx.size());
  } else if (name == "i2c" || name == "i3c") {
    unsigned address = packet.value("address", value_uint(config,"address"));
    unsigned receive = packet.value("read", 0U);
    fail_if(address > 0x7F || receive > 64, "address or read length out of range");
    auto rx = name == "i2c" ? device.i2c_transfer(uint8_t(address), data, uint8_t(receive)) :
                              device.i3c_transfer(uint8_t(address), data, uint8_t(receive));
    answer["address"] = address; answer["data"] = hex_of(rx.data(),rx.size());
  } else if (name == "can") {
    unsigned identifier = packet.value("id", 0U);
    bool extended = packet.value("extended", false);
    fail_if(identifier > (extended ? 0x1FFFFFFFU : 0x7FFU), "CAN identifier out of range");
    device.can_send(identifier, extended, data);
    answer["id"] = identifier; answer["extended"] = extended;
  } else if (name == "uart" || name == "rs232" || name == "rs485") {
    fail_if(data.empty(), "serial TX requires at least one byte");
    device.serial_write(serial_port(name),data);
  } else throw std::runtime_error(name + " has no TX packet operation");
  if (json_output) std::cout << answer.dump() << '\n';
  else if (!answer["data"].get<std::string>().empty()) std::cout << answer["data"].get<std::string>() << '\n';
}
void rx(gmp::Device &device, const std::string &name, bool json_output) {
  json packet = nullptr;
  if (name == "can") {
    gmp::CanFrame frame;
    if (device.can_receive(frame))
      packet = {{"interface",name},{"id",frame.identifier},{"extended",frame.extended},
                {"data",hex_of(frame.data.data(),frame.data.size())}};
  } else if (name == "uart" || name == "rs232" || name == "rs485") {
    auto data = device.serial_read(serial_port(name));
    if (!data.empty()) packet = {{"interface",name},{"data",hex_of(data.data(),data.size())}};
  } else throw std::runtime_error(name + " RX queue is not exposed by current firmware");
  if (json_output) std::cout << packet.dump() << '\n';
  else if (packet.is_null()) std::cout << "empty\n";
  else if (name == "can") std::cout << "0x" << std::hex << packet["id"].get<unsigned>() << std::dec
                                      << " " << packet["data"].get<std::string>() << '\n';
  else std::cout << packet["data"].get<std::string>() << '\n';
}
int run(int argc, char **argv) {
  int i = 1;
  std::string port = port_name(argc, argv, i);
  fail_if(i >= argc, "expected list, [interface] config|bind|unbind|tx|rx|save|load");
  std::string name = argv[i++];
  if (name == "list") { fail_if(i != argc, "list takes no arguments"); list(); return 0; }
  bool all = name == "save" || name == "load";
  fail_if(!all && !known(name), "unknown interface: " + name);
  fail_if(i >= argc, "missing action");
  std::string action = all ? name : argv[i++];
  if (all) fail_if(i >= argc || std::string(argv[i++]) != "config", "use save config or load config");
  else if (action == "save" || action == "load")
    fail_if(i >= argc || std::string(argv[i++]) != "config", "use <interface> save config or load config");
  fail_if(port.empty(), "select a board using --port COMx or GMP_PORT");
  json state = state_load(port);
  if (action == "save") {
    fail_if(i != argc, "save config accepts no extra arguments; redirect stdout to file");
    json result = json::object();
    for (const auto &item : names) if (all || item == name) result[item] = config_of(state, item);
    std::cout << result.dump(2) << '\n'; return 0;
  }
  if (action == "load" || action == "config") {
    json incoming = json::object();
    bool input_provided = false;
    std::vector<std::string> arguments;
    for (; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg.rfind("-e:",0) == 0) { incoming = read_json_file(arg.substr(3)); input_provided = true; }
      else arguments.push_back(arg);
    }
    if (action == "load" && !input_provided) {
      if (!arguments.empty()) { incoming = read_json_file(arguments.front()); arguments.erase(arguments.begin()); }
      else incoming = parse_json(std::cin);
    }
    if (action == "load") fail_if(!arguments.empty(), "unexpected arguments after load config");
    json input_sections = sections(incoming);
    for (auto it = input_sections.begin(); it != input_sections.end(); ++it)
      fail_if(!known(it.key()), "unknown interface in JSON: " + it.key());
    json updated = state;
    for (const auto &item : names) {
      if (!all && item != name) continue;
      if (action == "load" && !input_sections.contains(item)) continue;
      json current = config_of(state, item);
      if (input_sections.contains(item)) merge(current, input_sections[item], item);
      if (action == "config") {
        for (const auto &argument : arguments) {
          size_t eq = argument.find('=');
          fail_if(eq == std::string::npos || !eq || eq + 1 == argument.size(), "use key=value: " + argument);
          current[argument.substr(0,eq)] = scalar(argument.substr(0,eq), argument.substr(eq+1), defaults(item));
        }
        validate(item, current);
      }
      fail_if(bound(state,item), item + " is bound with " + config_of(state,item).dump());
      updated[item] = {{"config",current},{"bound",false}};
      if (action == "config") std::cout << json({{item,current}}).dump(2) << '\n';
    }
    state_save(port, updated); return 0;
  }
  if (action == "bind") {
    fail_if(i != argc, "bind takes no arguments");
    fail_if(bound(state,name), name + " is already bound with " + config_of(state,name).dump());
    json current = config_of(state,name);
    gmp::Device device(port); apply(device,name,current);
    state[name] = {{"config",current},{"bound",true}}; state_save(port,state);
    std::cout << json({{name,current}}).dump(2) << '\n'; return 0;
  }
  if (action == "unbind") {
    fail_if(i != argc, "unbind takes no arguments");
    gmp::Device device(port);
    uint8_t interface_id = uint8_t(std::find(names.begin(), names.end(), name) - names.begin() + 1);
    int rc = gmp_interface_unbind(device.native_handle(), interface_id);
    fail_if(rc != GMP_OK, name + " deinitialization failed: " + gmp_error_string(rc));
    state.erase(name); state_save(port,state);
    std::cout << name << " unbound; defaults restored\n"; return 0;
  }
  if (action == "tx" || action == "rx") {
    fail_if(!bound(state,name), name + " is not bound; configure and bind first");
    bool json_output = false;
    std::string raw, output_file;
    json packet = json::object();
    for (; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--json") json_output = true;
      else if (arg.rfind("-e:",0) == 0) packet = read_json_file(arg.substr(3));
      else if (arg.rfind("-o:",0) == 0) output_file = arg.substr(3);
      else if (arg.rfind("--read=",0) == 0) packet["read"] = number(arg.substr(7));
      else if (arg.rfind("--id=",0) == 0) packet["id"] = number(arg.substr(5));
      else if (arg.rfind("--address=",0) == 0) packet["address"] = number(arg.substr(10));
      else if (arg == "--extended") packet["extended"] = true;
      else { fail_if(!raw.empty(), "only one raw payload is allowed"); raw = arg; }
    }
    if (action == "tx") {
      if (!raw.empty()) {
        if (json_output) packet = json::parse(raw);
        else packet["data"] = raw;
      }
      if (!packet.contains("data")) packet["data"] = "";
      gmp::Device device(port); tx(device,name,packet,json_output,config_of(state,name));
    } else {
      fail_if(!raw.empty(), "rx takes no raw payload");
      if (!output_file.empty()) {
        std::ofstream file(output_file);
        fail_if(!file, "cannot open RX output file");
        std::streambuf *old = std::cout.rdbuf(file.rdbuf());
        try { gmp::Device device(port); rx(device,name,true); }
        catch (...) { std::cout.rdbuf(old); throw; }
        std::cout.rdbuf(old);
      } else { gmp::Device device(port); rx(device,name,json_output); }
    }
    return 0;
  }
  throw std::runtime_error(action + " is not implemented in the standard CLI; legacy raw bus commands remain available");
}
} // namespace

int gmp_standard_cli(int argc, char **argv) {
  if (argc < 2) return -1;
  const std::string first = argv[1];
  if (first != "--port" && first != "list" && first != "save" && first != "load" && !known(first)) return -1;
  try { return run(argc,argv); }
  catch (const std::exception &error) { std::cerr << "Error: " << error.what() << '\n'; return 1; }
}
