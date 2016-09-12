#ifndef __INDUCE_MINUS_STAR_SUBSTRINGS_HPP_INCLUDED
#define __INDUCE_MINUS_STAR_SUBSTRINGS_HPP_INCLUDED

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <algorithm>

#include "utils.hpp"
#include "em_radix_heap.hpp"
#include "io/async_stream_reader.hpp"
#include "io/async_stream_writer.hpp"
#include "io/async_backward_stream_reader.hpp"


template<typename chr_t, typename saidx_t>
void induce_minus_star_substrings(const chr_t *text, std::uint64_t text_length,
    // std::uint64_t ram_use,
    std::string plus_substrings_filename,
    std::string minus_substrings_filename, std::uint64_t &total_io_volume,
    // temporary:
    std::uint64_t radix_heap_bufsize, std::uint64_t radix_log) {
//  fprintf(stderr, "Induce minus-star-substrings:\n");
//  long double start = utils::wclock();

#if 0
  // Decide on the radix and buffer size for the radix heap.
  std::uint64_t radix_log = 1;
  {
    std::uint64_t ram_left = ram_use - text_length * sizeof(chr_t);
    while (radix_log <= 16) {
      std::uint64_t depth = (8UL * sizeof(chr_t) + radix_log - 1) / radix_log;
      std::uint64_t radix = (1UL << radix_log);
      std::uint64_t n_queues = depth * (radix - 1) + 1;
      std::uint64_t ram_needed = n_queues * radix_heap_bufsize;
      if (ram_needed > ram_left) break;
      else ++radix_log;
    }

    if (radix_log > 1) --radix_log;
    else {
      // ram_use was very small. Use the smallest
      // possible radix and shrink the buffer size.
      std::uint64_t depth = (8UL * sizeof(chr_t) + radix_log - 1) / radix_log;
      std::uint64_t radix = (1UL << radix_log);
      std::uint64_t n_queues = depth * (radix - 1) + 1;
      radix_heap_bufsize = ram_left / n_queues;
    }
  }
#endif

  // Print decided values.
//  fprintf(stderr, "  Radix log = %lu\n", radix_log);
//  fprintf(stderr, "  Radix buffer size = %lu\n", radix_heap_bufsize);

  // Initialize radix heap.
  typedef em_radix_heap<chr_t, saidx_t> radix_heap_type;
  radix_heap_type *radix_heap = new radix_heap_type(radix_log,
      radix_heap_bufsize, minus_substrings_filename);

  // Initialize reading of sorted plus-substrings.
  typedef async_backward_stream_reader<saidx_t> plus_reader_type;
  plus_reader_type *plus_reader = new plus_reader_type(plus_substrings_filename);

  // Initialize writer of sorted minus-substrings.
  typedef async_stream_writer<saidx_t> minus_writer_type;
  minus_writer_type *minus_writer = new minus_writer_type(minus_substrings_filename);

  // Inducing of minus-substrings follows.
//  fprintf(stderr, "  Induce: ");
  bool is_next_plus_substring = false;
  chr_t next_plus_substring_bucket = 0;
  if (!plus_reader->empty()) {
    is_next_plus_substring = true;
    next_plus_substring_bucket = text[(std::uint64_t)plus_reader->peek()];
  }
  radix_heap->push(text[text_length - 1], (saidx_t)(text_length - 1));
  while (!radix_heap->empty() || is_next_plus_substring) {
    // Process minus-substrings.
    while (!radix_heap->empty() && (!is_next_plus_substring ||
          radix_heap->min_compare(next_plus_substring_bucket))) {
      std::pair<chr_t, saidx_t> p = radix_heap->extract_min();
      chr_t ch = p.first;
      saidx_t pos = p.second;
      std::uint64_t pos_uint64 = pos;
      if (pos_uint64 > 0) {
        if (text[pos_uint64 - 1] >= ch)
          radix_heap->push(text[pos_uint64 - 1], (saidx_t)(pos_uint64 - 1));
        else minus_writer->write(pos);
      }
    }

    // Process plus-substrings.
    if (is_next_plus_substring) {
      is_next_plus_substring = false;
      while (!plus_reader->empty()) {
        std::uint64_t pos = plus_reader->peek();
        chr_t ch = text[pos];
        if (ch == next_plus_substring_bucket) {
          plus_reader->read();
          if (pos > 0 && text[pos - 1] > ch)
            radix_heap->push(text[pos - 1], (saidx_t)(pos - 1));
        } else {
          is_next_plus_substring = true;
          next_plus_substring_bucket = ch;
          break;
        }
      }
    }
  }

  // Update I/O volume.
  std::uint64_t io_volume = plus_reader->bytes_read() +
    minus_writer->bytes_written() + radix_heap->io_volume();
  total_io_volume += io_volume;

  // Clean up.
  delete plus_reader;
  delete minus_writer;
  delete radix_heap;

  // Print summary.
//  long double total_time = utils::wclock() - start;
//  fprintf(stderr, "  time = %.2Lfs, I/O = %.2LfMiB/s, "
//      "total I/O vol = %.2Lfn\n", total_time,
//      (1.L * io_volume / (1L << 20)) / total_time,
//      (1.L * total_io_volume) / text_length);
}

#endif  // __INDUCE_MINUS_STAR_SUBSTRINGS_HPP_INCLUDED