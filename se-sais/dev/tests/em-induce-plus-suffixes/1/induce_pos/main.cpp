#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <ctime>
#include <map>
#include <unistd.h>

#include "em_induce_plus_suffixes.hpp"
#include "io/async_backward_stream_reader.hpp"
#include "io/async_stream_reader.hpp"
#include "io/async_stream_writer.hpp"
#include "io/async_bit_stream_writer.hpp"
#include "io/async_backward_bit_stream_reader.hpp"
#include "utils.hpp"
#include "uint40.hpp"
#include "uint48.hpp"
#include "divsufsort.h"


template<typename T, typename stream_type>
void encode_vbyte_to_stream(T x, stream_type *stream) {
  std::uint64_t y = x;
  while (y > 127) {
    stream->write((std::uint8_t)((y & 0x7f) | 0x80));
    y >>= 7;
  }
  stream->write((std::uint8_t)y);
}

void test(std::uint64_t n_testcases, std::uint64_t max_length, std::uint64_t radix_heap_bufsize, std::uint64_t radix_log) {
  fprintf(stderr, "TEST, n_testcases=%lu, max_length=%lu, buffer_size=%lu, radix_log=%lu\n", n_testcases, max_length, radix_heap_bufsize, radix_log);

  typedef std::uint8_t chr_t;
  typedef std::uint32_t saidx_tt;

  chr_t *text = new chr_t[max_length];
  saidx_tt *sa = new saidx_tt[max_length];
  bool *suf_type = new bool[max_length];

  for (std::uint64_t testid = 0; testid < n_testcases; ++testid) {
    std::uint64_t text_length = utils::random_int64(1L, (std::int64_t)max_length);
    for (std::uint64_t j = 0; j < text_length; ++j)
      text[j] = utils::random_int64(0L, 255L);
    divsufsort(text, (std::int32_t *)sa, text_length);
    typedef std::uint8_t blockidx_t;
    std::uint64_t max_block_size = 0;
    std::uint64_t n_blocks = 0;
    do {
      max_block_size = utils::random_int64(1L, (std::int64_t)text_length);
      n_blocks = (text_length + max_block_size - 1) / max_block_size;
    } while (n_blocks > 256);

    for (std::uint64_t i = text_length; i > 0; --i) {
      if (i == text_length) suf_type[i - 1] = 0;              // minus
      else {
        if (text[i - 1] > text[i]) suf_type[i - 1] = 0;       // minus
        else if (text[i - 1] < text[i]) suf_type[i - 1] = 1;  // plus
        else suf_type[i - 1] = suf_type[i];
      }
    }

    std::string minus_count_filename = "tmp." + utils::random_string_hash();
    {
      typedef async_stream_writer<saidx_tt> writer_type;
      writer_type *writer = new writer_type(minus_count_filename);
#if 0
      std::uint64_t end = text_length;
      for (std::uint64_t ch = 256; ch > 0; --ch) {
        std::uint64_t minus_count = 0;
        std::uint64_t beg = end;
        while (beg > 0 && text[sa[beg - 1]] == ch - 1) {
          if (suf_type[sa[beg - 1]] == 0 && sa[beg - 1] > 0 && suf_type[sa[beg - 1] - 1] == 1) ++minus_count;
          --beg;
        }
        writer->write(minus_count);
        end = beg;
      }
#else
      std::uint64_t beg = 0;
      for (std::uint64_t ch = 0; ch < 256; ++ch) {
        std::uint64_t minus_count = 0;
        std::uint64_t end = beg;
        while (end < text_length && text[sa[end]] == ch) {
          if (suf_type[sa[end]] == 0 && sa[end] > 0 && suf_type[sa[end] - 1] == 1)
            ++minus_count;
          ++end;
        }
        writer->write(minus_count);
        beg = end;
      }
#endif
      delete writer;
    }
    
    std::vector<std::string> symbols_filenames;
    {
      for (std::uint64_t j = 0; j < n_blocks; ++j) {
        std::string filename = "tmp." + utils::random_string_hash();
        symbols_filenames.push_back(filename);
      }
      typedef async_stream_writer<chr_t> writer_type;
      writer_type **writers = new writer_type*[n_blocks];
      for (std::uint64_t j = 0; j < n_blocks; ++j)
        writers[j] = new writer_type(symbols_filenames[j]);
      for (std::uint64_t j = 0; j < text_length; ++j) {
        std::uint64_t s = sa[j];
        if (s > 0 && suf_type[s - 1] == 1) {
          std::uint64_t block_id = s / max_block_size;
          writers[block_id]->write(text[s - 1]);
        }
      }
      for (std::uint64_t j = 0; j < n_blocks; ++j)
        delete writers[j];
      delete[] writers;
    }

    std::vector<std::string> plus_type_filenames;
    {
      for (std::uint64_t j = 0; j < n_blocks; ++j) {
        std::string filename = "tmp." + utils::random_string_hash();
        plus_type_filenames.push_back(filename);
      }
      typedef async_bit_stream_writer writer_type;
      writer_type **writers = new writer_type*[n_blocks];
      for (std::uint64_t j = 0; j < n_blocks; ++j)
        writers[j] = new writer_type(plus_type_filenames[j]);
      for (std::uint64_t j = 0; j < text_length; ++j) {
        std::uint64_t s = sa[j];
        if (suf_type[s] == 1) {
          std::uint64_t block_id = s / max_block_size;
          writers[block_id]->write((s > 0) && (suf_type[s - 1] == 0));
        }
      }
      for (std::uint64_t j = 0; j < n_blocks; ++j)
        delete writers[j];
      delete[] writers;
    }
    
    std::vector<std::string> plus_pos_filenames;
    {
      for (std::uint64_t j = 0; j < n_blocks; ++j) {
        std::string filename = "tmp." + utils::random_string_hash();
        plus_pos_filenames.push_back(filename);
      }
      typedef async_stream_writer<saidx_tt> writer_type;
      writer_type **writers = new writer_type*[n_blocks];
      for (std::uint64_t j = 0; j < n_blocks; ++j)
        writers[j] = new writer_type(plus_pos_filenames[j]);
      for (std::uint64_t j = 0; j < text_length; ++j) {
        std::uint64_t s = sa[j];
        if (suf_type[s] == 1) {
          std::uint64_t block_id = s / max_block_size;
          writers[block_id]->write((saidx_tt)s);
        }
      }
      for (std::uint64_t j = 0; j < n_blocks; ++j)
        delete writers[j];
      delete[] writers;
    }

    std::vector<std::uint64_t> block_count(n_blocks, 0UL);
    std::vector<std::uint64_t> block_count_target(n_blocks, 0UL);
    {
      for (std::uint64_t j = text_length; j > 0; --j) {
        std::uint64_t s = sa[j - 1];
        std::uint64_t block_id = s / max_block_size;
        bool is_at_block_beg = (block_id * max_block_size == s);
        if (s > 0 && suf_type[s] == 0 && suf_type[s - 1] == 1) {
          ++block_count[block_id];
          if (is_at_block_beg == true)
            block_count_target[block_id] = block_count[block_id];
        }
      }
    }

    // Write all lex-sorted star suffixes to file.
    std::string minus_pos_filename = "tmp." + utils::random_string_hash();
    {
      typedef async_stream_writer<blockidx_t> writer_type;
      writer_type *writer = new writer_type(minus_pos_filename);
      for (std::uint64_t i = 0; i < text_length; ++i) {
        std::uint64_t s = sa[i];
        if (s > 0 && suf_type[s] == 0 && suf_type[s - 1] == 1) {
          std::uint64_t block_id = s / max_block_size;
          writer->write(block_id);
        }
      }
      delete writer;
    }

    // Create a vector with all minus-suffixes (i.e., correct answer).
    std::vector<saidx_tt> v;
    for (std::uint64_t i = 0; i < text_length; ++i) {
      std::uint64_t s = sa[i];
      if (suf_type[s] == 1)
        v.push_back((saidx_tt)s);
    }

    // Run the tested algorithm.
    std::string output_pos_filename = "tmp." + utils::random_string_hash();
    std::string output_type_filename = "tmp." + utils::random_string_hash();
    std::string output_count_filename = "tmp." + utils::random_string_hash();
    std::uint64_t total_io_volume = 0;
    em_induce_plus_suffixes<chr_t, saidx_tt, blockidx_t>(
        text_length,
        radix_heap_bufsize,
        radix_log,
        max_block_size,
        255,
        block_count_target,
        output_pos_filename,
        output_type_filename,
        output_count_filename,
        minus_pos_filename,
        minus_count_filename,
        plus_type_filenames,
        plus_pos_filenames,
        symbols_filenames,
        total_io_volume);

    // Delete input files.
    utils::file_delete(minus_pos_filename);
    utils::file_delete(minus_count_filename);
    for (std::uint64_t i = 0; i < n_blocks; ++i) {
      if (utils::file_exists(symbols_filenames[i])) utils::file_delete(symbols_filenames[i]);
      if (utils::file_exists(plus_type_filenames[i])) utils::file_delete(plus_type_filenames[i]);
      if (utils::file_exists(plus_pos_filenames[i])) utils::file_delete(plus_pos_filenames[i]);
    }
    
    // Read the computed output into vector.
    std::vector<saidx_tt> v_computed;
    {
      typedef async_backward_stream_reader<saidx_tt> reader_type;
      reader_type *reader = new reader_type(output_pos_filename);
      while (!reader->empty())
        v_computed.push_back(reader->read());
      delete reader;
    }

    std::vector<std::uint64_t> v_type_correct;
    {
      for (std::uint64_t j = 0; j < text_length; ++j) {
        std::uint64_t s = sa[j];
        if (suf_type[s] == 1) {
          std::uint8_t is_star = (s > 0 && suf_type[s - 1] == 0);
          v_type_correct.push_back(is_star);
        }
      }
    }

    std::vector<std::uint8_t> v_type_computed;
    {
      typedef async_backward_bit_stream_reader reader_type;
      reader_type *reader = new reader_type(output_type_filename);
      for (std::uint64_t j = 0; j < v_type_correct.size(); ++j) {
        std::uint8_t bit = reader->read();
        v_type_computed.push_back(bit);
      }
      delete reader;
    }

    if (v_type_correct.size() != v_type_computed.size() ||
        std::equal(v_type_correct.begin(), v_type_correct.end(), v_type_computed.begin()) == false) {
      fprintf(stderr, "Error: types don't match!\n");
      std::exit(EXIT_FAILURE);
    }

    std::vector<std::pair<chr_t, std::uint64_t> > v_computed_count;
    {
      typedef async_backward_stream_reader<saidx_t> reader_type;
      reader_type *reader = new reader_type(output_count_filename);
      chr_t cur_sym = 0;
      while (reader->empty() == false) {
        std::uint64_t count = reader->read();
        if (count > 0)
          v_computed_count.push_back(std::make_pair(cur_sym, count));
        ++cur_sym;
      }
      delete reader;
    }

    std::vector<std::pair<chr_t, std::uint64_t> > v_correct_count;
    {
      std::map<chr_t, std::uint64_t> m;
      for (std::uint64_t j = 0; j < text_length; ++j) {
        std::uint64_t s = sa[j];
        if (suf_type[s] == 1)
          m[text[s]] += 1;
      }
      for (std::map<chr_t, std::uint64_t>::iterator it = m.begin(); it != m.end(); ++it)
        v_correct_count.push_back(std::make_pair(it->first, it->second));
    }

    if (v_computed_count.size() != v_correct_count.size() ||
        std::equal(v_computed_count.begin(), v_computed_count.end(), v_correct_count.begin()) == false) {
      fprintf(stderr, "Error: counts do not match!\n");
      std::exit(EXIT_FAILURE);
    }


    // Delete output files.
    utils::file_delete(output_pos_filename);
    utils::file_delete(output_type_filename);
    utils::file_delete(output_count_filename);

    // Compare answer.
    bool ok = true;
    if (v.size() != v_computed.size()) ok = false;
    else if (!std::equal(v.begin(), v.end(), v_computed.begin())) ok = false;
    if (!ok) {
      fprintf(stderr, "Error:\n");
      fprintf(stderr, "  text: ");
      for (std::uint64_t i = 0; i < text_length; ++i)
        fprintf(stderr, "%c", text[i]);
      fprintf(stderr, "\n");
      fprintf(stderr, "  SA: ");
      for (std::uint64_t i = 0; i < text_length; ++i)
        fprintf(stderr, "%lu ", (std::uint64_t)sa[i]);
      fprintf(stderr, "\n");
      fprintf(stderr, "  computed result: ");
      for (std::uint64_t i = 0; i < v_computed.size(); ++i)
        fprintf(stderr, "%lu ", (std::uint64_t)v_computed[i]);
      fprintf(stderr, "\n");
      fprintf(stderr, "  correct result: ");
      for (std::uint64_t i = 0; i < v.size(); ++i)
        fprintf(stderr, "%lu ", (std::uint64_t)v[i]);
      fprintf(stderr, "\n");
      std::exit(EXIT_FAILURE);
    }
  }

  delete[] text;
  delete[] sa;
  delete[] suf_type;
}

int main() {
  srand(time(0) + getpid());

  for (std::uint64_t max_length = 1; max_length <= (1L << 14); max_length *= 2)
    for (std::uint64_t buffer_size = 1; buffer_size <= /*(1L << 10)*/1; buffer_size *= 2)
      for (std::uint64_t radix_log = 1; radix_log <= /*5*/1; ++radix_log)
        test(/*100*/1000, max_length, buffer_size, radix_log);

  fprintf(stderr, "All tests passed.\n");
}
