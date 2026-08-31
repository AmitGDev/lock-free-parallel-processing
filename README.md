# lock-free-parallel-processing

A small C++23 demo showing how several threads can write into one shared
buffer **without any locks or atomics**, by partitioning the work so that no
two threads ever touch the same bytes.

## The idea

The demo draws filled rectangles into a `Frame` (a 2D grid of characters).
A rectangle is split into disjoint column segments, one per thread. Because
the segments never overlap, there is nothing to synchronize: no mutex, no
atomics, no contention.

The point is the partitioning, not a lock-free data structure. Correctness
rests on one invariant: **segments must not overlap**. Break it and you have a
data race.

## How it works

### Frame memory layout

A frame is a single heap allocation, so rows, columns and data travel together:

```
+---------------+---------------+--------------------------------+
| rows (size_t) | cols (size_t) | frame data (1 byte per cell)   |
+---------------+---------------+--------------------------------+
```

- Data is stored column by column: cell `(x, y)` lives at `x + y * rows`.
  A column is therefore one contiguous run of bytes, which is what makes the
  per-thread fill a plain `std::ranges::fill` over a `std::span`.
- A fresh frame is filled with `0xFF` ("black"). Drawing writes `0x00`
  ("white").
- Raw pointer access is confined to two private helpers (`HeaderBytes()` and
  `DataBytes()`). Everything else uses `std::span`, so the code stays clean
  under `cppcoreguidelines-pro-bounds-pointer-arithmetic`.

### Work partitioning

`Frame::Draw(rect, thread_count)`:

1. Clamps `thread_count` between 1 and `min(columns to draw,
   hardware_concurrency)`, so you never spawn more threads than there is work
   or hardware for.
2. Splits the columns of the rectangle into `thread_count` contiguous
   segments. The remainder is spread one column at a time over the first
   segments, so sizes differ by at most one.
3. Runs `thread_count - 1` workers as `std::jthread`s and uses the calling
   thread for the last segment, then joins.

Segments are whole columns, so two threads never write to adjacent bytes
except at segment boundaries, which are `rows` bytes apart from any other
thread's hot region. This keeps false sharing negligible.

### Coordinates

A `Rect` is `{x1, y1, x2, y2}`, **inclusive** on both ends. `x` runs along
rows, `y` along columns. `Draw` validates the rectangle (ordering and frame
bounds) before launching any thread and returns `false` if it is invalid.

## What the demo runs

1. **Functionality test**: a small 10 x 15 frame, two rectangles drawn with 2
   threads, and the frame printed after each draw (`0` = white, `1` = black)
   so you can check the result by eye. The per-thread column ranges are
   printed too.
2. **Performance test**: a 600000 x 2000 frame (about 1.2 GB of RAM) and a
   512 MB rectangle drawn with 1, 2, 4, 8 and 12 threads, each timed in
   milliseconds. A plain allocate-and-`memset` of the same size is timed first
   as a baseline for what the memory system itself costs.

## Requirements

- A C++23-compatible compiler and standard library. On Linux, GCC 16 or later.
- About 1.5 GB of free RAM for the performance test

## Build and Run

- [Build on Windows](https://github.com/AmitGDev/cpp-ci-template#build-on-windows)
- [Build on Linux](https://github.com/AmitGDev/cpp-ci-template#build-on-linux)

## Reading the results

Expect speedup to flatten once the memory bus is saturated. This workload is
memory-bound (it only writes bytes), so scaling past a few threads is limited by
bandwidth, not by CPU. Thread counts above your core count are clamped by
`Draw`, so results for 8 and 12 may be identical on smaller machines.

## Code Style

- [C++](https://github.com/AmitGDev/cpp-coding-standard)
- [Python](https://github.com/AmitGDev/cpp-ci-template#python)
- [CMake](https://github.com/AmitGDev/cpp-ci-template#cmake)

## License

MIT License. Copyright © 2024-2026 Amit Gefen.

---

**Last Updated:** 2026-10-22  
**Maintainer:** AmitGDev
