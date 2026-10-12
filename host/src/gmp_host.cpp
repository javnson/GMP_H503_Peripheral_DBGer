#include "gmp_host.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# define NOMINMAX
# include <windows.h>
#else
# include <cerrno>
# include <fcntl.h>
# include <sys/select.h>
# include <termios.h>
# include <unistd.h>
#endif

struct gmp_device {
#ifdef _WIN32
  HANDLE port = INVALID_HANDLE_VALUE;
#else
  int port = -1;
#endif
  uint8_t sequence = 0;
};
static uint16_t crc16(const uint8_t *p, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; ++i) {
    c ^= uint16_t(p[i]) << 8;
    for (int b = 0; b < 8; ++b) c = uint16_t((c << 1) ^ ((c & 0x8000) ? 0x1021 : 0));
  }
  return c;
}
static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int b = 0; b < 8; ++b) c = (c >> 1) ^ ((c & 1u) ? 0xEDB88320u : 0u);
  }
  return ~c;
}
static uint8_t crc8(const uint8_t *p, size_t n) {
  uint8_t c = 0;
  for (size_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int b = 0; b < 8; ++b) c = uint8_t((c << 1) ^ ((c & 0x80) ? 7 : 0));
  }
  return c;
}
static uint32_t le32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
static bool write_all(gmp_device *d, const uint8_t *p, size_t n) {
  while (n) {
#ifdef _WIN32
    DWORD done = 0;
    if (!WriteFile(d->port, p, DWORD(n), &done, nullptr) || !done) return false;
#else
    ssize_t done = write(d->port, p, n);
    if (done <= 0) return false;
#endif
    p += done; n -= size_t(done);
  }
  return true;
}
static int read_one(gmp_device *d, uint8_t &out, int timeout_ms) {
#ifdef _WIN32
  COMMTIMEOUTS timeout{};
  timeout.ReadIntervalTimeout = MAXDWORD;
  timeout.ReadTotalTimeoutConstant = DWORD(timeout_ms);
  SetCommTimeouts(d->port, &timeout);
  DWORD done = 0;
  if (!ReadFile(d->port, &out, 1, &done, nullptr)) return GMP_ERR_IO;
  return done ? GMP_OK : GMP_ERR_TIMEOUT;
#else
  fd_set fds; FD_ZERO(&fds); FD_SET(d->port, &fds);
  timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
  int ready = select(d->port + 1, &fds, nullptr, nullptr, &tv);
  if (ready < 0) return GMP_ERR_IO;
  if (!ready) return GMP_ERR_TIMEOUT;
  return read(d->port, &out, 1) == 1 ? GMP_OK : GMP_ERR_IO;
#endif
}
static int transact(gmp_device *d, uint8_t command,
                    const uint8_t *request, size_t request_size,
                    std::vector<uint8_t> &answer, int timeout_ms = 1500) {
  if (!d || request_size > 256) return GMP_ERR_ARGUMENT;
  std::vector<uint8_t> packet{0xA5, 0x5A, 1, command, ++d->sequence,
                              uint8_t(request_size), uint8_t(request_size >> 8)};
  if (request_size) packet.insert(packet.end(), request, request + request_size);
  uint16_t check = crc16(packet.data(), packet.size());
  packet.push_back(uint8_t(check)); packet.push_back(uint8_t(check >> 8));
#ifdef _WIN32
  PurgeComm(d->port, PURGE_RXCLEAR);
#else
  tcflush(d->port, TCIFLUSH);
#endif
  if (!write_all(d, packet.data(), packet.size())) return GMP_ERR_IO;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  std::vector<uint8_t> frame;
  while (std::chrono::steady_clock::now() < deadline) {
    uint8_t byte = 0;
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline - std::chrono::steady_clock::now()).count();
    int rc = read_one(d, byte, int(std::max<int64_t>(1, left)));
    if (rc == GMP_ERR_TIMEOUT) continue;
    if (rc != GMP_OK) return rc;
    if (frame.empty() && byte != 0xA5) continue;
    if (frame.size() == 1 && byte != 0x5A) { frame.clear(); continue; }
    frame.push_back(byte);
    if (frame.size() == 7) {
      size_t length = size_t(frame[5]) | (size_t(frame[6]) << 8);
      if (frame[2] != 1 || frame[3] != (command | 0x80) ||
          frame[4] != d->sequence || length > 257 || length == 0) return GMP_ERR_PROTOCOL;
    }
    if (frame.size() >= 9 && frame.size() == 9 + (size_t(frame[5]) | (size_t(frame[6]) << 8))) {
      uint16_t actual = crc16(frame.data(), frame.size() - 2);
      uint16_t received = uint16_t(frame[frame.size()-2]) |
                          (uint16_t(frame.back()) << 8);
      if (actual != received) return GMP_ERR_PROTOCOL;
      if (frame[7] != 0) return GMP_ERR_DEVICE;
      answer.assign(frame.begin() + 8, frame.end() - 2);
      return GMP_OK;
    }
  }
  return GMP_ERR_TIMEOUT;
}

extern "C" {
int gmp_open(const char *port, gmp_device **out) {
  if (!port || !out) return GMP_ERR_ARGUMENT;
  *out = nullptr;
  auto d = new gmp_device;
#ifdef _WIN32
  std::string path = "\\\\.\\" + std::string(port);
  d->port = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                         nullptr, OPEN_EXISTING, 0, nullptr);
  if (d->port == INVALID_HANDLE_VALUE) { delete d; return GMP_ERR_IO; }
  DCB config{}; config.DCBlength = sizeof(config);
  if (!GetCommState(d->port, &config)) { gmp_close(d); return GMP_ERR_IO; }
  config.BaudRate = CBR_115200; config.ByteSize = 8;
  config.Parity = NOPARITY; config.StopBits = ONESTOPBIT;
  config.fDtrControl = DTR_CONTROL_ENABLE;
  if (!SetCommState(d->port, &config)) { gmp_close(d); return GMP_ERR_IO; }
#else
  d->port = open(port, O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (d->port < 0) { delete d; return GMP_ERR_IO; }
  termios config{};
  if (tcgetattr(d->port, &config) != 0) { gmp_close(d); return GMP_ERR_IO; }
  cfmakeraw(&config); cfsetispeed(&config, B115200); cfsetospeed(&config, B115200);
  config.c_cflag |= CLOCAL | CREAD;
  if (tcsetattr(d->port, TCSANOW, &config) != 0) { gmp_close(d); return GMP_ERR_IO; }
#endif
  *out = d; return GMP_OK;
}
void gmp_close(gmp_device *d) {
  if (!d) return;
#ifdef _WIN32
  if (d->port != INVALID_HANDLE_VALUE) CloseHandle(d->port);
#else
  if (d->port >= 0) close(d->port);
#endif
  delete d;
}
const char *gmp_error_string(int rc) {
  switch (rc) {
    case GMP_OK: return "success";
    case GMP_ERR_ARGUMENT: return "invalid argument";
    case GMP_ERR_IO: return "serial I/O failed";
    case GMP_ERR_TIMEOUT: return "device timeout";
    case GMP_ERR_PROTOCOL: return "protocol mismatch or bad CRC";
    case GMP_ERR_DEVICE: return "device rejected command";
    default: return "unknown error";
  }
}
int gmp_info_get(gmp_device *d, gmp_info *out) {
  if (!out) return GMP_ERR_ARGUMENT;
  std::vector<uint8_t> result;
  int rc = transact(d, 1, nullptr, 0, result);
  if (rc) return rc;
  if (result.size() < 6) return GMP_ERR_PROTOCOL;
  out->protocol_major = result[0]; out->protocol_minor = result[1];
  out->capabilities = le32(result.data() + 2);
  size_t n = std::min(result.size() - 6, sizeof(out->name) - 1);
  memcpy(out->name, result.data() + 6, n); out->name[n] = 0;
  return GMP_OK;
}
int gmp_status_get(gmp_device *d, gmp_status *out) {
  if (!out) return GMP_ERR_ARGUMENT;
  std::vector<uint8_t> result;
  int rc = transact(d, 2, nullptr, 0, result);
  if (rc) return rc;
  if (result.size() != 12) return GMP_ERR_PROTOCOL;
  out->tpld_boot_status = result[0]; out->tpld_address = result[1];
  out->image_present = result[2]; out->autoload = result[3];
  out->generation = le32(result.data() + 4);
  out->image_crc32 = le32(result.data() + 8);
  return GMP_OK;
}
int gmp_interface_status_get(gmp_device *d, gmp_interface_status *out) {
  if (!out) return GMP_ERR_ARGUMENT;
  std::vector<uint8_t> result;
  int rc = transact(d, 0x70, nullptr, 0, result);
  if (rc) return rc;
  if (result.size() != 15) return GMP_ERR_PROTOCOL;
  out->spi_ready = result[0]; out->spi_role = result[1];
  out->spi_error = le32(result.data() + 2);
  out->i3c_ready = result[6]; out->i3c_role = result[7];
  out->i3c_error = le32(result.data() + 8);
  out->i2c_target_active = result[12];
  out->i2c_target_address = result[13];
  out->serial_ready_mask = result[14];
  return GMP_OK;
}
int gmp_gpio_read(gmp_device *d, uint8_t port, uint8_t pin, uint8_t *value) {
  if (!value) return GMP_ERR_ARGUMENT;
  uint8_t p[] = {port, pin}; std::vector<uint8_t> result;
  int rc = transact(d, 3, p, sizeof(p), result);
  if (rc) return rc;
  if (result.size() != 1) return GMP_ERR_PROTOCOL;
  *value = result[0]; return GMP_OK;
}
int gmp_gpio_write(gmp_device *d, uint8_t port, uint8_t pin, uint8_t value) {
  uint8_t p[] = {port, pin, value}; std::vector<uint8_t> result;
  return transact(d, 4, p, sizeof(p), result);
}
int gmp_pin_config(gmp_device *d, uint8_t pin, uint8_t mode) {
  if (pin > 5 || mode > 3) return GMP_ERR_ARGUMENT;
  uint8_t p[] = {0, pin, mode}; std::vector<uint8_t> result;
  return transact(d, 5, p, sizeof(p), result);
}
int gmp_pin_read(gmp_device *d, uint8_t pin, uint8_t *mode, uint16_t *value) {
  if (pin > 5 || !mode || !value) return GMP_ERR_ARGUMENT;
  uint8_t p[] = {0, pin}; std::vector<uint8_t> result;
  int rc = transact(d, 6, p, sizeof(p), result);
  if (rc) return rc;
  if (result.size() != 3) return GMP_ERR_PROTOCOL;
  *mode = result[0]; *value = uint16_t(result[1]) | (uint16_t(result[2]) << 8);
  return GMP_OK;
}
int gmp_pin_set(gmp_device *d, uint8_t pin, uint16_t value) {
  if (pin > 5) return GMP_ERR_ARGUMENT;
  uint8_t p[] = {0, pin, uint8_t(value), uint8_t(value >> 8)};
  std::vector<uint8_t> result; return transact(d, 7, p, sizeof(p), result);
}
int gmp_i2c_transfer(gmp_device *d, uint8_t address, const uint8_t *tx,
                     uint8_t tx_length, uint8_t *rx, uint8_t rx_length) {
  if (tx_length > 64 || rx_length > 64 || (tx_length && !tx) || (rx_length && !rx))
    return GMP_ERR_ARGUMENT;
  std::vector<uint8_t> p{address, tx_length, rx_length};
  if (tx_length) p.insert(p.end(), tx, tx + tx_length);
  std::vector<uint8_t> result;
  int rc = transact(d, 0x20, p.data(), p.size(), result);
  if (rc) return rc;
  if (result.size() != rx_length) return GMP_ERR_PROTOCOL;
  if (rx_length) memcpy(rx, result.data(), rx_length);
  return GMP_OK;
}
int gmp_i3c_config(gmp_device *d, uint8_t role) {
  if (role > 1) return GMP_ERR_ARGUMENT;
  std::vector<uint8_t> result; return transact(d, 0x60, &role, 1, result);
}
int gmp_i2c_target_config(gmp_device *d, uint8_t enable, uint8_t address) {
  if (enable > 1 || (enable && (address < 8 || address > 0x77)))
    return GMP_ERR_ARGUMENT;
  uint8_t p[] = {enable, address}; std::vector<uint8_t> result;
  return transact(d, 0x21, p, sizeof(p), result);
}
int gmp_i2c_target_profile(gmp_device *d, uint8_t profile, uint8_t reg,
                           uint16_t value) {
  if (profile > 1 || (profile && reg != 0 && reg != 1 && reg != 2))
    return GMP_ERR_ARGUMENT;
  uint8_t p[] = {profile, reg, uint8_t(value >> 8), uint8_t(value)};
  std::vector<uint8_t> result;
  return transact(d, 0x22, p, sizeof(p), result);
}
int gmp_i2c_target_word(gmp_device *d, uint8_t reg, uint16_t *value) {
  if (!value) return GMP_ERR_ARGUMENT;
  std::vector<uint8_t> result;
  int rc = transact(d, 0x23, &reg, 1, result);
  if (rc) return rc;
  if (result.size() != 2) return GMP_ERR_PROTOCOL;
  *value = uint16_t(result[0] << 8) | result[1];
  return GMP_OK;
}
int gmp_can_config(gmp_device *d, uint32_t bitrate) {
  if (bitrate != 125000 && bitrate != 250000 && bitrate != 500000 && bitrate != 1000000)
    return GMP_ERR_ARGUMENT;
  uint8_t p[] = {uint8_t(bitrate), uint8_t(bitrate >> 8),
                 uint8_t(bitrate >> 16), uint8_t(bitrate >> 24)};
  std::vector<uint8_t> result; return transact(d, 0x71, p, sizeof(p), result);
}
int gmp_can_send(gmp_device *d, uint32_t id, uint8_t extended,
                 const uint8_t *data, uint8_t length) {
  if (extended > 1 || id > (extended ? 0x1FFFFFFF : 0x7FF) ||
      length > 8 || (length && !data)) return GMP_ERR_ARGUMENT;
  std::vector<uint8_t> p{uint8_t(id), uint8_t(id >> 8), uint8_t(id >> 16),
                         uint8_t(id >> 24), extended, length};
  if (length) p.insert(p.end(), data, data + length);
  std::vector<uint8_t> result; return transact(d, 0x72, p.data(), p.size(), result);
}
int gmp_can_receive(gmp_device *d, uint8_t *present, uint32_t *id,
                    uint8_t *extended, uint8_t *data, uint8_t *length) {
  if (!present || !id || !extended || !data || !length) return GMP_ERR_ARGUMENT;
  std::vector<uint8_t> result;
  int rc = transact(d, 0x73, nullptr, 0, result);
  if (rc) return rc;
  if (result.size() == 1 && result[0] == 0) { *present = 0; *length = 0; return GMP_OK; }
  if (result.size() < 7 || result[0] != 1 || result[5] > 1 ||
      result[6] > 8 || result.size() != size_t(7 + result[6])) return GMP_ERR_PROTOCOL;
  *present = 1; *id = le32(result.data() + 1); *extended = result[5];
  *length = result[6]; memcpy(data, result.data() + 7, *length);
  return GMP_OK;
}
int gmp_can_status_get(gmp_device *d, gmp_can_status *out) {
  if (!out) return GMP_ERR_ARGUMENT;
  std::vector<uint8_t> result;
  int rc = transact(d, 0x74, nullptr, 0, result);
  if (rc) return rc;
  if (result.size() != 7) return GMP_ERR_PROTOCOL;
  memcpy(out, result.data(), 7);
  return GMP_OK;
}
int gmp_i3c_assign(gmp_device *d, uint8_t base_address, uint8_t maximum,
                   uint8_t *addresses, uint8_t *actual) {
  if (!addresses || !actual || base_address < 8 || maximum < 1 || maximum > 8 ||
      unsigned(base_address) + maximum > 0x78) return GMP_ERR_ARGUMENT;
  uint8_t p[] = {base_address, maximum}; std::vector<uint8_t> result;
  int rc = transact(d, 0x61, p, sizeof(p), result);
  if (rc) return rc;
  if (result.empty() || result[0] > maximum || result.size() != size_t(result[0]) + 1)
    return GMP_ERR_PROTOCOL;
  *actual = result[0];
  if (*actual) memcpy(addresses, result.data() + 1, *actual);
  return GMP_OK;
}
int gmp_i3c_transfer(gmp_device *d, uint8_t address, const uint8_t *tx,
                     uint8_t tx_length, uint8_t *rx, uint8_t rx_length) {
  if (address < 8 || address > 0x77 || tx_length > 64 || rx_length > 64 ||
      (!tx_length && !rx_length) || (tx_length && !tx) || (rx_length && !rx))
    return GMP_ERR_ARGUMENT;
  std::vector<uint8_t> p{address, tx_length, rx_length};
  if (tx_length) p.insert(p.end(), tx, tx + tx_length);
  std::vector<uint8_t> result;
  int rc = transact(d, 0x62, p.data(), p.size(), result);
  if (rc) return rc;
  if (result.size() != rx_length) return GMP_ERR_PROTOCOL;
  if (rx_length) memcpy(rx, result.data(), rx_length);
  return GMP_OK;
}
int gmp_spi_config(gmp_device *d, uint8_t role, uint8_t mode, uint8_t divider) {
  if (role > 1 || mode > 3 || divider > 7) return GMP_ERR_ARGUMENT;
  uint8_t p[] = {role, mode, divider}; std::vector<uint8_t> result;
  return transact(d, 0x30, p, sizeof(p), result);
}
int gmp_spi_transfer(gmp_device *d, const uint8_t *tx, uint8_t *rx, uint8_t length) {
  if (!tx || !rx || !length || length > 64) return GMP_ERR_ARGUMENT;
  std::vector<uint8_t> result;
  int rc = transact(d, 0x31, tx, length, result);
  if (rc) return rc;
  if (result.size() != length) return GMP_ERR_PROTOCOL;
  memcpy(rx, result.data(), length); return GMP_OK;
}
int gmp_qep_start(gmp_device *d) {
  std::vector<uint8_t> result; return transact(d, 0x40, nullptr, 0, result);
}
int gmp_qep_read(gmp_device *d, int32_t *position, uint8_t *index_level, uint8_t *direction) {
  if (!position || !index_level || !direction) return GMP_ERR_ARGUMENT;
  std::vector<uint8_t> result;
  int rc = transact(d, 0x41, nullptr, 0, result);
  if (rc) return rc;
  if (result.size() != 6) return GMP_ERR_PROTOCOL;
  *position = int32_t(le32(result.data()));
  *index_level = result[4]; *direction = result[5]; return GMP_OK;
}
int gmp_qep_reset(gmp_device *d) {
  std::vector<uint8_t> result; return transact(d, 0x42, nullptr, 0, result);
}
int gmp_serial_config(gmp_device *d, uint8_t port, uint8_t role, uint32_t baud) {
  if (port > 2 || role > 1 || baud < 1200 || baud > 2000000) return GMP_ERR_ARGUMENT;
  uint8_t p[] = {port, role, uint8_t(baud), uint8_t(baud >> 8),
                 uint8_t(baud >> 16), uint8_t(baud >> 24)};
  std::vector<uint8_t> result; return transact(d, 0x50, p, sizeof(p), result);
}
int gmp_serial_write(gmp_device *d, uint8_t port, const uint8_t *data, uint8_t length) {
  if (port > 2 || !data || !length || length > 64) return GMP_ERR_ARGUMENT;
  std::vector<uint8_t> p{port}; p.insert(p.end(), data, data + length);
  std::vector<uint8_t> result; return transact(d, 0x51, p.data(), p.size(), result);
}
int gmp_serial_read(gmp_device *d, uint8_t port, uint8_t *data, uint8_t capacity, uint8_t *actual) {
  if (port > 2 || !actual || capacity > 64 || (capacity && !data)) return GMP_ERR_ARGUMENT;
  uint8_t p[] = {port, capacity}; std::vector<uint8_t> result;
  int rc = transact(d, 0x52, p, sizeof(p), result);
  if (rc) return rc;
  if (result.size() > capacity) return GMP_ERR_PROTOCOL;
  *actual = uint8_t(result.size());
  if (*actual) memcpy(data, result.data(), *actual);
  return GMP_OK;
}
int gmp_register_write(gmp_device *d, uint8_t port, uint8_t address,
                       const uint8_t *data, uint8_t length) {
  if (port > 5 || !data || !length || length > 32 || unsigned(address) + length > 256)
    return GMP_ERR_ARGUMENT;
  std::vector<uint8_t> p{port, address}; p.insert(p.end(), data, data + length);
  std::vector<uint8_t> result; return transact(d, 0x53, p.data(), p.size(), result);
}
int gmp_register_read(gmp_device *d, uint8_t port, uint8_t address,
                      uint8_t *data, uint8_t length) {
  if (port > 5 || !data || !length || length > 32 || unsigned(address) + length > 256)
    return GMP_ERR_ARGUMENT;
  uint8_t p[] = {port, address, length}; std::vector<uint8_t> result;
  int rc = transact(d, 0x54, p, sizeof(p), result);
  if (rc) return rc;
  if (result.size() != length) return GMP_ERR_PROTOCOL;
  memcpy(data, result.data(), length); return GMP_OK;
}
static int remote_register(gmp_device *d, uint8_t bus, uint8_t target_address,
                           uint8_t reg, const uint8_t *tx, uint8_t *rx,
                           uint8_t length) {
  if ((bus != 3 && bus != 4) || !length || length > 32 ||
      unsigned(reg) + length > 256 || (!tx && !rx)) return GMP_ERR_ARGUMENT;
  std::array<uint8_t, 37> request{}, response{};
  request[0] = 0xA5; request[1] = tx ? 1 : 0;
  request[2] = reg; request[3] = length;
  size_t frame = tx ? size_t(length) + 4 : 4;
  if (tx) memcpy(request.data() + 4, tx, length);
  request[frame] = crc8(request.data(), frame);
  int rc;
  if (bus == 3) {
    std::array<uint8_t, 37> discarded{}, dummy{};
    rc = gmp_spi_transfer(d, request.data(), discarded.data(), 37);
    if (rc) return rc;
    /* Allow the target's EOT interrupt to prepare the next full-duplex reply. */
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    rc = gmp_spi_transfer(d, dummy.data(), response.data(), 37);
  } else {
    rc = gmp_i3c_transfer(d, target_address, request.data(), 37,
                          response.data(), 37);
  }
  if (rc) return rc;
  uint8_t returned = response[2];
  if (response[0] != 0x5A || returned > 32 || (rx && returned != length) ||
      (!rx && returned != 0) ||
      response[size_t(returned) + 3] != crc8(response.data(), size_t(returned) + 3))
    return GMP_ERR_PROTOCOL;
  if (response[1] != 0) return GMP_ERR_DEVICE;
  if (rx) memcpy(rx, response.data() + 3, length);
  return GMP_OK;
}
static int serial_remote_register(gmp_device *d, uint8_t port, uint8_t reg,
                                  const uint8_t *tx, uint8_t *rx, uint8_t length) {
  uint8_t request[37] = {0xA5, uint8_t(tx ? 1 : 0), reg, length};
  size_t payload_size = tx ? size_t(length) : 0;
  if (tx) memcpy(request + 4, tx, length);
  request[4 + payload_size] = crc8(request, 4 + payload_size);
  /* A register transaction owns the transparent RX queue. */
  for (int drain = 0; drain < 3; ++drain) {
    uint8_t old[64], actual = 0;
    int rc = gmp_serial_read(d, port, old, 64, &actual);
    if (rc) return rc;
    if (!actual) break;
  }
  int rc = gmp_serial_write(d, port, request, uint8_t(5 + payload_size));
  if (rc) return rc;
  std::vector<uint8_t> frame;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
  while (std::chrono::steady_clock::now() < deadline) {
    uint8_t chunk[64], actual = 0;
    rc = gmp_serial_read(d, port, chunk, 64, &actual);
    if (rc) return rc;
    frame.insert(frame.end(), chunk, chunk + actual);
    while (!frame.empty() && frame[0] != 0x5A) frame.erase(frame.begin());
    if (frame.size() >= 3) {
      uint8_t returned = frame[2];
      if (returned > 32) { frame.erase(frame.begin()); continue; }
      size_t expected = size_t(returned) + 4;
      if (frame.size() >= expected) {
        if (frame[expected - 1] != crc8(frame.data(), expected - 1))
          return GMP_ERR_PROTOCOL;
        if (frame[1] != 0) return GMP_ERR_DEVICE;
        if (returned != (rx ? length : 0)) return GMP_ERR_PROTOCOL;
        if (rx) memcpy(rx, frame.data() + 3, length);
        return GMP_OK;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return GMP_ERR_TIMEOUT;
}
int gmp_remote_register_read(gmp_device *d, uint8_t bus, uint8_t target_address,
                             uint8_t reg, uint8_t *data, uint8_t length) {
  if (!data || !length || length > 32 || unsigned(reg) + length > 256)
    return GMP_ERR_ARGUMENT;
  if (bus <= 2)
    return serial_remote_register(d, bus, reg, nullptr, data, length);
  if (bus == 5)
    return gmp_i2c_transfer(d, target_address, &reg, 1, data, length);
  return remote_register(d, bus, target_address, reg, nullptr, data, length);
}
int gmp_remote_register_write(gmp_device *d, uint8_t bus, uint8_t target_address,
                              uint8_t reg, const uint8_t *data, uint8_t length) {
  if (!data || !length || length > 32 || unsigned(reg) + length > 256)
    return GMP_ERR_ARGUMENT;
  if (bus <= 2)
    return serial_remote_register(d, bus, reg, data, nullptr, length);
  if (bus == 5) {
    uint8_t payload[33]; payload[0] = reg;
    memcpy(payload + 1, data, length);
    return gmp_i2c_transfer(d, target_address, payload, uint8_t(length + 1),
                            nullptr, 0);
  }
  return remote_register(d, bus, target_address, reg, data, nullptr, length);
}
int gmp_tpld_upload(gmp_device *d, const uint8_t image[512], uint8_t autoload) {
  if (!d || !image || autoload > 1) return GMP_ERR_ARGUMENT;
  std::vector<uint8_t> result;
  int rc = transact(d, 0x10, &autoload, 1, result);
  if (rc) return rc;
  for (size_t offset = 0; offset < 512; offset += 128) {
    uint8_t p[130]; p[0] = uint8_t(offset); p[1] = uint8_t(offset >> 8);
    memcpy(p + 2, image + offset, 128);
    rc = transact(d, 0x11, p, sizeof(p), result);
    if (rc) return rc;
  }
  uint32_t c = crc32(image, 512);
  uint8_t p[] = {uint8_t(c), uint8_t(c >> 8), uint8_t(c >> 16), uint8_t(c >> 24)};
  rc = transact(d, 0x12, p, sizeof(p), result, 8000);
  if (rc) return rc;
  gmp_status status{};
  rc = gmp_status_get(d, &status);
  if (rc) return rc;
  return status.image_present && status.image_crc32 == c &&
         status.autoload == autoload ? GMP_OK : GMP_ERR_PROTOCOL;
}
int gmp_reset(gmp_device *d) {
  std::vector<uint8_t> result;
  return transact(d, 0x13, nullptr, 0, result);
}
static int hexbyte(char a, char b) {
  auto digit = [](char c)->int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  int x = digit(a), y = digit(b);
  return x < 0 || y < 0 ? -1 : x * 16 + y;
}
int gmp_load_tpld_hex(const char *path, uint8_t image[512]) {
  if (!path || !image) return GMP_ERR_ARGUMENT;
  std::ifstream in(path); if (!in) return GMP_ERR_IO;
  std::array<uint8_t, 512> seen{};
  std::string line; bool eof = false;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
    if (line.empty()) continue;
    if (line[0] != ':' || (line.size() - 1) % 2) return GMP_ERR_PROTOCOL;
    std::vector<uint8_t> record;
    for (size_t i = 1; i < line.size(); i += 2) {
      int x = hexbyte(line[i], line[i + 1]);
      if (x < 0) return GMP_ERR_PROTOCOL;
      record.push_back(uint8_t(x));
    }
    if (record.size() < 5 || record.size() != size_t(record[0]) + 5) return GMP_ERR_PROTOCOL;
    unsigned sum = 0; for (uint8_t b : record) sum += b;
    if ((sum & 0xFFu) != 0 && (sum & 0xFFu) != record[1]) return GMP_ERR_PROTOCOL;
    unsigned address = unsigned(record[1]) * 256 + record[2];
    if (record[3] == 1 && record[0] == 0) { eof = true; break; }
    if (record[3] != 0 || address < 0x200 || address + record[0] > 0x400)
      return GMP_ERR_PROTOCOL;
    for (unsigned i = 0; i < record[0]; ++i) {
      unsigned at = address + i - 0x200;
      if (seen[at]) return GMP_ERR_PROTOCOL;
      seen[at] = 1; image[at] = record[4 + i];
    }
  }
  return eof && std::all_of(seen.begin(), seen.end(), [](uint8_t x){ return x == 1; })
         ? GMP_OK : GMP_ERR_PROTOCOL;
}
}
