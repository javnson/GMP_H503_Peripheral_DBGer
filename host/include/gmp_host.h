#ifndef GMP_HOST_H
#define GMP_HOST_H
#include <stdint.h>
#ifdef _WIN32
# ifdef GMP_HOST_STATIC
#  define GMP_API
# elif defined(GMP_HOST_BUILD)
#  define GMP_API __declspec(dllexport)
# else
#  define GMP_API __declspec(dllimport)
# endif
#else
# define GMP_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct gmp_device gmp_device;
typedef struct {
  uint8_t protocol_major, protocol_minor;
  uint32_t capabilities;
  char name[64];
} gmp_info;
typedef struct {
  uint8_t tpld_boot_status, tpld_address, image_present, autoload;
  uint32_t generation, image_crc32;
} gmp_status;
typedef struct {
  uint8_t spi_ready, spi_role;
  uint32_t spi_error;
  uint8_t i3c_ready, i3c_role;
  uint32_t i3c_error;
  uint8_t i2c_target_active, i2c_target_address, serial_ready_mask;
} gmp_interface_status;
enum { GMP_OK = 0, GMP_ERR_ARGUMENT = -1, GMP_ERR_IO = -2,
       GMP_ERR_TIMEOUT = -3, GMP_ERR_PROTOCOL = -4, GMP_ERR_DEVICE = -5 };
GMP_API int gmp_open(const char *port, gmp_device **out);
GMP_API void gmp_close(gmp_device *device);
GMP_API const char *gmp_error_string(int error);
GMP_API int gmp_info_get(gmp_device *device, gmp_info *out);
GMP_API int gmp_status_get(gmp_device *device, gmp_status *out);
GMP_API int gmp_interface_status_get(gmp_device *device, gmp_interface_status *out);
GMP_API int gmp_gpio_read(gmp_device *device, uint8_t port, uint8_t pin, uint8_t *value);
GMP_API int gmp_gpio_write(gmp_device *device, uint8_t port, uint8_t pin, uint8_t value);
enum { GMP_PIN_INPUT = 0, GMP_PIN_OUTPUT = 1, GMP_PIN_ADC = 2, GMP_PIN_DAC = 3 };
GMP_API int gmp_pin_config(gmp_device *device, uint8_t pin, uint8_t mode);
GMP_API int gmp_pin_read(gmp_device *device, uint8_t pin, uint8_t *mode, uint16_t *value);
GMP_API int gmp_pin_set(gmp_device *device, uint8_t pin, uint16_t value);
GMP_API int gmp_i2c_transfer(gmp_device *device, uint8_t address,
                              const uint8_t *tx, uint8_t tx_length,
                              uint8_t *rx, uint8_t rx_length);
GMP_API int gmp_i2c_target_config(gmp_device *device, uint8_t enable, uint8_t address);
GMP_API int gmp_i2c_target_profile(gmp_device *device, uint8_t profile,
                                    uint8_t reg, uint16_t value);
GMP_API int gmp_i2c_target_word(gmp_device *device, uint8_t reg, uint16_t *value);
GMP_API int gmp_can_config(gmp_device *device, uint32_t nominal_bitrate);
GMP_API int gmp_can_send(gmp_device *device, uint32_t identifier, uint8_t extended,
                         const uint8_t *data, uint8_t length);
/* A successful empty read sets present=0. */
GMP_API int gmp_can_receive(gmp_device *device, uint8_t *present,
                            uint32_t *identifier, uint8_t *extended,
                            uint8_t *data, uint8_t *length);
typedef struct {
  uint8_t ready, tx_free, rx_pending, bus_off, last_error, tx_error, rx_error;
} gmp_can_status;
GMP_API int gmp_can_status_get(gmp_device *device, gmp_can_status *out);
GMP_API int gmp_i3c_config(gmp_device *device, uint8_t role);
GMP_API int gmp_i3c_assign(gmp_device *device, uint8_t base_address,
                            uint8_t maximum, uint8_t *addresses, uint8_t *actual);
GMP_API int gmp_i3c_transfer(gmp_device *device, uint8_t address,
                              const uint8_t *tx, uint8_t tx_length,
                              uint8_t *rx, uint8_t rx_length);
GMP_API int gmp_spi_config(gmp_device *device, uint8_t role, uint8_t mode, uint8_t divider);
GMP_API int gmp_spi_transfer(gmp_device *device, const uint8_t *tx, uint8_t *rx, uint8_t length);
GMP_API int gmp_qep_start(gmp_device *device);
GMP_API int gmp_qep_read(gmp_device *device, int32_t *position, uint8_t *index_level, uint8_t *direction);
GMP_API int gmp_qep_reset(gmp_device *device);
/* Serial ports: 0=TTL UART, 1=RS232, 2=RS485. Role 0=stream, 1=register responder.
 * Register ports additionally include 3=SPI, 4=I3C, 5=I2C. */
GMP_API int gmp_serial_config(gmp_device *device, uint8_t port, uint8_t role, uint32_t baud);
GMP_API int gmp_serial_write(gmp_device *device, uint8_t port, const uint8_t *data, uint8_t length);
GMP_API int gmp_serial_read(gmp_device *device, uint8_t port, uint8_t *data, uint8_t capacity, uint8_t *actual);
GMP_API int gmp_register_write(gmp_device *device, uint8_t port, uint8_t address, const uint8_t *data, uint8_t length);
GMP_API int gmp_register_read(gmp_device *device, uint8_t port, uint8_t address, uint8_t *data, uint8_t length);
/* Cross-board register access: bus=0 UART, 1 RS232, 2 RS485, 3 SPI, 4 I3C,
 * 5 I2C. I3C uses ENTDAA dynamic address; I2C uses configured 7-bit address;
 * serial and SPI ignore target_address. Serial must be configured as stream. */
GMP_API int gmp_remote_register_read(gmp_device *device, uint8_t bus,
                                      uint8_t target_address, uint8_t reg,
                                      uint8_t *data, uint8_t length);
GMP_API int gmp_remote_register_write(gmp_device *device, uint8_t bus,
                                       uint8_t target_address, uint8_t reg,
                                       const uint8_t *data, uint8_t length);
GMP_API int gmp_tpld_upload(gmp_device *device, const uint8_t image[512],
                             uint8_t autoload);
GMP_API int gmp_reset(gmp_device *device);
/* Accepts TI InterConnect Studio or Intel HEX data records at 0x0200..0x03FF. */
GMP_API int gmp_load_tpld_hex(const char *path, uint8_t image[512]);
#ifdef __cplusplus
}
#include <stdexcept>
#include <string>
#include <vector>
namespace gmp {
struct QepState { int32_t position; bool index_level; bool decreasing; };
struct CanFrame { uint32_t identifier; bool extended; std::vector<uint8_t> data; };
class Device {
  gmp_device *handle_ = nullptr;
  static void check(int code) {
    if (code != GMP_OK) throw std::runtime_error(gmp_error_string(code));
  }
public:
  explicit Device(const std::string &port) { check(gmp_open(port.c_str(), &handle_)); }
  ~Device() { gmp_close(handle_); }
  Device(const Device &) = delete;
  Device &operator=(const Device &) = delete;
  Device(Device &&other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
  gmp_info info() const { gmp_info x{}; check(gmp_info_get(handle_, &x)); return x; }
  gmp_status status() const { gmp_status x{}; check(gmp_status_get(handle_, &x)); return x; }
  gmp_interface_status interface_status() const {
    gmp_interface_status x{}; check(gmp_interface_status_get(handle_, &x)); return x;
  }
  bool gpio(uint8_t port, uint8_t pin) const {
    uint8_t value = 0; check(gmp_gpio_read(handle_, port, pin, &value)); return value != 0;
  }
  void gpio(uint8_t port, uint8_t pin, bool value) const {
    check(gmp_gpio_write(handle_, port, pin, value ? 1 : 0));
  }
  void pin_config(uint8_t pin, uint8_t mode) const { check(gmp_pin_config(handle_, pin, mode)); }
  uint16_t pin_read(uint8_t pin, uint8_t &mode) const {
    uint16_t value = 0; check(gmp_pin_read(handle_, pin, &mode, &value)); return value;
  }
  void pin_set(uint8_t pin, uint16_t value) const { check(gmp_pin_set(handle_, pin, value)); }
  void spi_config(uint8_t role, uint8_t mode, uint8_t divider) const {
    check(gmp_spi_config(handle_, role, mode, divider));
  }
  std::vector<uint8_t> spi_transfer(const std::vector<uint8_t> &tx) const {
    if (tx.empty() || tx.size() > 64) check(GMP_ERR_ARGUMENT);
    std::vector<uint8_t> rx(tx.size());
    check(gmp_spi_transfer(handle_, tx.data(), rx.data(), uint8_t(tx.size())));
    return rx;
  }
  std::vector<uint8_t> i2c_transfer(uint8_t address,
      const std::vector<uint8_t> &tx, uint8_t receive) const {
    if (tx.size() > 64 || receive > 64) check(GMP_ERR_ARGUMENT);
    std::vector<uint8_t> rx(receive);
    check(gmp_i2c_transfer(handle_, address, tx.data(), uint8_t(tx.size()),
                           rx.data(), receive));
    return rx;
  }
  void i2c_target_config(bool enabled, uint8_t address) const {
    check(gmp_i2c_target_config(handle_, enabled ? 1 : 0, address));
  }
  void i2c_target_profile(uint8_t profile, uint8_t reg, uint16_t value) const {
    check(gmp_i2c_target_profile(handle_, profile, reg, value));
  }
  uint16_t i2c_target_word(uint8_t reg) const {
    uint16_t value = 0; check(gmp_i2c_target_word(handle_, reg, &value)); return value;
  }
  void can_config(uint32_t bitrate) const { check(gmp_can_config(handle_, bitrate)); }
  void can_send(uint32_t id, bool extended, const std::vector<uint8_t> &data) const {
    check(gmp_can_send(handle_, id, extended ? 1 : 0, data.data(), uint8_t(data.size())));
  }
  bool can_receive(CanFrame &frame) const {
    uint8_t present = 0, extended = 0, length = 0;
    uint32_t identifier = 0; uint8_t data[8] = {};
    check(gmp_can_receive(handle_, &present, &identifier, &extended, data, &length));
    if (!present) return false;
    frame = {identifier, extended != 0, std::vector<uint8_t>(data, data + length)};
    return true;
  }
  gmp_can_status can_status() const {
    gmp_can_status state{}; check(gmp_can_status_get(handle_, &state)); return state;
  }
  void i3c_config(uint8_t role) const { check(gmp_i3c_config(handle_, role)); }
  std::vector<uint8_t> i3c_assign(uint8_t base, uint8_t maximum) const {
    std::vector<uint8_t> addresses(maximum); uint8_t actual = 0;
    check(gmp_i3c_assign(handle_, base, maximum, addresses.data(), &actual));
    addresses.resize(actual); return addresses;
  }
  std::vector<uint8_t> i3c_transfer(uint8_t address,
      const std::vector<uint8_t> &tx, uint8_t receive) const {
    if (tx.size() > 64 || receive > 64) check(GMP_ERR_ARGUMENT);
    std::vector<uint8_t> rx(receive);
    check(gmp_i3c_transfer(handle_, address, tx.data(), uint8_t(tx.size()),
                           rx.data(), receive));
    return rx;
  }
  void qep_start() const { check(gmp_qep_start(handle_)); }
  QepState qep_read() const {
    int32_t position = 0; uint8_t index = 0, direction = 0;
    check(gmp_qep_read(handle_, &position, &index, &direction));
    return {position, index != 0, direction != 0};
  }
  void qep_reset() const { check(gmp_qep_reset(handle_)); }
  void serial_config(uint8_t port, uint8_t role, uint32_t baud) const {
    check(gmp_serial_config(handle_, port, role, baud));
  }
  void serial_write(uint8_t port, const std::vector<uint8_t> &data) const {
    if (data.empty() || data.size() > 64) check(GMP_ERR_ARGUMENT);
    check(gmp_serial_write(handle_, port, data.data(), uint8_t(data.size())));
  }
  std::vector<uint8_t> serial_read(uint8_t port, uint8_t capacity = 64) const {
    std::vector<uint8_t> data(capacity); uint8_t actual = 0;
    check(gmp_serial_read(handle_, port, data.data(), capacity, &actual));
    data.resize(actual); return data;
  }
  void register_write(uint8_t port, uint8_t address,
                      const std::vector<uint8_t> &data) const {
    if (data.empty() || data.size() > 32) check(GMP_ERR_ARGUMENT);
    check(gmp_register_write(handle_, port, address, data.data(), uint8_t(data.size())));
  }
  std::vector<uint8_t> register_read(uint8_t port, uint8_t address,
                                      uint8_t length) const {
    std::vector<uint8_t> data(length);
    check(gmp_register_read(handle_, port, address, data.data(), length));
    return data;
  }
  void remote_register_write(uint8_t bus, uint8_t target, uint8_t address,
                             const std::vector<uint8_t> &data) const {
    if (data.empty() || data.size() > 32) check(GMP_ERR_ARGUMENT);
    check(gmp_remote_register_write(handle_, bus, target, address,
                                    data.data(), uint8_t(data.size())));
  }
  std::vector<uint8_t> remote_register_read(uint8_t bus, uint8_t target,
                                             uint8_t address, uint8_t length) const {
    std::vector<uint8_t> data(length);
    check(gmp_remote_register_read(handle_, bus, target, address,
                                   data.data(), length));
    return data;
  }
  void upload(const uint8_t image[512], bool autoload = true) const {
    check(gmp_tpld_upload(handle_, image, autoload ? 1 : 0));
  }
  void reset() const { check(gmp_reset(handle_)); }
  gmp_device *native_handle() const { return handle_; }
};
}
#endif
#endif
