#include "gmp_host.h"
#include "gmp_standard_cli.h"
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

static void usage() {
  std::cerr << "GMP Peripheral Host CLI\n"
            << "Usage: gmpctl list\n"
            << "       gmpctl --port <port> <interface> config [key=value ...] [-e:file.json]\n"
            << "       gmpctl --port <port> <interface> bind|unbind|tx|rx\n"
            << "       gmpctl --port <port> [<interface>] save|load config\n"
            << "Legacy: gmpctl <port> info|status|interfaces|metrics|reset\n"
            << "       gmpctl <port> gpio get <PA0|PA1|PA4|PA5>\n"
            << "       gmpctl <port> gpio set PA0 <0|1>\n"
            << "       gmpctl <port> pin <PA0|PA1> read|mode <input|output|adc>|set <0|1>\n"
            << "       gmpctl <port> i2c <7-bit-address> [write-hex] [read-count]\n"
            << "       gmpctl <port> i2c-target <address>|off\n"
            << "       gmpctl <port> i2c-profile generic|hdc1080|eeprom <reg> <word>\n"
            << "       gmpctl <port> eeprom status|read <address> <count>|write <address> <hex>\n"
            << "       gmpctl <port> eeprom load <256-byte.bin>|serve <16/32/64-KiB.bin>\n"
            << "       gmpctl <port> can config <125000|250000|500000|1000000>\n"
            << "       gmpctl <port> can send <id> <hex-data> [extended]\n"
            << "       gmpctl <port> can recv\n"
            << "       gmpctl <port> can status\n"
            << "       gmpctl <port> i3c controller|target|daa <base> <maximum>\n"
            << "       gmpctl <port> i3c transfer <dynamic-address> [write-hex] [read-count]\n"
            << "       gmpctl <port> spi config <mode:0..3> <divider:0..7>\n"
            << "       gmpctl <port> spi target <mode:0..3>\n"
            << "       gmpctl <port> spi transfer <hex>\n"
            << "       gmpctl <port> qep start|read|reset\n"
            << "       gmpctl <port> uart|rs232|rs485 config <stream|slave> <baud>\n"
            << "       gmpctl <port> uart|rs232|rs485 send <hex>|recv [count]\n"
            << "       gmpctl <port> uart|rs232|rs485 reg-get <addr> <count>|reg-set <addr> <hex>\n"
            << "       gmpctl <port> reg <uart|rs232|rs485|spi|i3c|i2c> get <addr> <count>\n"
            << "       gmpctl <port> reg <uart|rs232|rs485|spi|i3c|i2c> set <addr> <hex>\n"
            << "       gmpctl <port> remote <uart|rs232|rs485|spi|i3c|i2c> <target-address> get <reg> <count>\n"
            << "       gmpctl <port> remote <uart|rs232|rs485|spi|i3c|i2c> <target-address> set <reg> <hex>\n"
            << "       gmpctl <port> tpld upload <program.hex> [--no-autoload] [--reset]\n";
}
static unsigned number(const char *s) {
  char *end = nullptr;
  unsigned long x = std::strtoul(s, &end, 0);
  if (!s[0] || !end || *end) throw std::runtime_error("invalid number");
  return unsigned(x);
}
static uint8_t nibble(char c) {
  if (c >= '0' && c <= '9') return uint8_t(c - '0');
  if (c >= 'a' && c <= 'f') return uint8_t(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return uint8_t(c - 'A' + 10);
  throw std::runtime_error("invalid hex data");
}
static std::vector<uint8_t> hex_data(const char *s, size_t limit) {
  std::string hex(s); if (hex.empty() || hex.size() % 2 || hex.size() > 2 * limit)
    throw std::runtime_error("invalid hex data length");
  std::vector<uint8_t> data(hex.size() / 2);
  for (size_t i = 0; i < data.size(); ++i)
    data[i] = uint8_t((nibble(hex[2*i]) << 4) | nibble(hex[2*i+1]));
  return data;
}
static void print_hex(const uint8_t *data, size_t size) {
  static const char *digits = "0123456789abcdef";
  for (size_t i = 0; i < size; ++i)
    std::cout << digits[data[i] >> 4] << digits[data[i] & 15];
  std::cout << "\n";
}
static void check(int rc) { if (rc) throw std::runtime_error(gmp_error_string(rc)); }
int main(int argc, char **argv) {
  int standard = gmp_standard_cli(argc, argv);
  if (standard >= 0) return standard;
  if (argc < 3) { usage(); return 2; }
  try {
    gmp::Device device(argv[1]);
    std::string command = argv[2];
    if (command == "info" && argc == 3) {
      auto x = device.info();
      std::cout << x.name << " protocol " << unsigned(x.protocol_major) << "."
                << unsigned(x.protocol_minor) << " capabilities 0x" << std::hex
                << x.capabilities << std::dec << "\n";
    } else if (command == "status" && argc == 3) {
      auto x = device.status();
      std::cout << "TPLD boot status: " << unsigned(x.tpld_boot_status)
                << ", address: 0x" << std::hex << unsigned(x.tpld_address)
                << ", image CRC32: 0x" << x.image_crc32 << std::dec
                << ", present: " << unsigned(x.image_present)
                << ", autoload: " << unsigned(x.autoload)
                << ", generation: " << x.generation << "\n";
    } else if (command == "interfaces" && argc == 3) {
      auto x = device.interface_status();
      std::cout << "SPI ready=" << unsigned(x.spi_ready) << " role=" << unsigned(x.spi_role)
                << " error=0x" << std::hex << x.spi_error << std::dec << "\n"
                << "I3C ready=" << unsigned(x.i3c_ready) << " role=" << unsigned(x.i3c_role)
                << " error=0x" << std::hex << x.i3c_error << std::dec << "\n"
                << "I2C target=" << unsigned(x.i2c_target_active)
                << " address=0x" << std::hex << unsigned(x.i2c_target_address)
                << " serial-ready-mask=0x" << unsigned(x.serial_ready_mask)
                << std::dec << "\n";
    } else if (command == "metrics" && argc == 3) {
      auto x = device.metrics();
      std::cout << "RAM static=" << x.static_bytes
                << " allocated=" << x.allocated_bytes
                << " available=" << x.available_bytes
                << " stack-reserved=" << x.stack_reserve_bytes
                << " CPU=";
      if (x.cpu_percent == 255) std::cout << "sampling";
      else std::cout << unsigned(x.cpu_percent) << "%";
      std::cout << "\n";
    } else if (command == "reset" && argc == 3) {
      device.reset(); std::cout << "Reset requested\n";
    } else if (command == "pin" && argc >= 5) {
      std::string label(argv[3]);
      if (label != "PA0" && label != "PA1") throw std::runtime_error("use PA0 or PA1");
      uint8_t pin = uint8_t(label[2] - '0');
      std::string operation(argv[4]);
      if (operation == "read" && argc == 5) {
        uint8_t mode = 0; uint16_t value = 0;
        check(gmp_pin_read(device.native_handle(), pin, &mode, &value));
        std::cout << "mode=" << unsigned(mode) << " value=" << value << "\n";
      } else if (operation == "mode" && argc == 6) {
        std::string setting(argv[5]);
        uint8_t mode = setting == "input" ? 0 : setting == "output" ? 1 :
                       setting == "adc" ? 2 : 255;
        if (mode == 255) throw std::runtime_error("invalid pin mode");
        device.pin_config(pin, mode);
      } else if (operation == "set" && argc == 6) {
        unsigned value = number(argv[5]);
        if (value > 1) throw std::runtime_error("GPIO level must be 0 or 1");
        device.pin_set(pin, uint16_t(value));
      } else { usage(); return 2; }
    } else if (command == "gpio" && argc >= 5) {
      std::string pin = argv[4];
      if (pin.size() != 3 || pin.substr(0, 2) != "PA") throw std::runtime_error("use PA0/PA1/PA4/PA5");
      uint8_t bit = uint8_t(number(pin.c_str() + 2));
      if (std::strcmp(argv[3], "get") == 0 && argc == 5)
        std::cout << unsigned(device.gpio(0, bit)) << "\n";
      else if (std::strcmp(argv[3], "set") == 0 && argc == 6) {
        if (bit != 0) throw std::runtime_error("only PA0 is writable; TPLD drives PA1/PA4/PA5");
        unsigned value = number(argv[5]);
        if (value > 1) throw std::runtime_error("GPIO value must be 0 or 1");
        device.gpio(0, bit, value != 0);
      } else { usage(); return 2; }
    } else if (command == "i2c" && argc >= 4 && argc <= 6) {
      unsigned address = number(argv[3]);
      if (address > 0x7f) throw std::runtime_error("I2C address out of range");
      std::string hex = argc >= 5 ? argv[4] : "";
      if (hex.size() % 2 || hex.size() > 128) throw std::runtime_error("invalid I2C write length");
      std::array<uint8_t, 64> tx{}, rx{};
      for (size_t i = 0; i < hex.size() / 2; ++i)
        tx[i] = uint8_t((nibble(hex[2*i]) << 4) | nibble(hex[2*i+1]));
      unsigned count = argc == 6 ? number(argv[5]) : 0;
      if (count > 64) throw std::runtime_error("I2C read length exceeds 64");
      int rc = gmp_i2c_transfer(device.native_handle(), uint8_t(address),
                                tx.data(), uint8_t(hex.size()/2), rx.data(), uint8_t(count));
      if (rc) throw std::runtime_error(gmp_error_string(rc));
      for (unsigned i = 0; i < count; ++i) {
        static const char *digits = "0123456789abcdef";
        std::cout << digits[rx[i] >> 4] << digits[rx[i] & 15];
      }
      std::cout << "\n";
    } else if (command == "i2c-target" && argc == 4) {
      if (std::strcmp(argv[3], "off") == 0)
        check(gmp_i2c_target_config(device.native_handle(), 0, 0));
      else {
        unsigned address = number(argv[3]);
        if (address > 0x77) throw std::runtime_error("I2C target address out of range");
        check(gmp_i2c_target_config(device.native_handle(), 1, uint8_t(address)));
      }
    } else if (command == "i2c-profile" && argc == 6) {
      std::string profile(argv[3]);
      uint8_t selected = profile == "hdc1080" ? 1 : profile == "eeprom" ? 2 :
                         profile == "generic" ? 0 : 255;
      if (selected == 255) throw std::runtime_error("unknown I2C profile");
      unsigned reg = number(argv[4]), value = number(argv[5]);
      if (reg > 255 || value > 0xFFFF) throw std::runtime_error("I2C profile value out of range");
      check(gmp_i2c_target_profile(device.native_handle(), selected,
                                   uint8_t(reg), uint16_t(value)));
    } else if (command == "eeprom" && argc >= 4) {
      std::string operation(argv[3]);
      if (operation == "status" && argc == 4) {
        uint8_t busy = 0, error = 0;
        check(gmp_i2c_eeprom_status(device.native_handle(), &busy, &error));
        std::cout << "busy=" << unsigned(busy) << " error=" << unsigned(error) << "\n";
      } else if (operation == "read" && argc == 6) {
        unsigned address = number(argv[4]), count = number(argv[5]);
        if (address > 255 || !count || count > 64 || address + count > 256)
          throw std::runtime_error("EEPROM read range out of bounds");
        std::array<uint8_t, 64> data{};
        check(gmp_i2c_eeprom_read(device.native_handle(), uint8_t(address),
                                  data.data(), uint8_t(count)));
        print_hex(data.data(), count);
      } else if (operation == "write" && argc == 6) {
        unsigned address = number(argv[4]);
        auto data = hex_data(argv[5], 32);
        if (address > 255 || address + data.size() > 256)
          throw std::runtime_error("EEPROM write range out of bounds");
        check(gmp_i2c_eeprom_write(device.native_handle(), uint8_t(address),
                                   data.data(), uint8_t(data.size())));
      } else if (operation == "load" && argc == 5) {
        std::ifstream file(argv[4], std::ios::binary | std::ios::ate);
        if (!file || file.tellg() != std::streampos(256))
          throw std::runtime_error("EEPROM image must be exactly 256 bytes");
        std::array<uint8_t, 256> image{};
        file.seekg(0); file.read(reinterpret_cast<char *>(image.data()), image.size());
        if (!file) throw std::runtime_error("cannot read EEPROM image");
        check(gmp_i2c_target_config(device.native_handle(), 1, 0x50));
        check(gmp_i2c_eeprom_config(device.native_handle(), 8, 8));
        check(gmp_i2c_target_profile(device.native_handle(), 2, 0, 0));
        for (unsigned offset = 0; offset < 256; offset += 32)
          check(gmp_i2c_eeprom_write(device.native_handle(), uint8_t(offset),
                                     image.data() + offset, 32));
        std::cout << "256-byte image loaded into STM32 RAM\n";
      } else if (operation == "serve" && argc == 5) {
        std::fstream file(argv[4], std::ios::in | std::ios::out | std::ios::binary);
        if (!file) throw std::runtime_error("cannot open EEPROM image for read/write");
        file.seekg(0, std::ios::end);
        const auto capacity = file.tellg();
        uint8_t bits = 0, page_size = 0;
        if (capacity == std::streampos(16384)) { bits = 14; page_size = 64; }
        else if (capacity == std::streampos(32768)) { bits = 15; page_size = 64; }
        else if (capacity == std::streampos(65536)) { bits = 16; page_size = 128; }
        else throw std::runtime_error("image must be exactly 16, 32, or 64 KiB");
        check(gmp_i2c_target_config(device.native_handle(), 1, 0x50));
        check(gmp_i2c_eeprom_config(device.native_handle(), bits, page_size));
        check(gmp_i2c_target_profile(device.native_handle(), 2, 0, 0));
        std::cout << "Serving EEPROM image at I2C 0x50; keep this process running (Ctrl+C to stop)\n";
        for (;;) {
          gmp_eeprom_event event{};
          check(gmp_i2c_eeprom_event(device.native_handle(), &event));
          if (!event.kind) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
          }
          if ((unsigned(event.page) + 1U) * event.page_size > unsigned(capacity))
            throw std::runtime_error("device requested page outside image");
          std::array<uint8_t, 128> data{};
          file.clear(); file.seekg(event.page * event.page_size);
          file.read(reinterpret_cast<char *>(data.data()), event.page_size);
          if (!file) throw std::runtime_error("cannot read EEPROM page");
          if (event.kind == 1) {
            check(gmp_i2c_eeprom_reply(device.native_handle(), &event, data.data()));
          } else if (event.kind == 2) {
            for (unsigned i = 0; i < event.page_size; ++i)
              if (event.mask[i / 8] & (1U << (i % 8))) data[i] = event.data[i];
            file.clear(); file.seekp(event.page * event.page_size);
            file.write(reinterpret_cast<const char *>(data.data()), event.page_size);
            file.flush();
            if (!file) throw std::runtime_error("cannot persist EEPROM page");
            check(gmp_i2c_eeprom_reply(device.native_handle(), &event, nullptr));
          } else throw std::runtime_error("unknown EEPROM service event");
        }
      } else { usage(); return 2; }
    } else if (command == "can" && argc >= 4) {
      std::string operation(argv[3]);
      if (operation == "config" && argc == 5) {
        check(gmp_can_config(device.native_handle(), number(argv[4])));
      } else if (operation == "send" && (argc == 6 || argc == 7)) {
        unsigned id = number(argv[4]);
        auto data = hex_data(argv[5], 8);
        uint8_t extended = argc == 7 && std::strcmp(argv[6], "extended") == 0;
        if (argc == 7 && !extended) throw std::runtime_error("use extended for 29-bit ID");
        check(gmp_can_send(device.native_handle(), id, extended,
                           data.data(), uint8_t(data.size())));
      } else if (operation == "recv" && argc == 4) {
        uint8_t present = 0, extended = 0, length = 0;
        uint32_t id = 0; std::array<uint8_t,8> data{};
        check(gmp_can_receive(device.native_handle(), &present, &id,
                              &extended, data.data(), &length));
        if (!present) std::cout << "empty\n";
        else {
          std::cout << "0x" << std::hex << id << std::dec
                    << (extended ? " extended " : " standard ");
          print_hex(data.data(), length);
        }
      } else if (operation == "status" && argc == 4) {
        gmp_can_status s{};
        check(gmp_can_status_get(device.native_handle(), &s));
        std::cout << "ready=" << unsigned(s.ready) << " tx-free=" << unsigned(s.tx_free)
                  << " rx-pending=" << unsigned(s.rx_pending)
                  << " bus-off=" << unsigned(s.bus_off)
                  << " last-error=" << unsigned(s.last_error)
                  << " tx-error=" << unsigned(s.tx_error)
                  << " rx-error=" << unsigned(s.rx_error) << "\n";
      } else { usage(); return 2; }
    } else if (command == "i3c" && argc >= 4) {
      std::string operation(argv[3]);
      if (operation == "controller" && argc == 4)
        check(gmp_i3c_config(device.native_handle(), 0));
      else if (operation == "target" && argc == 4)
        check(gmp_i3c_config(device.native_handle(), 1));
      else if (operation == "daa" && argc == 6) {
        unsigned base = number(argv[4]), maximum = number(argv[5]);
        if (base > 0x77 || maximum < 1 || maximum > 8)
          throw std::runtime_error("invalid I3C DAA range");
        std::array<uint8_t,8> addresses{}; uint8_t actual = 0;
        check(gmp_i3c_assign(device.native_handle(), uint8_t(base), uint8_t(maximum),
                             addresses.data(), &actual));
        for (unsigned i = 0; i < actual; ++i)
          std::cout << "0x" << std::hex << unsigned(addresses[i]) << std::dec << " ";
        std::cout << "\n";
      } else if (operation == "transfer" && argc >= 5 && argc <= 7) {
        unsigned address = number(argv[4]);
        if (address > 0x77) throw std::runtime_error("invalid I3C address");
        std::vector<uint8_t> tx = argc >= 6 && argv[5][0] ? hex_data(argv[5], 64) :
                                  std::vector<uint8_t>();
        unsigned count = argc == 7 ? number(argv[6]) : 0;
        if (count > 64) throw std::runtime_error("I3C read count exceeds 64");
        std::array<uint8_t,64> rx{};
        check(gmp_i3c_transfer(device.native_handle(), uint8_t(address),
                               tx.data(), uint8_t(tx.size()), rx.data(), uint8_t(count)));
        print_hex(rx.data(), count);
      } else { usage(); return 2; }
    } else if (command == "spi" && argc >= 4) {
      std::string operation(argv[3]);
      if (operation == "config" && argc == 6) {
        unsigned mode = number(argv[4]), divider = number(argv[5]);
        if (mode > 3 || divider > 7) throw std::runtime_error("invalid SPI setting");
        check(gmp_spi_config(device.native_handle(), 0, uint8_t(mode), uint8_t(divider)));
      } else if (operation == "target" && argc == 5) {
        unsigned mode = number(argv[4]);
        if (mode > 3) throw std::runtime_error("invalid SPI mode");
        check(gmp_spi_config(device.native_handle(), 1, uint8_t(mode), 0));
      } else if (operation == "transfer" && argc == 5) {
        auto tx = hex_data(argv[4], 64); std::vector<uint8_t> rx(tx.size());
        check(gmp_spi_transfer(device.native_handle(), tx.data(), rx.data(), uint8_t(tx.size())));
        print_hex(rx.data(), rx.size());
      } else { usage(); return 2; }
    } else if (command == "reg" && argc == 7) {
      std::string name(argv[3]), operation(argv[4]);
      const char *names[] = {"uart", "rs232", "rs485", "spi", "i3c", "i2c"};
      uint8_t port = 255;
      for (uint8_t i = 0; i < 6; ++i) if (name == names[i]) port = i;
      if (port == 255) throw std::runtime_error("unknown register port");
      unsigned address = number(argv[5]);
      if (address > 255) throw std::runtime_error("register address out of range");
      if (operation == "set") {
        auto data = hex_data(argv[6], 32);
        check(gmp_register_write(device.native_handle(), port, uint8_t(address),
                                 data.data(), uint8_t(data.size())));
      } else if (operation == "get") {
        unsigned count = number(argv[6]);
        if (count < 1 || count > 32) throw std::runtime_error("invalid register count");
        std::array<uint8_t,32> data{};
        check(gmp_register_read(device.native_handle(), port, uint8_t(address),
                                data.data(), uint8_t(count)));
        print_hex(data.data(), count);
      } else { usage(); return 2; }
    } else if (command == "remote" && argc == 8) {
      std::string bus_name(argv[3]), operation(argv[5]);
      uint8_t bus = bus_name == "uart" ? 0 : bus_name == "rs232" ? 1 :
                    bus_name == "rs485" ? 2 : bus_name == "spi" ? 3 :
                    bus_name == "i3c" ? 4 : bus_name == "i2c" ? 5 : 255;
      if (bus == 255) throw std::runtime_error("unknown remote bus");
      unsigned target = number(argv[4]), address = number(argv[6]);
      if (target > 0x77 || address > 255) throw std::runtime_error("remote address out of range");
      if (operation == "set") {
        auto data = hex_data(argv[7], 32);
        check(gmp_remote_register_write(device.native_handle(), bus, uint8_t(target),
                                        uint8_t(address), data.data(), uint8_t(data.size())));
      } else if (operation == "get") {
        unsigned count = number(argv[7]);
        if (count < 1 || count > 32) throw std::runtime_error("invalid remote read count");
        std::array<uint8_t,32> data{};
        check(gmp_remote_register_read(device.native_handle(), bus, uint8_t(target),
                                       uint8_t(address), data.data(), uint8_t(count)));
        print_hex(data.data(), count);
      } else { usage(); return 2; }
    } else if (command == "qep" && argc == 4) {
      std::string operation(argv[3]);
      if (operation == "start") check(gmp_qep_start(device.native_handle()));
      else if (operation == "reset") check(gmp_qep_reset(device.native_handle()));
      else if (operation == "read") {
        int32_t position; uint8_t index, direction;
        check(gmp_qep_read(device.native_handle(), &position, &index, &direction));
        std::cout << "position=" << position << " index=" << unsigned(index)
                  << " direction=" << unsigned(direction) << "\n";
      } else { usage(); return 2; }
    } else if (command == "uart" || command == "rs232" || command == "rs485") {
      if (argc < 4) { usage(); return 2; }
      uint8_t port = command == "uart" ? 0 : command == "rs232" ? 1 : 2;
      std::string operation(argv[3]);
      if (operation == "config" && argc == 6) {
        std::string role(argv[4]);
        if (role != "stream" && role != "slave") throw std::runtime_error("invalid serial role");
        check(gmp_serial_config(device.native_handle(), port, uint8_t(role == "slave"), number(argv[5])));
      } else if (operation == "send" && argc == 5) {
        auto data = hex_data(argv[4], 64);
        check(gmp_serial_write(device.native_handle(), port, data.data(), uint8_t(data.size())));
      } else if (operation == "recv" && (argc == 4 || argc == 5)) {
        unsigned count = argc == 5 ? number(argv[4]) : 64;
        if (count > 64) throw std::runtime_error("read count exceeds 64");
        std::array<uint8_t,64> data{}; uint8_t actual = 0;
        check(gmp_serial_read(device.native_handle(), port, data.data(), uint8_t(count), &actual));
        print_hex(data.data(), actual);
      } else if (operation == "reg-set" && argc == 6) {
        unsigned address = number(argv[4]); auto data = hex_data(argv[5], 32);
        if (address > 255) throw std::runtime_error("register address out of range");
        check(gmp_register_write(device.native_handle(), port, uint8_t(address), data.data(), uint8_t(data.size())));
      } else if (operation == "reg-get" && argc == 6) {
        unsigned address = number(argv[4]), count = number(argv[5]);
        if (address > 255 || count < 1 || count > 32) throw std::runtime_error("invalid register span");
        std::array<uint8_t,32> data{};
        check(gmp_register_read(device.native_handle(), port, uint8_t(address), data.data(), uint8_t(count)));
        print_hex(data.data(), count);
      } else { usage(); return 2; }
    } else if (command == "tpld" && argc >= 5 && std::strcmp(argv[3], "upload") == 0) {
      bool autoload = true, reset = false;
      for (int i = 5; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-autoload") == 0) autoload = false;
        else if (std::strcmp(argv[i], "--reset") == 0) reset = true;
        else { usage(); return 2; }
      }
      std::array<uint8_t, 512> image{};
      int rc = gmp_load_tpld_hex(argv[4], image.data());
      if (rc) throw std::runtime_error(gmp_error_string(rc));
      device.upload(image.data(), autoload);
      std::cout << "Uploaded and verified 512-byte TPLD image\n";
      if (reset) { device.reset(); std::cout << "Reset requested\n"; }
    } else { usage(); return 2; }
  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << "\n"; return 1;
  }
  return 0;
}
