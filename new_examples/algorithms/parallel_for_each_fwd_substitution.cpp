/*
    Copyright (c) 2024 Intel Corporation

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <atomic>
#include <vector>
#include <tbb/tbb.h>

const int block_size = 512;

void serialFwdSub(std::vector<double>& x, 
                  const std::vector<double>& a, 
                  std::vector<double>& b) {
  const int N = x.size();
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < i; ++j) {
      b[i] -= a[j + i*N] * x[j];
    }
    x[i] = b[i] / a[i + i*N];
  }
}

static inline void 
computeBlock(int N, int r, int c, 
             std::vector<double>& x, 
             const std::vector<double>& a, 
             std::vector<double>& b);

void serialFwdSubTiled(std::vector<double>& x, 
                       const std::vector<double>& a, 
                       std::vector<double>& b) {
  const int N = x.size();
  const int num_blocks = N / block_size;

  for ( int r = 0; r < num_blocks; ++r ) {
    for ( int c = 0; c <= r; ++c ) {
      computeBlock(N, r, c, x, a, b);
    }
  }
}

void parallelFwdSub(std::vector<double>& x, 
                    const std::vector<double>& a, 
                    std::vector<double>& b) {
  const int N = x.size();
  const int num_blocks = N / block_size;

  // create reference counts
  std::vector<std::atomic<char>> ref_count(num_blocks*num_blocks);
  ref_count[0] = 0;
  for (int r = 1; r < num_blocks; ++r) {
    ref_count[r*num_blocks] = 1;
    for (int c = 1; c < r; ++c) {
      ref_count[r*num_blocks + c] = 2;
    }
    ref_count[r*num_blocks + r] = 1;
  }

  using BlockIndex = std::pair<size_t, size_t>;
  BlockIndex top_left(0,0);

  tbb::parallel_for_each( &top_left, &top_left+1, 
    [&](const BlockIndex& bi, tbb::feeder<BlockIndex>& f) {
      auto [r, c] = bi;
      computeBlock(N, r, c, x, a, b);
      // add successor to right if ready
      if (c + 1 <= r && --ref_count[r*num_blocks + c + 1] == 0) {
        f.add(BlockIndex(r, c + 1));
      }
      // add succesor below if ready
      if (r + 1 < (size_t)num_blocks && --ref_count[(r+1)*num_blocks + c] == 0) {
        f.add(BlockIndex(r+1, c));
      }
    }
  );
}

#include <iostream>

static std::vector<double> initForwardSubstitution(std::vector<double>& x, 
                                                   std::vector<double>& a, 
                                                   std::vector<double>& b);
static void warmupTBB();

int main() {
  const int N = 32768;

  std::vector<double> a(N*N);
  std::vector<double> b_serial(N), x_serial(N);
  std::vector<double> b_serial_tiled(N), x_serial_tiled(N);
  std::vector<double> b_tbb(N), x_tbb(N);

  auto x_gold = initForwardSubstitution(x_serial,a,b_serial);
  initForwardSubstitution(x_serial_tiled,a,b_serial_tiled);
  initForwardSubstitution(x_tbb,a,b_tbb);

  double serial_time = 0.0, serial_tiled_time = 0.0, tbb_time = 0.0;
  {
    tbb::tick_count t0 = tbb::tick_count::now();
    serialFwdSub(x_serial,a,b_serial);
    serial_time = (tbb::tick_count::now() - t0).seconds();
  }
  {
    tbb::tick_count t0 = tbb::tick_count::now();
    serialFwdSubTiled(x_serial_tiled,a,b_serial_tiled);
    serial_tiled_time = (tbb::tick_count::now() - t0).seconds();
  }
  warmupTBB();
  {
    tbb::tick_count t0 = tbb::tick_count::now();
    parallelFwdSub(x_tbb,a,b_tbb);
    tbb_time = (tbb::tick_count::now() - t0).seconds();
  }
  for (int i = 0; i < N; ++i) {
    if (x_serial[i] > 2*x_gold[i] || x_serial[i] < 0.5*x_gold[i]) {
        std::cerr << "serial did not validate at " << i << " " << x_serial[i] << " != " << x_gold[i] << std::endl;
    }
    if (x_serial_tiled[i] > 2*x_gold[i] || x_serial_tiled[i] < 0.5*x_gold[i]) {
        std::cerr << "serial tiled did not validate at " << i << " " << x_serial_tiled[i] << " != " << x_gold[i] << std::endl;
    }
    if (x_tbb[i] > 2*x_gold[i] || x_tbb[i] < 0.5*x_gold[i]) {
        std::cerr << "parallel did not validate at " << i << " " << x_tbb[i] << " != " << x_gold[i] << std::endl;
    }
  }
  std::cout << "serial_time == " << serial_time << " seconds" << std::endl;
  std::cout << "serial_tiled_time == " << serial_tiled_time << " seconds" << std::endl;
  std::cout << "tbb_time    == " << tbb_time << " seconds" << std::endl;
  std::cout << "speedup (serial_tiled_time/tbb_time) == " << serial_tiled_time/tbb_time << std::endl;
  return 0;
}

static std::vector<double> initForwardSubstitution(std::vector<double>& x, 
                                                   std::vector<double>& a, 
                                                   std::vector<double>& b) {
  const int N = x.size();
  for (int i = 0; i < N; ++i) {
    x[i] = 0;
    b[i] = i*i;
    for (int j = 0; j <= i; ++j) {
      a[j + i*N] = 1 + j*i;
    }
  }

  std::vector<double> b_tmp = b;
  std::vector<double> x_gold = x;
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < i; ++j) {
      b_tmp[i] -= a[j + i*N] * x_gold[j];
    }
    x_gold[i] = b_tmp[i] / a[i + i*N];
  }
  return x_gold;
}

static inline void 
computeBlock(int N, int r, int c,
             std::vector<double>& x, 
             const std::vector<double>& a, 
             std::vector<double>& b) {
  int i_start = r*block_size, i_end = i_start + block_size;
  int j_start = c*block_size, j_max = j_start + block_size - 1;
  for (int i = i_start; i < i_end; ++i) {
    int j_end = (i <= j_max) ? i : j_max + 1;
    for (int j = j_start; j < j_end; ++j) {
      b[i] -= a[j + i*N] * x[j];
    }
    if (j_end == i) {
      x[i] = b[i] / a[i + i*N];
    }
  }
}

static void warmupTBB() {
  // This is a simple loop that should get workers started.
  // oneTBB creates workers lazily on first use of the library
  // so this hides the startup time when looking at trivial
  // examples that do little real work. 
  tbb::parallel_for(0, tbb::info::default_concurrency(), 
   [=](int) {
      tbb::tick_count t0 = tbb::tick_count::now();
      while ((tbb::tick_count::now() - t0).seconds() < 0.01);
    }
  );
}

