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

#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <stdio.h>
#include <stdlib.h>

#include <tbb/info.h>
#include <tbb/task_arena.h>
#include <tbb/global_control.h>
#include <tbb/parallel_for.h>

using namespace std;

#define HOWMANY 10000

// This program employs a "BBP-type" digit extraction scheme to produce hex digits of pi.
// This code is valid up to ic = 2^24 on systems with IEEE arithmetic.
//
// "BBP-type formulas" for pi originally developed by Simon Plouffe
// https://www.nas.nasa.gov/assets/nas/pdf/techreports/1996/nas-96-016.pdf
// http://www.plouffe.fr/simon/articlepi.html
// https://arxiv.org/ftp/arxiv/papers/2201/2201.12601.pdf
// https://www.davidhbailey.com/dhbpapers/bbp-formulas.pdf
//

class bbpHexPi {
private:
  static const int ntp = 25;
  static constexpr double tp[ntp] = {
	         1.0, //   0
	         2.0, //   1
	         4.0, //   2
	         8.0, //   3
	        16.0, //   4
	        32.0, //   5
	        64.0, //   6
	       128.0, //   7
	       256.0, //   8
	       512.0, //   9
	      1024.0, //  10
	      2048.0, //  11
	      4096.0, //  12
	      8192.0, //  13
	     16384.0, //  14
	     32768.0, //  15
	     65536.0, //  16
	    131072.0, //  17
	    262144.0, //  18
	    524288.0, //  19
	   1048576.0, //  20
	   2097152.0, //  21
	   4194304.0, //  22
	   8388608.0, //  23
	  16777216.0  //  24
  };


public:
//
// ------- Compute 16^p mod ak
// This routine uses the left-to-right binary exponentiation scheme.
// It is valid for  ak <= 2^24.
//
  double expm(double p, double ak) {
    double p1, pt, r;
    int i;

    if (ak == 1.0)  return 0.0;

    for (i = 0; tp[i] <= p; i++)  ; // Find the greatest power of two <= p.
    pt = tp[i-1];
    p1 = p;
    r = 1.0;

    while (pt >= 1.0) { // Perform binary exponentiation algorithm modulo ak.
      if (p1 >= pt) {
	r = fmod( 16.0 * r, ak);
	p1 -= pt;
      }
      pt *= 0.5;
      if (pt >= 1.0)
	r = fmod(r * r, ak);
    }
    return fmod(r, ak);
  } // expm

  //
  // Evaluate the series sum_k 16^(ic-k)/(8*k+m)
  // using the modular exponentiation technique.
  //
  double series(int m, long ic) {
    const double eps = 1.0e-17;
    double ak, p, s, t;
    long k;

    s = 0.0;
    for (k = 0; k < ic; k++) { // Sum the series up to ic.
      ak = 8 * k + m;
      p = ic - k;
      t = expm(p, ak);
      s += t / ak;
      s -= floor(s);
    }
    p = 16.0;  t = 1.0;
    for (k = ic; t > eps; k++) { // Compute a few terms where k >= ic.
      ak = 8 * k + m;
      p /= 16.0;
      t = p / ak;
      s += t;
      s -= floor(s);
    }
    return s;
  } // series

  //
  // Compute four hex digits starting at position pos
  //
  unsigned EightHexPiDigits(int startposition) {
    const int octo8 = 8;
    long fractionalposition;
    double pid, s1, s2, s3, s4, y;
    unsigned result = 0;

    fractionalposition = startposition - 1;

    if (!startposition)
      return 0x3243f6a8;

    if (startposition < 0)
      return 0xFEFEFEFE;

    if (fractionalposition > 16777216L)
      return 0xBEBEBEBE;

    s1 = series(1, fractionalposition);
    s2 = series(4, fractionalposition);
    s3 = series(5, fractionalposition);
    s4 = series(6, fractionalposition);
    pid = fmod( 4.0 * s1 - 2.0 * s2 - s3 - s4, 1.0);

    if (pid < 0)
      pid += 1.0;

    y = fabs(pid);

    for (int i = 0; i < octo8; i++) {
      y = 16.0 * (y - floor(y));
      result = (result << 4) + ((unsigned) y);
    }

    return result;
  }
}; // Class bbpHexPi

constexpr double bbpHexPi::tp[bbpHexPi::ntp];

// The existance of "BBP-type formulas" for mathematical constants
// were first discovered for pi by Simon Plouffe.
// This program employs his "BBP-type" digit extraction scheme to
// produce hex digits of pi.
// Interested related papers include:
//  https://www.davidhbailey.com/dhbpapers/bbp-formulas.pdf
//  https://www.nas.nasa.gov/assets/nas/pdf/techreports/1996/nas-96-016.pdf
//  http://www.plouffe.fr/simon/articlepi.html
//  https://arxiv.org/ftp/arxiv/papers/2201/2201.12601.pdf

int main(int argc, char **argv)
{
  auto values = std::vector<unsigned>(HOWMANY);

  for ( auto idx = 1; idx < 225; idx++ ) {
    // Limit the number of threads to two for all oneTBB parallel interfaces
    tbb::global_control global_limit(tbb::global_control::max_allowed_parallelism, idx);

    bbpHexPi bbp;

    auto t1 =
      chrono::steady_clock::now();  // Start timing
    tbb::parallel_for(tbb::blocked_range<int>(0,values.size()),
		      [&](tbb::blocked_range<int> r) {
			for (int i=r.begin(); i<r.end(); ++i) {
			  values[i] = bbp.EightHexPiDigits(i*8);
			}
		      });
    auto t2 =
      chrono::steady_clock::now();  // Start timing
    double timed =
      (chrono::duration_cast<chrono::microseconds>(t2 - t1).count());

    for (unsigned eightdigits : values)
      printf("%.8x", eightdigits);

    printf("\n%4d Time %20.0f\n",idx,timed);

  }

  return 0;
}
