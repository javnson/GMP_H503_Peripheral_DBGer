#include "gmp_host.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

static void usage() {
  std::cerr << "GMP Peripheral ISO Host CLI\n"
            << "Usage: gmpctl <port> info|status|reset\n"
            << "       gmpctl <port> gpio get <PA0|PA1|PA4|PA5>\n"
            << "       gmpctl <port> gpio set PA0 <0|1>\n"
            << "       gmpctl <port> i2c <7-bit-address> [write-hex] [read-count]\n"
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
int main(int argc, char **argv) {
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
    } else if (command == "reset" && argc == 3) {
      device.reset(); std::cout << "Reset requested\n";
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
