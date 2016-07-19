#ifndef __SIMPLE_ACCESSOR_HPP_INCLUDED
#define __SIMPLE_ACCESSOR_HPP_INCLUDED

#include <cstdio>
#include <algorithm>

#include "../utils.hpp"


template<typename ValueType>
class simple_accessor {
  public:
    typedef ValueType value_type;

  private:
    std::uint64_t m_bytes_read;
    std::uint64_t m_file_items;
    std::uint64_t m_items_per_buf;
    std::uint64_t m_buf_pos;
    std::uint64_t m_buf_filled;

    value_type *m_buf;
    std::FILE *m_file;

  public:
    simple_accessor(std::string filename, std::uint64_t bufsize = (1UL << 20)) {
      m_items_per_buf = std::max(2UL, bufsize / sizeof(value_type));
      m_file_items = utils::file_size(filename) / sizeof(value_type);
      m_file = utils::file_open(filename, "r");

      m_buf = new value_type[m_items_per_buf];
      m_buf_filled = 0;
      m_buf_pos = 0;
      m_bytes_read = 0;
    }

    inline std::uint8_t access(std::uint64_t i) {
      if (!(m_buf_pos <= i && i < m_buf_pos + m_buf_filled)) {
        if (i >= m_items_per_buf / 2) m_buf_pos = i - m_items_per_buf / 2;
        else m_buf_pos = 0;
        m_buf_filled = std::min(m_file_items - m_buf_pos, m_items_per_buf);
        std::fseek(m_file, m_buf_pos * sizeof(value_type), SEEK_SET);
        utils::read_from_file(m_buf, m_buf_filled, m_file);
        m_bytes_read += m_buf_filled * sizeof(value_type);
      }

      return m_buf[i - m_buf_pos];
    }

    inline std::uint64_t bytes_read() const {
      return m_bytes_read;
    }

    ~simple_accessor() {
      std::fclose(m_file);
      delete[] m_buf;
    }
};

#endif  // __SIMPLE_ACCESSOR_HPP_INCLUDED
