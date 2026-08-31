/*
    main.cpp
    Copyright (c) 2024-2026, Amit Gefen

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal in the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <format>
#include <iostream>
#include <memory>
#include <new>
#include <ranges>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

constexpr double kBase1024 = 1024.0;
constexpr unsigned char kColorBlack = 0xFF;
constexpr unsigned char kColorWhite = 0x00;

// Utility__

// Get the current time.
static auto Now() {
  return std::chrono::steady_clock::now();
}

// Print the duration since a given start time.
static void PrintDuration(
    const std::chrono::time_point<std::chrono::steady_clock> start_time) {
  const auto duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(Now() - start_time);
  std::cout << "(execution time: " << duration_ms.count() << " milliseconds)"
            << '\n';
}

// Format a large character count with appropriate units (K, M, G, etc.)
static std::string FormatCharCount(const uint64_t char_count) {
  constexpr int kMaxUnitIndex = 8;
  static constexpr std::array<const char*, kMaxUnitIndex + 1> kUnits = {
      "", "K", "M", "G", "T", "P", "E", "Z", "Y",
  };

  // Calculate the appropriate unit index based on the number of characters:
  // (std::min ensures the calculated unit index doesn't exceed the bounds of
  // the units array.)
  const int unit_index =
      std::min(static_cast<int>(std::log2(char_count) / 10), kMaxUnitIndex);

  // Format the number with the appropriate unit:
  return unit_index == 0 ? std::format("{}", char_count)
                         : std::format("{}{}",
                                       static_cast<double>(char_count) /
                                           std::pow(kBase1024, unit_index),
                                       kUnits.at(unit_index));
}

// __Utility

namespace {

// clang-format off
//	Frame class: Represents a rectangular frame of characters.
//
//	buffer_ (std::unique_ptr<char[]>)                                  <-- Pointer to dynamically allocated memory.
//	+-------------------------------+-------------------------------|
//	|                               |                               |  <-- Rows (size_t). See: GetRows().
//	+-------------------------------+-------------------------------|
//	|                               |                               |  <-- Cols (size_t). See: GetCols().
//	+-------------------------------+-------------------------------|
//	|       |       |       |       |       |       |       |       |  <-- Frame data (characters). See: GetDataIndex()
//	+-------+-------+-------+-------+-------+-------+-------+-------|
//	| ...   | ...   | ...   | ...   | ...   | ...   | ...   | ...   |
//	+-------+-------+-------+-------+-------+-------+-------+-------|
//
//	buffer_: Pointer to a dynamically allocated memory block holding the frame data.
//	- The first `sizeof(size_t)` bytes (typically 8 bytes on modern systems) store the number of rows.
//	- The next `sizeof(size_t)` bytes (typically 8 bytes on modern systems) store the number of columns.
//	- The remaining memory stores the frame data, with each character ccupying 1 byte.
// clang-format on

class Frame final {
 public:
  // Struct to define a rectangle within the frame:
  struct Rect final {
    size_t x1{0}, y1{0}, x2{0}, y2{0};
  };

  // Constructor to create a frame with given dimensions:
  Frame(const size_t rows, const size_t cols) {
    [[maybe_unused]] const auto create_ok{Create(rows, cols)};
  }

  // Draw "White" if frame with optimized_n worker-threads.
  [[nodiscard]] bool Draw(const Rect& rect,
                          const size_t thread_count = 1) const {
    const size_t cols_to_draw{(rect.y2 - rect.y1) + 1};

    // Dynamic thread count optimization:
    const size_t optimized_thread_count =
        std::clamp(thread_count, static_cast<size_t>(1),  // Min.
                   std::min(cols_to_draw,
                            static_cast<size_t>(
                                std::thread::hardware_concurrency())));  // Max.

    std::string note{"main-thread"};
    if (optimized_thread_count > 1) {
      note = "threads: " + std::to_string(optimized_thread_count - 1) +
             " worker threads + " + note;
    }

    const auto chars =
        FormatCharCount((rect.x2 - rect.x1 + 1) * (rect.y2 - rect.y1 + 1));
    std::cout << "draw (" << note << ") (x1-y1: " << rect.x1 << "-" << rect.y1
              << ", x2-y2: " << rect.x2 << "-" << rect.y2
              << ", total: " << chars << " chars)" << '\n';

    if (!DrawSanityChecks(rect)) {
      std::cerr << "error: Draw() sanity check failed." << '\n';

      return false;
    }

    const auto start_time = Now();     // <-- Start.

    if (optimized_thread_count > 1) {  // Run with worker-threads:
      std::vector<std::pair<size_t, size_t>> segments(optimized_thread_count);
      PrepareSegments(cols_to_draw, segments);

      std::vector<std::jthread> threads;
      threads.reserve(optimized_thread_count - 1);

      // (optimized_thread_count - 1) worker threads:
      for (unsigned int i = 0; i < optimized_thread_count - 1; ++i) {
        // This uses the default capture mode (&), capturing all variables by
        // reference, except i, which is captured by value.
        threads.emplace_back([&, i] { DrawThread(rect, segments.at(i)); });
      }

      // + [main thread]:
      DrawThread(rect, segments.at(optimized_thread_count - 1));

      for (auto& thread : threads) {
        thread.join();
      }
    } else {  // Run with main-thread:
      DrawThread(rect, {0, rect.y2 - rect.y1});
    }

    PrintDuration(start_time);  // <-- Finish.

    return true;
  }

  // Print the frame.
  // This is mainly for debug / demo.
  // Usefull on small frame (~ up to 100 rows).
  [[nodiscard]] bool PrintFrame() const {
    if (buffer_ == nullptr) {  // Create() failed.
      std::cerr << "error: PrintFrame() frame buffer is nullptr." << '\n';

      return false;
    }

    const size_t rows = GetRows();
    const std::span<const char> data = DataBytes();

    std::cout << "frame" << '\n';
    for (size_t index = 0; index < GetCols(); ++index) {
      const auto character_span = data.subspan(index * rows, rows);
      std::ranges::for_each(character_span, [](char character) {
        std::cout << ((character == 0) ? '0' : '1');
      });
      std::cout << '\n';
    }

    return true;
  }

 private:
  // Assign a "segment" of cols_to_draw for each thread (so we don't need thread
  // syncronization):
  static void
  PrepareSegments(const size_t cols_to_draw,
                  std::vector<std::pair<size_t, size_t>>& segments) {
    // Calculate the size of each segment to evenly distribute the drawing
    // workload among them. Calculate the remainder to handle cases where the
    // number of columns to draw is not evenly divisible by the number of
    // chunks.
    const size_t segment_size{cols_to_draw / segments.size()};
    const size_t remainder = cols_to_draw % segments.size();

    // Set the size of each segment based on the calculated segment size and
    // distribute the remaining workload.
    for (size_t index = 0; index < segments.size(); ++index) {
      auto& segment = segments.at(index);
      segment.first = segment_size;
      if (index < remainder) {
        ++segment.first;
      }
    }

    // Adjust the 'from' and 'to' indices of each segment to properly define
    // their boundaries.
    size_t temp{0};
    for (auto& segment : segments) {
      segment.second = (segment.first - 1) + temp;
      segment.first = temp;
      temp = segment.second + 1;
    }

    // Print segment loads:
    // Create a 1-based index view and zip it with segments
    for (const auto& [index, segment] :
         std::views::zip(std::views::iota(1), segments)) {
      std::cout << "* thread " << index << ": col " << segment.first << " - "
                << segment.second << '\n';
    }
  }

  // Check that Draw() is feasible.
  [[nodiscard]] bool DrawSanityChecks(const Rect& rect) const {
    return (rect.x1 >= 0 && rect.y1 >= 0  // x1-y1 negative.
            && rect.x2 >= rect.x1 &&
            rect.y2 >= rect.y1            // x1-y1 is beyond x2-y2.
            && rect.x2 <= GetRows() - 1 &&
            rect.y2 <= GetCols() - 1      // x2-y2 is exceeds frame.
            && buffer_ != nullptr);       // Create() failed.
  }

  // (Run in the context of multiple threads; no syncronization! - SEGMENTS
  // SHOULD NOT OVERLAP!) Draw segment. segment is col from - to offsets
  // (*relative to rect.y1*).
  void DrawThread(const Rect& rect,
                  const std::pair<size_t, size_t> segment) const {
    // Hoisted out of the loop: both read the buffer header on every call.
    const size_t rows = GetRows();
    const std::span<char> data = DataBytes();

    for (size_t index = segment.first; index <= segment.second; ++index) {
      // Offset inside the data area; if y1 == 0, just x1.
      const size_t start_p = rect.x1 + ((index + rect.y1) * rows);
      // If x2 == x1, 1 char.
      const size_t size_for_memset = (rect.x2 - rect.x1) + 1;

      // (Each thread writes to an exclusive segments of the buffer => No need
      // for mutex.)
      std::ranges::fill(data.subspan(start_p, size_for_memset),
                        kColorWhite);  // Draw "White" (kColorWhite).
    }
  }

  // Create a blank frame.
  [[nodiscard]] bool Create(const size_t rows, const size_t cols) {
    // If rows and/or cols 0, return false. buffer_ stays nullptr.
    if (rows <= 0 || cols <= 0) {
      std::cerr << "error: Create() rows and/or cols 0." << '\n';

      return false;
    }

    try {
      const size_t buffer_size{GetDataIndex() + (cols * rows)};
      buffer_ = std::unique_ptr<char[]>(
          new char[buffer_size]);  // std::make_unique<char[]>(buffer_size); (As
                                   // of C++20, std::make_unique does not
                                   // support dynamic arrays)

      // Whole-allocation view, so the writes below are span operations and not
      // raw pointer arithmetic.
      const std::span<char> bytes{buffer_.get(), buffer_size};

      // Embeds rows and cols into buffer_:
      std::memcpy(bytes.data(), &rows, sizeof(size_t));
      std::memcpy(bytes.subspan(sizeof(size_t)).data(), &cols, sizeof(size_t));

      // Initialize empty frame:
      std::ranges::fill(bytes.subspan(GetDataIndex()),
                        kColorBlack);  // Draw "Black" (kColorBlack).

      std::cout << "create frame (rows: " << rows << ", cols: " << cols << ")"
                << '\n';
    } catch ([[maybe_unused]] const std::bad_alloc& e) {
      // buffer_ stays nullptr.
      return false;
    }

    return true;
  }

  // Buffer indicators__

  // Extract rows from the buffer.
  [[nodiscard]] size_t GetRows() const {
    if (buffer_ == nullptr) {  // Create() failed.
      std::cerr << "error: GetRows() frame buffer is nullptr." << '\n';

      return 0;
    }

    size_t rows{0};
    std::memcpy(&rows, HeaderBytes().data(), sizeof(size_t));

    return rows;
  }

  // Extract cols from the buffer .
  [[nodiscard]] size_t GetCols() const {
    if (buffer_ == nullptr) {  // Create() failed.
      std::cerr << "error: GetCols() frame buffer is nullptr." << '\n';

      return 0;
    }

    size_t cols{0};
    std::memcpy(&cols, HeaderBytes().subspan(sizeof(size_t)).data(),
                sizeof(size_t));

    return cols;
  }

  // Return the index where the frame begins.
  [[nodiscard]] static constexpr size_t
  GetDataIndex()  // The result is computed during compilation.
  {
    return sizeof(size_t) * 2;
  }

  // __Buffer indicators

  // Views over the buffer. This is the single place where the raw pointer meets
  // a size, so callers get span operations instead of pointer arithmetic.
  // Callers must make sure Create() succeeded.

  // The [rows][cols] header.
  [[nodiscard]] std::span<char> HeaderBytes() const {
    return std::span<char>{buffer_.get(), GetDataIndex()};
  }

  // The frame data (everything after the header).
  [[nodiscard]] std::span<char> DataBytes() const {
    if (buffer_ == nullptr) {  // Create() failed.
      return {};
    }

    const size_t total_size = GetDataIndex() + (GetRows() * GetCols());

    return std::span<char>{buffer_.get(), total_size}.subspan(GetDataIndex());
  }

  std::unique_ptr<char[]> buffer_;  // [rows][cols][....frame data....]
};

}  // namespace

// Let's visually confirm that it is functioning correctly.
static void TestFunctionality() {
  std::cout << "**** test functionality: small frame + small draw + print "
               "frame: ****\n"
            << '\n';
  ;

  // XY frame sizes.
  const std::vector<std::pair<size_t, size_t>> xy_frames{
      {10, 15},  // Possible to add here more xy_frames.
  };

  // Rects to draw inside each xy_frame.
  const auto rects = std::to_array<Frame::Rect>({
      {.x1 = 1, .y1 = 1, .x2 = 3, .y2 = 2},
      {.x1 = 5, .y1 = 1, .x2 = 8, .y2 = 13},
  });

  for (const auto& xy_frame : xy_frames) {  // For each frame size
    // Create frame
    const Frame frame{xy_frame.first, xy_frame.second};  // Create
    // Draw rects and print frame
    for (const auto& rect : rects) {  // For each rect
      std::cout << '\n';
      [[maybe_unused]] const auto result1 = frame.Draw(rect, 2);  // Draw
      std::cout << '\n';
      [[maybe_unused]] const auto result2 = frame.PrintFrame();   // Print
      std::cout << '\n';
    }
  }
}

// Let's assess the performance on a very large frame with a different thread
// count.
static void TestPerformance() {
  std::cout << "**** test performance (hardware concurrency: "
            << std::thread::hardware_concurrency()
            << "): large frame + large draw: ****\n"
            << '\n';

  constexpr size_t kFrameRows = 600000;
  constexpr size_t kFrameCols = 2000;

  // 512MB frame draw:
  constexpr size_t kDrawRows = 524288;  // 512KB
  constexpr size_t kDrawCols = 1024;    // 1KB

  constexpr std::array<uint8_t, 5> kThreadCounts = {1, 2, 4, 8, 12};

  constexpr Frame::Rect kDrawRect{
      .x1 = 1,
      .y1 = 1,
      .x2 = kDrawRows,
      .y2 = kDrawCols,
  };

  const Frame frame{kFrameRows, kFrameCols};
  std::cout << '\n';

  {
    // Cast sub-expr. to a wider type.
    const size_t buffer_size = static_cast<size_t>(kDrawRows) * kDrawCols;

    std::cout << "benchmark: std::memset of " << buffer_size << " bytes"
              << '\n';

    const auto buffer = std::unique_ptr<char[]>(new char[buffer_size]);

    // Baseline for the Draw() runs below, so it has to measure the same thing:
    // writing to memory that is already mapped. A freshly allocated buffer is
    // only reserved address space; the OS maps each page on its first write
    // (page fault). Frame::Create() has already paid that cost for the frame
    // that Draw() writes to, so timing a first write here would inflate the
    // baseline and make the Draw() times look better than they are.
    // Hence: touch every page once, untimed, and time only the second pass.
    std::memset(buffer.get(), '1', buffer_size);  // Warm-up (untimed).

    // Start
    const auto start_time = Now();

    // Benchmark:
    std::memset(buffer.get(), '0', buffer_size);

    PrintDuration(start_time);  // <-- Finish.
    std::cout << '\n';
  }

  for (const auto thread_count : kThreadCounts) {
    [[maybe_unused]] const bool done = frame.Draw(kDrawRect, thread_count);
    std::cout << '\n';
  }
}

int main() {
  try {
    TestFunctionality();
    std::cout << '\n';
    TestPerformance();
    return EXIT_SUCCESS;
  } catch (const std::exception& e) {
    // fputs instead of std::println: the error path must not throw again.
    std::fputs(e.what(), stderr);
    std::fputs("\n", stderr);
    return EXIT_FAILURE;
  }
}