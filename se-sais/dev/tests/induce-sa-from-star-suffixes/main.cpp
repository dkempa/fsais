#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <ctime>
#include <unistd.h>

#include "induce_plus_suffixes.hpp"
#include "induce_minus_suffixes.hpp"
#include "io/async_stream_reader.hpp"
#include "io/async_stream_writer.hpp"
#include "utils.hpp"
#include "uint40.hpp"
#include "uint48.hpp"
#include "divsufsort.h"


void test(std::uint64_t n_testcases, std::uint64_t max_length, std::uint64_t ram_use, std::uint64_t buffer_size) {
  fprintf(stderr, "TEST, n_testcases=%lu, max_length=%lu, ram_use=%lu, buffer_size=%lu\n", n_testcases, max_length, ram_use, buffer_size);

  typedef std::uint8_t chr_t;
  typedef std::uint32_t saidx_tt;

  chr_t *text = new chr_t[max_length];
  saidx_tt *sa = new saidx_tt[max_length];
  bool *suf_type = new bool[max_length];

  for (std::uint64_t testid = 0; testid < n_testcases; ++testid) {
    std::uint64_t text_length = utils::random_int64(1L, (std::int64_t)max_length);
    for (std::uint64_t j = 0; j < text_length; ++j)
      text[j] = utils::random_int64(0L, 255L);
      //text[j] = 'a' + utils::random_int64(0L, 6L);
    divsufsort(text, (std::int32_t *)sa, text_length);

    for (std::uint64_t i = text_length; i > 0; --i) {
      if (i == text_length) suf_type[i - 1] = 0;              // minus
      else {
        if (text[i - 1] > text[i]) suf_type[i - 1] = 0;       // minus
        else if (text[i - 1] < text[i]) suf_type[i - 1] = 1;  // plus
        else suf_type[i - 1] = suf_type[i];
      }
    }

    // Write all lex-sorted star suffixes to file.
    std::string minus_star_sufs_filename = "tmp." + utils::random_string_hash();
    {
      typedef async_stream_writer<saidx_tt> writer_type;
      writer_type *writer = new writer_type(minus_star_sufs_filename);
      for (std::uint64_t i = 0; i < text_length; ++i) {
        std::uint64_t s = sa[i];
        // BOTH WORK:
        // if (suf_type[s] == 0)
        if (s > 0 && suf_type[s] == 0 && suf_type[s - 1] == 1)
          writer->write((saidx_tt)s);
      }
      delete writer;
    }

    // Create a vector with correct answer (SA).
    std::vector<saidx_tt> v;
    for (std::uint64_t i = 0; i < text_length; ++i) {
      std::uint64_t s = sa[i];
      v.push_back((saidx_tt)s);
    }

    // Run the tested algorithm.
    std::string plus_sufs_filename = "tmp." + utils::random_string_hash();
    std::string sa_filename = "tmp." + utils::random_string_hash();
    std::uint64_t total_io_volume = 0;
    induce_plus_suffixes<chr_t, saidx_tt>(text, text_length, text_length * sizeof(chr_t) + ram_use, minus_star_sufs_filename, plus_sufs_filename, total_io_volume, buffer_size);
    utils::file_delete(minus_star_sufs_filename);
    induce_minus_suffixes<chr_t, saidx_tt>(text, text_length, text_length * sizeof(chr_t) + ram_use, plus_sufs_filename, sa_filename, total_io_volume, buffer_size);
    utils::file_delete(plus_sufs_filename);
    
    // Read the computed output into vector.
    std::vector<saidx_tt> v_computed;
    {
      typedef async_stream_reader<saidx_tt> reader_type;
      reader_type *reader = new reader_type(sa_filename);
      while (!reader->empty())
        v_computed.push_back(reader->read());
      delete reader;
    }

    // Delete output file.
    utils::file_delete(sa_filename);

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

  for (std::uint64_t max_length = 1; max_length <= (1UL << 15); max_length *= 2)
    for (std::uint64_t ram_use = 1; ram_use <= (1UL << 10); ram_use = std::max(ram_use + 1, (ram_use * 5 + 3) / 4))
      for (std::uint64_t buffer_size = 1; buffer_size <= (1UL << 10); buffer_size = std::max(buffer_size + 1, (buffer_size * 5 + 3) / 4))
        test(20, max_length, ram_use, buffer_size);

  fprintf(stderr, "All tests passed.\n");
}