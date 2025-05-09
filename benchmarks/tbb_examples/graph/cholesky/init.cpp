/*
    Copyright (C) 2024-2025 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#include <cstdio>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <string>
#include <fstream>
#include <iostream>

#include <mkl_cblas.h>

static void posdef_gen(double *A, int n) {
    /* Allocate memory for the matrix and its transpose */
    double *L = (double *)calloc(sizeof(double), n * n);
    assert(L);

    double *LT = (double *)calloc(sizeof(double), n * n);
    assert(LT);

    memset(A, 0, sizeof(double) * n * n);

    /* Generate a conditioned matrix and fill it with random numbers */
    for (int j = 0; j < n; ++j) {
        for (int k = 0; k < j; ++k) {
            // The initial value has to be between [0,1].
            L[k * n + j] =
                (((j * k) / ((double)(j + 1)) / ((double)(k + 2)) * 2.0) - 1.0) / ((double)n);
        }

        L[j * n + j] = 1;
    }

    /* Compute transpose of the matrix */
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            LT[j * n + i] = L[i * n + j];
        }
    }
    cblas_dgemm(CblasColMajor, CblasNoTrans, CblasNoTrans, n, n, n, 1, L, n, LT, n, 0, A, n);

    free(L);
    free(LT);
}

// Read the matrix from the input file
void matrix_init(double *&A, int &n, const std::string& fname) {
    if (!fname.empty()) {
        std::ifstream fp{fname, std::ios::in | std::ios::binary};

        if (!fp.is_open()) {
            std::cerr << "\nFile does not exist\n";
            std::exit(0);
        }
        if (!fp.read(reinterpret_cast<char*>(&n), sizeof(n))) {
            throw std::runtime_error("Couldn't read n from " + fname);
        }
        A = (double *)calloc(sizeof(double), n * n);
        // TODO: Investigate how beneficial reading of consecutive memory could be here
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j <= i; ++j) {
                if (!fp.read(reinterpret_cast<char*>(&A[j * n + i]), sizeof(double))) {
                    throw std::runtime_error("Couldn't read A from " + fname);
                }
                if (i != j) {
                    A[i * n + j] = A[j * n + i];
                }
            }
        }
    }
    else {
        A = (double *)calloc(sizeof(double), n * n);
        posdef_gen(A, n);
    }
}

// write matrix to file
void matrix_write(double *A, int n, const std::string& fname) {
    if (!fname.empty()) {
        std::ofstream fp{fname, std::ios::out | std::ios::binary};
        if (!fp.is_open()) {
            std::cerr << "\nCould not open file " << fname << " for writing.\n";
            std::exit(0);
        }
        fp.write(reinterpret_cast<char*>(&n), sizeof(n));
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j <= i; ++j) {
                // TODO: Investigate how beneficial writing of consecutive memory could be here
                fp.write(reinterpret_cast<char*>(&A[j * n + i]), sizeof(double));
            }
        }
    }
}
