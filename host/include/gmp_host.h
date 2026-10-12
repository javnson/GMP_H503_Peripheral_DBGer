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
enum { GMP_OK = 0, GMP_ERR_ARGUMENT = -1, GMP_ERR_IO = -2,
       GMP_ERR_TIMEOUT = -3, GMP_ERR_PROTOCOL = -4, GMP_ERR_DEVICE = -5 };
GMP_API int gmp_open(const char *port, gmp_device **out);
GMP_API void gmp_close(gmp_device *device);
GMP_API const char *gmp_error_string(int error);
GMP_API int gmp_info_get(gmp_device *device, gmp_info *out);
GMP_API int gmp_status_get(gmp_device *device, gmp_status *out);
GMP_API int gmp_gpio_read(gmp_device *device, uint8_t port, uint8_t pin, uint8_t *value);
GMP_API int gmp_gpio_write(gmp_device *device, uint8_t port, uint8_t pin, uint8_t value);
GMP_API int gmp_i2c_transfer(gmp_device *device, uint8_t address,
                              const uint8_t *tx, uint8_t tx_length,
                              uint8_t *rx, uint8_t rx_length);
GMP_API int gmp_tpld_upload(gmp_device *device, const uint8_t image[512],
                             uint8_t autoload);
GMP_API int gmp_reset(gmp_device *device);
/* Accepts TI InterConnect Studio or Intel HEX data records at 0x0200..0x03FF. */
GMP_API int gmp_load_tpld_hex(const char *path, uint8_t image[512]);
#ifdef __cplusplus
}
#include <stdexcept>
#include <string>
namespace gmp {
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
  bool gpio(uint8_t port, uint8_t pin) const {
    uint8_t value = 0; check(gmp_gpio_read(handle_, port, pin, &value)); return value != 0;
  }
  void gpio(uint8_t port, uint8_t pin, bool value) const {
    check(gmp_gpio_write(handle_, port, pin, value ? 1 : 0));
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
