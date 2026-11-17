#pragma once

#include "types.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <type_traits>
#include <vector>

class StateWriter {
public:
  template <typename T> void write(T value) {
    static_assert(std::is_integral_v<T>, "write() takes integral types only");
    using U = std::make_unsigned_t<T>;
    const U u = static_cast<U>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
      m_data.push_back(static_cast<u8>((u >> (8 * i)) & 0xFF));
    }
  }

  void write_bool(bool value) { write<u8>(value ? 1 : 0); }

  void write_float(float value) {
    u32 bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    write<u32>(bits);
  }

  void write_bytes(const u8 *ptr, std::size_t count) {
    m_data.insert(m_data.end(), ptr, ptr + count);
  }

  template <std::size_t N> void write_array(const std::array<u8, N> &arr) {
    write_bytes(arr.data(), N);
  }

  void write_u8_vector(const std::vector<u8> &vec) {
    write<u64>(vec.size());
    write_bytes(vec.data(), vec.size());
  }

  const std::vector<u8> &data() const { return m_data; }

private:
  std::vector<u8> m_data;
};

class StateReader {
public:
  explicit StateReader(std::vector<u8> data) : m_data{std::move(data)} {}

  template <typename T> T read() {
    static_assert(std::is_integral_v<T>, "read() takes integral types only");
    require(sizeof(T));
    using U = std::make_unsigned_t<T>;
    U u = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
      u |= static_cast<U>(m_data[m_pos + i]) << (8 * i);
    }
    m_pos += sizeof(T);
    return static_cast<T>(u);
  }

  bool read_bool() { return read<u8>() != 0; }

  float read_float() {
    const u32 bits = read<u32>();
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  void read_bytes(u8 *ptr, std::size_t count) {
    require(count);
    std::memcpy(ptr, m_data.data() + m_pos, count);
    m_pos += count;
  }

  template <std::size_t N> void read_array(std::array<u8, N> &arr) {
    read_bytes(arr.data(), N);
  }

  std::vector<u8> read_u8_vector() {
    const u64 size = read<u64>();
    if (size > (16u * 1024u * 1024u)) { // sanity: no DMG state needs >16MB
      throw std::runtime_error{"state: implausible vector size"};
    }
    std::vector<u8> vec(static_cast<std::size_t>(size));
    read_bytes(vec.data(), vec.size());
    return vec;
  }

  bool at_end() const { return m_pos == m_data.size(); }

private:
  void require(std::size_t count) const {
    if (m_pos + count > m_data.size()) {
      throw std::runtime_error{"state: truncated data"};
    }
  }

  std::vector<u8> m_data;
  std::size_t m_pos = 0;
};


inline void save_state_to_file(const std::string &path, const StateWriter &w) {
  std::ofstream file{path, std::ios::binary | std::ios::trunc};
  if (!file) {
    throw std::runtime_error{"state: cannot open file for writing"};
  }
  file.write(reinterpret_cast<const char *>(w.data().data()),
             static_cast<std::streamsize>(w.data().size()));
}

inline StateReader load_state_from_file(const std::string &path) {
  std::ifstream file{path, std::ios::binary | std::ios::ate};
  if (!file) {
    throw std::runtime_error{"state: cannot open file for reading"};
  }
  const auto size = file.tellg();
  file.seekg(0);
  std::vector<u8> data(static_cast<std::size_t>(size));
  file.read(reinterpret_cast<char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  return StateReader{std::move(data)};
}
