#ifndef __EM_INDUCE_PLUS_STAR_SUFFIXES_HPP_INCLUDED
#define __EM_INDUCE_PLUS_STAR_SUFFIXES_HPP_INCLUDED

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>

#include "utils.hpp"
#include "em_radix_heap.hpp"
#include "io/async_stream_writer.hpp"
#include "io/async_backward_stream_reader.hpp"
#include "io/async_backward_multi_stream_reader.hpp"
#include "io/async_backward_multi_bit_stream_reader.hpp"
#include "io/async_bit_stream_writer.hpp"


template<typename char_type,
  typename text_offset_type,
  typename block_offset_type,
  typename block_id_type>
void em_induce_plus_star_suffixes(
    std::uint64_t text_length,
    std::uint64_t radix_heap_bufsize,
    std::uint64_t radix_log,
    std::uint64_t max_block_size,
    std::vector<std::uint64_t> &block_count_target,
    std::string output_pos_filename,
    std::string output_count_filename,
    std::string minus_pos_filename,
    std::string minus_count_filename,
    std::vector<std::string> &plus_type_filenames,
    std::vector<std::string> &symbols_filenames,
    std::uint64_t &total_io_volume) {

  // Initialize radix heap.
  typedef em_radix_heap<char_type, block_id_type> radix_heap_type;
  radix_heap_type *radix_heap = new radix_heap_type(radix_log,
      radix_heap_bufsize, output_pos_filename);

  // Initialize readers of data associated with minus suffixes.
  typedef async_backward_stream_reader<text_offset_type> minus_count_reader_type;
  typedef async_backward_stream_reader<block_id_type> minus_pos_reader_type;
  minus_count_reader_type *minus_count_reader = new minus_count_reader_type(minus_count_filename);
  minus_pos_reader_type *minus_pos_reader = new minus_pos_reader_type(minus_pos_filename);

  // Initialize readers of data associated with plus suffixes.
  std::uint64_t n_blocks = (text_length + max_block_size - 1) / max_block_size;
  typedef async_backward_multi_bit_stream_reader plus_type_reader_type;
  plus_type_reader_type *plus_type_reader = new plus_type_reader_type(n_blocks);
  for (std::uint64_t block_id = 0; block_id < n_blocks; ++block_id)
    plus_type_reader->add_file(plus_type_filenames[block_id]);

  // Initialize the readers of data associated with both types of suffixes.
  typedef async_backward_multi_stream_reader<char_type> symbols_reader_type;
  symbols_reader_type *symbols_reader = new symbols_reader_type(n_blocks);
  for (std::uint64_t block_id = 0; block_id < n_blocks; ++block_id)
    symbols_reader->add_file(symbols_filenames[block_id]);

  // Initialize output writers.
  typedef async_stream_writer<block_id_type> output_pos_writer_type;
  typedef async_stream_writer<text_offset_type> output_count_writer_type;
  output_pos_writer_type *output_pos_writer = new output_pos_writer_type(output_pos_filename);
  output_count_writer_type *output_count_writer = new output_count_writer_type(output_count_filename);

  bool empty_output = true;
  std::uint64_t max_char = std::numeric_limits<char_type>::max();
  std::uint64_t head_char = 0;
  {
    std::uint64_t size = utils::file_size(minus_count_filename);
    if (size > 0)
      head_char = size / sizeof(text_offset_type) - 1;
  }
  std::uint64_t prev_written_head_char = 0;
  std::uint64_t cur_bucket_size = 0;
  std::vector<std::uint64_t> block_count(n_blocks, 0UL);

  // Induce plus suffixes.
  while (!radix_heap->empty() || !minus_count_reader->empty()) {
    // Process plus suffixes.
    while (!radix_heap->empty() && radix_heap->min_compare(max_char - head_char)) {
      std::pair<char_type, block_id_type> p = radix_heap->extract_min();
      std::uint64_t head_pos_block_id = p.second;
      bool is_head_pos_star = plus_type_reader->read_from_ith_file(head_pos_block_id);

      if (is_head_pos_star) {
        output_pos_writer->write(head_pos_block_id);
        if (!empty_output) {
          if (head_char == prev_written_head_char) ++cur_bucket_size;
          else {
            output_count_writer->write(cur_bucket_size);
            for (std::uint64_t ch = prev_written_head_char; ch > head_char + 1; --ch)
              output_count_writer->write(0);
            cur_bucket_size = 1;
            prev_written_head_char = head_char;
          }
        } else {
          cur_bucket_size = 1;
          prev_written_head_char = head_char;
        }
        empty_output = false;
      } else {
        ++block_count[head_pos_block_id];
        bool is_head_at_block_beg = (block_count[head_pos_block_id] == block_count_target[head_pos_block_id]);
        if (head_pos_block_id > 0 || !is_head_at_block_beg) {
          std::uint64_t prev_pos_char = symbols_reader->read_from_ith_file(head_pos_block_id);
          std::uint64_t prev_pos_block_id = head_pos_block_id - is_head_at_block_beg;
          radix_heap->push(max_char - prev_pos_char, prev_pos_block_id);
        }
      }
    }

    // Process minus suffixes.
    std::uint64_t minus_sufs_count = minus_count_reader->read();
    for (std::uint64_t i = 0; i < minus_sufs_count; ++i) {
      std::uint64_t head_pos_block_id = minus_pos_reader->read();
      ++block_count[head_pos_block_id];
      bool is_pos_head_at_block_beg = (block_count[head_pos_block_id] == block_count_target[head_pos_block_id]);
      std::uint64_t prev_pos_block_id = head_pos_block_id - is_pos_head_at_block_beg;
      std::uint64_t prev_pos_char = symbols_reader->read_from_ith_file(head_pos_block_id);
      radix_heap->push(max_char - prev_pos_char, prev_pos_block_id);
    }

    // Update current symbol.
    --head_char;
  }

  if (empty_output == false) {
    output_count_writer->write(cur_bucket_size);
    for (std::uint64_t ch = prev_written_head_char; ch > 0; --ch)
      output_count_writer->write(0);
  }

  // Update I/O volume.
  std::uint64_t io_volume = radix_heap->io_volume() +
    minus_pos_reader->bytes_read() + minus_count_reader->bytes_read() +
    plus_type_reader->bytes_read() + symbols_reader->bytes_read() +
    output_pos_writer->bytes_written() + output_count_writer->bytes_written();
  total_io_volume += io_volume;

  // Clean up.
  delete radix_heap;
  delete minus_pos_reader;
  delete minus_count_reader;
  delete plus_type_reader;
  delete symbols_reader;
  delete output_pos_writer;
  delete output_count_writer;
}

#endif  // __EM_INDUCE_PLUS_STAR_SUFFIXES_HPP_INCLUDED
