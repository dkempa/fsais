#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <ctime>
#include <unistd.h>

#include "utils.hpp"
#include "io/async_stream_writer_multipart.hpp"
#include "io/async_backward_stream_reader_multipart.hpp"


void test(std::uint64_t max_items, std::uint64_t testcases) {
  fprintf(stderr, "TEST, max_items = %lu, testcases = %li\n", max_items, testcases);
  typedef std::uint32_t value_type;

  // Allocate arrays.
  value_type *tab = new value_type[max_items];
  value_type *tab2 = new value_type[max_items];

  for (std::uint64_t testid = 0; testid < testcases; ++testid) {
    if (testid % 10 == 0)
      fprintf(stderr, "%.2Lf%%\r", (100.L * testid) / testcases);

    // Generate data.
    std::uint64_t items = utils::random_int64(0L, (std::int64_t)max_items);
    for (std::uint64_t i = 0; i < items; ++i)
      tab[i] = utils::random_int64(0L, 255L);

    // Write data to disk.
    std::string filename = "tmp." + utils::random_string_hash();
    std::uint64_t n_parts = 0;
    {
      std::uint64_t filesize = items * sizeof(value_type);
      std::uint64_t part_size = 0;
      std::uint64_t parts = 0;
      do {
        part_size = utils::random_int64(1L, std::max(1L, (std::int64_t)filesize));
        parts = (filesize + part_size - 1) / part_size;
      } while (parts > (1L << 7));

      typedef async_stream_writer_multipart<value_type> writer_type;
      writer_type *writer = NULL;
      {
        std::uint64_t writer_bufsize = utils::random_int64(1L, 50L);
        std::uint64_t writer_n_buffers = utils::random_int64(1L, 5L);
        writer = new writer_type(filename, part_size, writer_bufsize, writer_n_buffers);
      }
      for (std::uint64_t i = 0; i < items; ++i) writer->write(tab[i]);
      n_parts = writer->get_parts_count();
      delete writer;
    }

    // Read data from disk.
    {
      typedef async_backward_stream_reader_multipart<value_type> reader_type;
      std::uint64_t reader_bufsize = utils::random_int64(1L, 50L);
      std::uint64_t reader_n_buffers = utils::random_int64(1L, 5L);
      reader_type *reader = new reader_type(filename, n_parts, reader_bufsize, reader_n_buffers);
      for (std::uint64_t iplus = items; iplus > 0; --iplus)
        tab2[iplus - 1] = reader->read();
      delete reader;
    }

    // Comepare answers.
    if (!std::equal(tab, tab + items, tab2)) {
      fprintf(stderr, "\nError:\n");
      fprintf(stderr, "  tab: ");
      for (std::uint64_t i = 0; i < items; ++i)
        fprintf(stderr, "%lu ", (std::uint64_t)tab[i]);
      fprintf(stderr, "\n");
      fprintf(stderr, "  tab2: ");
      for (std::uint64_t i = 0; i < items; ++i)
        fprintf(stderr, "%lu ", (std::uint64_t)tab2[i]);
      fprintf(stderr, "\n");
      std::exit(EXIT_FAILURE);
    }
  }

  // Clean up.
  delete[] tab;
  delete[] tab2;
}

int main() {
  srand(time(0) + getpid());
  for (std::uint64_t items = 1; items <= (1L << 19); items *= 2)
    test(items, 100);
  fprintf(stderr, "All tests passed.\n");
}

