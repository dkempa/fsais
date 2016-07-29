#ifndef __EM_INDUCE_PLUS_STAR_SUBSTRINGS_HPP_INCLUDED
#define __EM_INDUCE_PLUS_STAR_SUBSTRINGS_HPP_INCLUDED

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <algorithm>

#include "packed_pair.hpp"
#include "em_radix_heap.hpp"
#include "io/async_backward_stream_reader.hpp"
#include "io/async_stream_reader.hpp"
#include "io/async_stream_writer.hpp"
#include "io/async_bit_stream_writer.hpp"
#include "io/async_multi_stream_reader.hpp"
#include "io/async_multi_bit_stream_reader.hpp"

#include "uint24.hpp"
#include "utils.hpp"


// TODO: split the ram_use into radix heap and readers
// Right now radix_heap uses all the ram, and I/O readers
// (non-contant number of them!) use the space beyond that.
template<typename char_type,
  typename text_offset_type,
  typename block_id_type,
  typename extext_block_id_type>
void em_induce_plus_star_substrings_large_alphabet(
    std::uint64_t text_length,
    std::uint64_t max_block_size,
    std::uint64_t ram_use,
    std::vector<std::uint64_t> &block_count_target,
    std::string text_filename,
    std::string output_pos_filename,
    std::string output_diff_filename,
    std::string output_count_filename,
    std::vector<std::string> &plus_type_filenames,
    std::vector<std::string> &symbols_filenames,
    std::uint64_t &total_io_volume) {
  std::uint64_t n_blocks = (text_length + max_block_size - 1) / max_block_size;
  std::uint64_t is_head_plus_bit = ((std::uint64_t)std::numeric_limits<extext_block_id_type>::max() + 1) / 2;
  std::uint64_t is_tail_plus_bit = is_head_plus_bit / 2;
  std::uint64_t io_volume = 0;

  if (text_length == 0) {
    fprintf(stderr, "\nError: text_length = 0\n");
    std::exit(EXIT_FAILURE);
  }

  if (n_blocks == 0) {
    fprintf(stderr, "\nError: n_blocks = 0\n");
    std::exit(EXIT_FAILURE);
  }

  // Check that all types are sufficiently large.
  if ((std::uint64_t)std::numeric_limits<text_offset_type>::max() < text_length - 1) {
    fprintf(stderr, "\nError: text_offset_type in em_induce_plus_star_substrings_large_alphabet too small!\n");
    std::exit(EXIT_FAILURE);
  }
  if ((std::uint64_t)std::numeric_limits<block_id_type>::max() < n_blocks - 1) {
    fprintf(stderr, "\nError: block_id_type in em_induce_plus_star_substrings_large_alphabet too small!\n");
    std::exit(EXIT_FAILURE);
  }
  if ((std::uint64_t)std::numeric_limits<extext_block_id_type>::max() < (n_blocks + 2) / 4UL) {
    fprintf(stderr, "\nError: extext_block_id_type in em_induce_plus_star_substrings_large_alphabet too small!\n");
    std::exit(EXIT_FAILURE);
  }

  // Start the timer.
  long double start = utils::wclock();
  fprintf(stderr, "  EM induce plus substrings (large alphabet):\n");
  fprintf(stderr, "    sizeof(extext_block_id_type) = %lu\n", sizeof(extext_block_id_type));

  // Initialize radix heap.
  std::vector<std::uint64_t> radix_logs;
  {
    std::uint64_t target_sum = 8UL * sizeof(char_type);
    std::uint64_t cur_sum = 0;
    while (cur_sum < target_sum) {
      std::uint64_t radix_log = std::min(10UL, target_sum - cur_sum);
      radix_logs.push_back(radix_log);
      cur_sum += radix_log;
    }
  }
  typedef packed_pair<extext_block_id_type, text_offset_type> ext_pair_type;
  typedef em_radix_heap<char_type, ext_pair_type> heap_type;
  heap_type *radix_heap = new heap_type(radix_logs, output_pos_filename, ram_use);

  // Initialize the readers.
  typedef async_multi_bit_stream_reader plus_type_reader_type;
  typedef async_multi_stream_reader<char_type> symbols_reader_type;
  plus_type_reader_type *plus_type_reader = new plus_type_reader_type(n_blocks);
  symbols_reader_type *symbols_reader = new symbols_reader_type(n_blocks);
  for (std::uint64_t block_id = 0; block_id < n_blocks; ++block_id) {
    plus_type_reader->add_file(plus_type_filenames[block_id]);
    symbols_reader->add_file(symbols_filenames[block_id]);
  }

  // Initialize the output writers.
  typedef async_stream_writer<block_id_type> output_pos_writer_type;
  typedef async_bit_stream_writer output_diff_writer_type;
  typedef async_stream_writer<text_offset_type> output_count_writer_type;
  output_pos_writer_type *output_pos_writer = new output_pos_writer_type(output_pos_filename);
  output_diff_writer_type *output_diff_writer = new output_diff_writer_type(output_diff_filename);
  output_count_writer_type *output_count_writer = new output_count_writer_type(output_count_filename);

  // Sort start positions of all minus star substrings by
  // the first symbol by adding them to the heap.
  {
    typedef async_backward_stream_reader<char_type> reader_type;
    reader_type *reader = new reader_type(text_filename);

    bool is_next_minus = false;
    char_type next_char = 0;
    std::uint64_t cur_pos_block_id = n_blocks - 1;
    std::uint64_t cur_pos_block_beg = cur_pos_block_id * max_block_size;
    std::uint64_t next_pos_block_id = 0;
    for (std::uint64_t iplus = text_length; iplus > 0; --iplus) {
      std::uint64_t i = iplus - 1;
      char_type cur_char = reader->read();
      bool is_minus = (iplus == text_length) ||
        ((std::uint64_t)cur_char > (std::uint64_t)next_char) ||
        ((std::uint64_t)cur_char == (std::uint64_t)next_char && is_next_minus);

      if (iplus < text_length && !is_minus && is_next_minus)
        radix_heap->push(std::numeric_limits<char_type>::max() - next_char, ext_pair_type(next_pos_block_id, 0));

      is_next_minus = is_minus;
      next_char = cur_char;
      next_pos_block_id = cur_pos_block_id;
      if (i == cur_pos_block_beg && i > 0) {
        --cur_pos_block_id;
        cur_pos_block_beg -= max_block_size;
      }
    }

    // Update I/O volume.
    io_volume += reader->bytes_read();

    // Clean up.
    delete reader;
  }

  bool empty_output = true;
  bool was_extract_min = false;
  bool is_prev_head_plus = false;
  bool is_prev_tail_plus = false;
  std::uint64_t diff_items = 0;
  std::uint64_t diff_items_snapshot = 0;
  std::uint64_t prev_tail_name = 0;
  std::uint64_t cur_bucket_size = 0;
  char_type prev_head_char = 0;
  char_type prev_written_head_char = 0;

  std::vector<std::uint64_t> block_count(n_blocks, 0UL);
  while (!radix_heap->empty()) {
    std::pair<char_type, ext_pair_type> p = radix_heap->extract_min();
    char_type head_char = std::numeric_limits<char_type>::max() - p.first;
    std::uint64_t block_id = p.second.first;
    std::uint64_t tail_name = p.second.second;

    // Unpack flags from block id.
    bool is_head_plus = false;
    bool is_tail_plus = false;
    if (block_id & is_head_plus_bit) {
      block_id -= is_head_plus_bit;
      is_head_plus = true;
    }
    if (block_id & is_tail_plus_bit) {
      block_id -= is_tail_plus_bit;
      is_tail_plus = true;
    }

    // Update block count.
    ++block_count[block_id];
    bool head_pos_at_block_beg = (block_count[block_id] ==
        block_count_target[block_id]);

    if (is_head_plus) {
      head_char = (std::uint64_t)head_char - 1;
      bool is_diff = false;
      if (was_extract_min) {
        if (!is_prev_head_plus || is_tail_plus != is_prev_tail_plus ||
            head_char != prev_head_char || tail_name != prev_tail_name)
          is_diff = true;
      } else is_diff = true;
      was_extract_min = true;
      diff_items += is_diff;

      // Note the +1 below. This is because we want the item in bucket c from the input to be processed.
      // after the items that were inserted in the line below. One way to do this is to insert items from
      // the input with a key decreased by one. Since we might not be able to always decrease, instead
      // we increase the key of the item inserted below. We can always do that, because there is no
      // plus-substring that begins with the maximal symbol in the alphabet.
      bool is_star = plus_type_reader->read_from_ith_file(block_id);
      if (is_star == true) {
        bool next_output_bit = false;
        if (empty_output == false) {
          if (diff_items != diff_items_snapshot)
            next_output_bit = true;
        } else next_output_bit = true;

        output_pos_writer->write(block_id);
        if (empty_output == false)
          output_diff_writer->write(next_output_bit);

        if (empty_output == false) {
          if (head_char == prev_written_head_char) ++cur_bucket_size;
          else {
            output_count_writer->write(cur_bucket_size);
            for (std::uint64_t ch = (std::uint64_t)prev_written_head_char; ch > (std::uint64_t)head_char + 1; --ch)
              output_count_writer->write(0);
            cur_bucket_size = 1;
            prev_written_head_char = head_char;
          }
        } else {
          cur_bucket_size = 1;
          prev_written_head_char = head_char;
        }

        empty_output = false;
        diff_items_snapshot = diff_items;
      } else if (block_id > 0 || head_pos_at_block_beg == false) {
        char_type prev_char = symbols_reader->read_from_ith_file(block_id);
        std::uint64_t prev_pos_block_idx = block_id - head_pos_at_block_beg;
        std::uint64_t new_block_id = (prev_pos_block_idx | is_head_plus_bit | is_tail_plus_bit);
        radix_heap->push(std::numeric_limits<char_type>::max() - (prev_char + 1), ext_pair_type(new_block_id, diff_items - 1));
      }
    } else {
      char_type prev_char = symbols_reader->read_from_ith_file(block_id);
      std::uint64_t prev_pos_block_idx = block_id - head_pos_at_block_beg;
      std::uint64_t new_block_id = (prev_pos_block_idx | is_head_plus_bit);
      radix_heap->push(std::numeric_limits<char_type>::max() - (prev_char + 1), ext_pair_type(new_block_id, (std::uint64_t)head_char));
    }

    is_prev_head_plus = is_head_plus;
    is_prev_tail_plus = is_tail_plus;
    prev_head_char = head_char;
    prev_tail_name = tail_name;
  }

  if (empty_output == false) {
    output_count_writer->write(cur_bucket_size);
    for (std::uint64_t ch = (std::uint64_t)prev_written_head_char; ch > 0; --ch)
      output_count_writer->write(0);
  }

  // Update I/O volume.
  io_volume += radix_heap->io_volume() +
    plus_type_reader->bytes_read() + symbols_reader->bytes_read() +
    output_pos_writer->bytes_written() + output_diff_writer->bytes_written() +
    output_count_writer->bytes_written();
  total_io_volume += io_volume;

  // Clean up.
  delete radix_heap;
  delete plus_type_reader;
  delete symbols_reader;
  delete output_pos_writer;
  delete output_diff_writer;
  delete output_count_writer;

  long double total_time = utils::wclock() - start;
  fprintf(stderr, "    time = %.2Lfs, I/O = %.2LfMiB/s, total I/O vol = %.1Lfn bytes\n", total_time,
      (1.L * io_volume / (1L << 20)) / total_time, (1.L * total_io_volume) / text_length);
}

template<typename char_type,
  typename text_offset_type,
  typename block_id_type>
void em_induce_plus_star_substrings_large_alphabet(
    std::uint64_t text_length,
    std::uint64_t max_block_size,
    std::uint64_t ram_use,
    std::vector<std::uint64_t> &block_count_target,
    std::string text_filename,
    std::string output_pos_filename,
    std::string output_diff_filename,
    std::string output_count_filename,
    std::vector<std::string> &plus_type_filenames,
    std::vector<std::string> &symbols_filenames,
    std::uint64_t &total_io_volume) {
  std::uint64_t n_blocks = (text_length + max_block_size - 1) / max_block_size;
  if (n_blocks < (1UL << 6))
    em_induce_plus_star_substrings_large_alphabet<char_type, text_offset_type, block_id_type, std::uint8_t>(text_length, max_block_size, ram_use, block_count_target,
    text_filename, output_pos_filename, output_diff_filename, output_count_filename, plus_type_filenames, symbols_filenames, total_io_volume);
  else if (n_blocks < (1UL << 14))
    em_induce_plus_star_substrings_large_alphabet<char_type, text_offset_type, block_id_type, std::uint16_t>(text_length, max_block_size, ram_use, block_count_target,
    text_filename, output_pos_filename, output_diff_filename, output_count_filename, plus_type_filenames, symbols_filenames, total_io_volume);
//  else if (n_blocks < (1UL << 22))
//    em_induce_plus_star_substrings_large_alphabet<char_type, text_offset_type, block_id_type, uint24>(text_length, max_block_size, ram_use, block_count_target,
//    text_filename, output_pos_filename, output_diff_filename, output_count_filename, plus_type_filenames, symbols_filenames, total_io_volume);
//  else if (n_blocks < (1UL << 30))
//    em_induce_plus_star_substrings_large_alphabet<char_type, text_offset_type, block_id_type, std::uint32_t>(text_length, max_block_size, ram_use, block_count_target,
//    text_filename, output_pos_filename, output_diff_filename, output_count_filename, plus_type_filenames, symbols_filenames, total_io_volume);
  else
    em_induce_plus_star_substrings_large_alphabet<char_type, text_offset_type, block_id_type, std::uint64_t>(text_length, max_block_size, ram_use, block_count_target,
    text_filename, output_pos_filename, output_diff_filename, output_count_filename, plus_type_filenames, symbols_filenames, total_io_volume);
}

// TODO: split the ram_use into radix heap and readers
// Right now radix_heap uses all the ram, and I/O readers
// (non-contant number of them!) use the space beyond that.
template<typename char_type,
  typename text_offset_type,
  typename block_id_type,
  typename extext_block_id_type>
void em_induce_plus_star_substrings_small_alphabet(
    std::uint64_t text_length,
    std::uint64_t max_block_size,
    std::uint64_t text_alphabet_size,
    std::uint64_t ram_use,
    std::vector<std::uint64_t> &block_count_target,
    std::string text_filename,
    std::string output_pos_filename,
    std::string output_diff_filename,
    std::string output_count_filename,
    std::vector<std::string> &plus_type_filenames,
    std::vector<std::string> &symbols_filenames,
    std::uint64_t &total_io_volume) {
  std::uint64_t n_blocks = (text_length + max_block_size - 1) / max_block_size;
  std::uint64_t is_diff_bit = ((std::uint64_t)std::numeric_limits<extext_block_id_type>::max() + 1) / 2;
  std::uint64_t is_head_plus_bit = is_diff_bit / 2;
  std::uint64_t io_volume = 0;

  if (text_length == 0) {
    fprintf(stderr, "\nError: text_length = 0\n");
    std::exit(EXIT_FAILURE);
  }

  if (n_blocks == 0) {
    fprintf(stderr, "\nError: n_blocks = 0\n");
    std::exit(EXIT_FAILURE);
  }

  if (text_alphabet_size == 0) {
    fprintf(stderr, "\nError: text_alphabet_size = 0\n");
    std::exit(EXIT_FAILURE);
  }

  // Check that all types are sufficiently large.
  if ((std::uint64_t)std::numeric_limits<char_type>::max() < text_alphabet_size - 1) {
    fprintf(stderr, "\nError: char_type in em_induce_plus_star_substrings_small_alphabet too small!\n");
    std::exit(EXIT_FAILURE);
  }
  if ((std::uint64_t)std::numeric_limits<text_offset_type>::max() < text_length - 1) {
    fprintf(stderr, "\nError: text_offset_type in em_induce_plus_star_substrings_small_alphabet too small!\n");
    std::exit(EXIT_FAILURE);
  }
  if ((std::uint64_t)std::numeric_limits<block_id_type>::max() < n_blocks - 1) {
    fprintf(stderr, "\nError: block_id_type in em_induce_plus_star_substrings_small_alphabet too small!\n");
    std::exit(EXIT_FAILURE);
  }
  if ((std::uint64_t)std::numeric_limits<extext_block_id_type>::max() < (n_blocks + 2) / 4UL) {
    fprintf(stderr, "\nError: extext_block_id_type in em_induce_plus_star_substrings_small_alphabet too small!\n");
    std::exit(EXIT_FAILURE);
  }

  // Start the timer.
  long double start = utils::wclock();
  fprintf(stderr, "  EM induce plus substrings (small alphabet):\n");
  fprintf(stderr, "    sizeof(extext_block_id_type) = %lu\n", sizeof(extext_block_id_type));

  // Initialize radix heap.
  std::vector<std::uint64_t> radix_logs;
  {
    std::uint64_t target_sum = 8UL * sizeof(char_type);
    std::uint64_t cur_sum = 0;
    while (cur_sum < target_sum) {
      std::uint64_t radix_log = std::min(10UL, target_sum - cur_sum);
      radix_logs.push_back(radix_log);
      cur_sum += radix_log;
    }
  }
  typedef em_radix_heap<char_type, extext_block_id_type> heap_type;
  heap_type *radix_heap = new heap_type(radix_logs, output_pos_filename, ram_use);



  // Initialize the readers.
  typedef async_multi_bit_stream_reader plus_type_reader_type;
  typedef async_multi_stream_reader<char_type> symbols_reader_type;
  plus_type_reader_type *plus_type_reader = new plus_type_reader_type(n_blocks);
  symbols_reader_type *symbols_reader = new symbols_reader_type(n_blocks);
  for (std::uint64_t block_id = 0; block_id < n_blocks; ++block_id) {
    plus_type_reader->add_file(plus_type_filenames[block_id]);
    symbols_reader->add_file(symbols_filenames[block_id]);
  }

  // Initialize the output writers.
  typedef async_stream_writer<block_id_type> output_pos_writer_type;
  typedef async_bit_stream_writer output_diff_writer_type;
  typedef async_stream_writer<text_offset_type> output_count_writer_type;
  output_pos_writer_type *output_pos_writer = new output_pos_writer_type(output_pos_filename);
  output_diff_writer_type *output_diff_writer = new output_diff_writer_type(output_diff_filename);
  output_count_writer_type *output_count_writer = new output_count_writer_type(output_count_filename);

  // Sort start positions of all minus star substrings by
  // the first symbol by adding them to the heap.
  {
    typedef async_backward_stream_reader<char_type> reader_type;
    reader_type *reader = new reader_type(text_filename);

    bool is_next_minus = false;
    char_type next_char = 0;
    std::uint64_t cur_pos_block_id = n_blocks - 1;
    std::uint64_t cur_pos_block_beg = cur_pos_block_id * max_block_size;
    std::uint64_t next_pos_block_id = 0;
    for (std::uint64_t iplus = text_length; iplus > 0; --iplus) {
      std::uint64_t i = iplus - 1;
      char_type cur_char = reader->read();
      bool is_minus = (iplus == text_length) ||
        ((std::uint64_t)cur_char > (std::uint64_t)next_char) ||
        ((std::uint64_t)cur_char == (std::uint64_t)next_char && is_next_minus);

      if (iplus < text_length && !is_minus && is_next_minus)
        radix_heap->push(std::numeric_limits<char_type>::max() - next_char, next_pos_block_id);

      is_next_minus = is_minus;
      next_char = cur_char;
      next_pos_block_id = cur_pos_block_id;
      if (i == cur_pos_block_beg && i > 0) {
        --cur_pos_block_id;
        cur_pos_block_beg -= max_block_size;
      }
    }

    // Update I/O volume.
    io_volume += reader->bytes_read();

    // Clean up.
    delete reader;
  }

  char_type prev_head_char = 0;
  char_type prev_written_head_char = 0;
  bool empty_output = true;
  bool was_extract_min = false;
  bool was_prev_head_minus = false;
  std::uint64_t cur_substring_name_snapshot = 0;
  std::uint64_t current_timestamp = 0;
  std::uint64_t cur_substring_name = 0;
  std::uint64_t cur_bucket_size = 0;
  std::vector<std::uint64_t> block_count(n_blocks, 0UL);
  std::vector<text_offset_type> symbol_timestamps(text_alphabet_size, (text_offset_type)0);

  while (!radix_heap->empty()) {
    std::pair<char_type, extext_block_id_type> p = radix_heap->extract_min();
    char_type head_char = std::numeric_limits<char_type>::max() - p.first;
    std::uint64_t block_id = p.second;

    // Unpack flags from block id.
    bool is_head_plus = false;
    if (block_id & is_head_plus_bit) {
      block_id -= is_head_plus_bit;
      is_head_plus = true;
    }

    bool is_diff_than_prev_extracted = false;
    if (is_head_plus == false) {
      if (was_prev_head_minus == true)
        is_diff_than_prev_extracted = (head_char != prev_head_char);
      else is_diff_than_prev_extracted = true;
      was_prev_head_minus = true;
    } else {
      if (block_id & is_diff_bit) {
        block_id -= is_diff_bit;
        is_diff_than_prev_extracted = true;
      }
      was_prev_head_minus = false;
    }

    // Update cur_substring_name.
    cur_substring_name += (was_extract_min && is_diff_than_prev_extracted);
    if (is_diff_than_prev_extracted == true)
      ++current_timestamp;
    was_extract_min = true;

    // Update block count.
    ++block_count[block_id];
    bool head_pos_at_block_beg = (block_count[block_id] == block_count_target[block_id]);

    if (is_head_plus) {
      head_char = (std::uint64_t)head_char - 1;
      // Note the +1 below. This is because we want the item in bucket c from the input to be processed.
      // after the items that were inserted in the line below. One way to do this is to insert items from
      // the input with a key decreased by one. Since we might not be able to always decrease, instead
      // we increase the key of the item inserted below. We can always do that, because there is no
      // plus-substring that begins with the maximal symbol in the alphabet.
      bool is_star = plus_type_reader->read_from_ith_file(block_id);
      if (is_star == true) {
        bool next_output_bit = false;
        if (empty_output == false) {
          if (cur_substring_name_snapshot != cur_substring_name)
            next_output_bit = true;
        } else next_output_bit = true;

        output_pos_writer->write(block_id);
        if (empty_output == false)
          output_diff_writer->write(next_output_bit);

        if (empty_output == false) {
          if (head_char == prev_written_head_char) ++cur_bucket_size;
          else {
            output_count_writer->write(cur_bucket_size);
            for (std::uint64_t ch = (std::uint64_t)prev_written_head_char; ch > (std::uint64_t)head_char + 1; --ch)
              output_count_writer->write(0);
            cur_bucket_size = 1;
            prev_written_head_char = head_char;
          }
        } else {
          cur_bucket_size = 1;
          prev_written_head_char = head_char;
        }

        empty_output = false;
        cur_substring_name_snapshot = cur_substring_name;
      } else if (block_id > 0 || head_pos_at_block_beg == false) {
        char_type prev_char = symbols_reader->read_from_ith_file(block_id);
        std::uint64_t prev_pos_block_idx = block_id - head_pos_at_block_beg;
        std::uint64_t new_block_id = (prev_pos_block_idx | is_head_plus_bit);
        if ((std::uint64_t)symbol_timestamps[prev_char] != current_timestamp)
          new_block_id |= is_diff_bit;
        radix_heap->push(std::numeric_limits<char_type>::max() - (prev_char + 1), new_block_id);
        symbol_timestamps[prev_char] = current_timestamp;
      }
    } else {
      char_type prev_char = symbols_reader->read_from_ith_file(block_id);
      std::uint64_t prev_pos_block_idx = block_id - head_pos_at_block_beg;
      std::uint64_t new_block_id = (prev_pos_block_idx | is_head_plus_bit);
      if ((std::uint64_t)symbol_timestamps[prev_char] != current_timestamp)
        new_block_id |= is_diff_bit;
      radix_heap->push(std::numeric_limits<char_type>::max() - (prev_char + 1), new_block_id);
      symbol_timestamps[prev_char] = current_timestamp;
    }

    prev_head_char = head_char;
  }

  if (empty_output == false) {
    output_count_writer->write(cur_bucket_size);
    for (std::uint64_t ch = (std::uint64_t)prev_written_head_char; ch > 0; --ch)
      output_count_writer->write(0);
  }

  // Update I/O volume.
  io_volume += radix_heap->io_volume() +
    plus_type_reader->bytes_read() + symbols_reader->bytes_read() +
    output_pos_writer->bytes_written() + output_diff_writer->bytes_written() +
    output_count_writer->bytes_written();
  total_io_volume += io_volume;

  // Clean up.
  delete radix_heap;
  delete plus_type_reader;
  delete symbols_reader;
  delete output_pos_writer;
  delete output_diff_writer;
  delete output_count_writer;

  long double total_time = utils::wclock() - start;
  fprintf(stderr, "    time = %.2Lfs, I/O = %.2LfMiB/s, total I/O vol = %.1Lfn bytes\n", total_time,
      (1.L * io_volume / (1L << 20)) / total_time, (1.L * total_io_volume) / text_length);
}

template<typename char_type,
  typename text_offset_type,
  typename block_id_type>
void em_induce_plus_star_substrings_small_alphabet(
    std::uint64_t text_length,
    std::uint64_t max_block_size,
    std::uint64_t text_alphabet_size,
    std::uint64_t ram_use,
    std::vector<std::uint64_t> &block_count_target,
    std::string text_filename,
    std::string output_pos_filename,
    std::string output_diff_filename,
    std::string output_count_filename,
    std::vector<std::string> &plus_type_filenames,
    std::vector<std::string> &symbols_filenames,
    std::uint64_t &total_io_volume) {
  std::uint64_t n_blocks = (text_length + max_block_size - 1) / max_block_size;
  if (n_blocks < (1UL << 6))
    em_induce_plus_star_substrings_small_alphabet<char_type, text_offset_type, block_id_type, std::uint8_t>(text_length, max_block_size, text_alphabet_size, ram_use,
        block_count_target, text_filename, output_pos_filename, output_diff_filename, output_count_filename, plus_type_filenames, symbols_filenames, total_io_volume);
  else if (n_blocks < (1UL << 14))
    em_induce_plus_star_substrings_small_alphabet<char_type, text_offset_type, block_id_type, std::uint16_t>(text_length, max_block_size, text_alphabet_size, ram_use,
        block_count_target, text_filename, output_pos_filename, output_diff_filename, output_count_filename, plus_type_filenames, symbols_filenames, total_io_volume);
//  else if (n_blocks < (1UL << 22))
//    em_induce_plus_star_substrings_small_alphabet<char_type, text_offset_type, block_id_type, uint24>(text_length, max_block_size, text_alphabet_size, ram_use,
//        block_count_target, text_filename, output_pos_filename, output_diff_filename, output_count_filename, plus_type_filenames, symbols_filenames, total_io_volume);
//  else if (n_blocks < (1UL << 30))
//    em_induce_plus_star_substrings_small_alphabet<char_type, text_offset_type, block_id_type, std::uint32_t>(text_length, max_block_size, text_alphabet_size, ram_use,
//        block_count_target, text_filename, output_pos_filename, output_diff_filename, output_count_filename, plus_type_filenames, symbols_filenames, total_io_volume);
  else
    em_induce_plus_star_substrings_small_alphabet<char_type, text_offset_type, block_id_type, std::uint64_t>(text_length, max_block_size, text_alphabet_size, ram_use,
        block_count_target, text_filename, output_pos_filename, output_diff_filename, output_count_filename, plus_type_filenames, symbols_filenames, total_io_volume);
}

template<typename char_type,
  typename text_offset_type,
  typename block_id_type>
void em_induce_plus_star_substrings(
    std::uint64_t text_length,
    std::uint64_t max_block_size,
    std::uint64_t text_alphabet_size,
    std::uint64_t ram_use,
    std::vector<std::uint64_t> &block_count_target,
    std::string text_filename,
    std::string output_pos_filename,
    std::string output_diff_filename,
    std::string output_count_filename,
    std::vector<std::string> &plus_type_filenames,
    std::vector<std::string> &symbols_filenames,
    std::uint64_t &total_io_volume) {
  // TODO more sophisticated criterion
  if (text_alphabet_size <= 2000000)
    em_induce_plus_star_substrings_small_alphabet<char_type, text_offset_type, block_id_type>(text_length,
        max_block_size, text_alphabet_size, ram_use, block_count_target, text_filename, output_pos_filename,
        output_diff_filename, output_count_filename, plus_type_filenames, symbols_filenames, total_io_volume);
  else
    em_induce_plus_star_substrings_large_alphabet<char_type, text_offset_type, block_id_type>(text_length,
        max_block_size, ram_use, block_count_target, text_filename, output_pos_filename, output_diff_filename,
        output_count_filename, plus_type_filenames, symbols_filenames, total_io_volume);
}

#endif  // __EM_INDUCE_PLUS_STAR_SUBSTRINGS_HPP_INCLUDED