/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#pragma once

#include <omp.h>
#include <tbb/task_group.h>
#include <tbb/task_arena.h>
#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>

#include <iostream>
#include <cstring>
#include <cassert>

namespace tcm {
namespace detail {
    struct arena_serial_impl {
        template <typename F>
        void bulk_execute(int start, int end, F&& f) const {
            for (int i = start; i < end; ++i) {
                f(i);
            }
        }

        template <typename Range, typename F>
        void bulk_execute(const Range& range, F &&f) const {
            for (int i = range.begin(); i < range.end(); ++i) {
                f(i);
            }
        }

        template <typename F>
        void execute(F &&f) const {
            f();
        }

        std::size_t max_concurrency() const {
            return 1;
        }

        static std::string name() {
            return "serial";
        }
    };

    class arena_tbb_impl {
        using pool_t = tbb::task_arena;
        using pool_ptr_t = std::unique_ptr<pool_t>;

        pool_ptr_t p_;
        const int32_t max_concurrency_;
    public:
        arena_tbb_impl(int32_t max_concurrency = tbb::task_arena::automatic)
            : max_concurrency_(max_concurrency)
        {
            p_.reset(new pool_t(max_concurrency_));
        }

        arena_tbb_impl(const tbb::task_arena::constraints& constraints) 
        : max_concurrency_(tbb::info::default_concurrency(constraints))
        {
            p_.reset(new pool_t(constraints));
        }

        ~arena_tbb_impl() = default;

        template <typename F, typename Partitioner>
        void bulk_execute(int start, int end, F&& f, Partitioner&& partitioner = tbb::auto_partitioner{}) const {
            p_->execute([start, end, &f, &partitioner]() {
                tbb::parallel_for(start, end, std::forward<F>(f), partitioner);
            });
        }

        template <typename Range, typename F, typename Partitioner>
        void bulk_execute(const Range& range, F&& f, Partitioner&& partitioner = tbb::auto_partitioner{}) const {
            p_->execute([&range, &f, &partitioner]() {
                tbb::parallel_for(range, std::forward<F>(f), partitioner);
            });
        }

        std::size_t max_concurrency() const {
            return max_concurrency_;
        }

        template <typename F>
        void execute(F &&f) const {
            p_->execute([f]() { f(); });
        }

        static std::string name() {
            return "tbb";
        }
    };

    class arena_omp_impl {
        const uint32_t max_concurrency_;
    public:
        arena_omp_impl(uint32_t max_concurrency = 0)
            : max_concurrency_(max_concurrency) {}

        ~arena_omp_impl() = default;

        template <typename F>
        void bulk_execute(int start, int end, F &&f) {
            uint32_t thds = max_concurrency_;
            #pragma omp parallel for if (thds > 0) num_threads(max_concurrency_)
            for (int i = start; i < end; ++i) {
                f(i);
            }
        }

        template <typename F>
        void execute(F &&f) {
            uint32_t thds = max_concurrency_;

            #pragma omp parallel if (thds > 0) num_threads(thds)
            #pragma omp single
            f();
        }

        std::size_t max_concurrency() const {
            return max_concurrency_;
        }

        static std::string name() {
            return "omp";
        }
    };
} // namespace detail

    template <typename I>
    struct arena_type {
        arena_type() : impl_(std::make_shared<I>()) {}

        arena_type(const arena_type &a) : impl_(a.impl_) {}
        arena_type(arena_type &a) : arena_type(const_cast<const arena_type &>(a)) {}

        template <typename... Args>
        arena_type(Args &&...args) : impl_(std::make_shared<I>(std::forward<Args>(args)...)) {}

        arena_type &operator=(const arena_type &a) { impl_ = a.impl_; }
        ~arena_type() {}

        template <typename F, typename... Args>
        void bulk_execute(int n, F&& f, Args&&... args) const {
            bulk_execute(0, n, std::forward<F>(f), std::forward<Args>(args)...);
        }

        template <typename F, typename... Args>
        void bulk_execute(int start, int end, F &&f, Args&&... args) const {
            impl_->bulk_execute(start, end, std::forward<F>(f), std::forward<Args>(args)...);
        }

        template <typename Range, typename F, typename... Args,
            typename std::enable_if<std::is_integral<Range>::value == false, bool>::type = true >
        void bulk_execute(const Range& range, F &&f, Args&&... args) const {
            impl_->bulk_execute(range, std::forward<F>(f), std::forward<Args>(args)...);
        }

        template <typename F>
        void execute(F &&f) const {
            impl_->execute(std::forward<F>(f));
        }

        std::size_t max_concurrency() const {
            return impl_->max_concurrency();
        }

        static std::string name() {
            return I::name();
        }

        template <typename... Args>
        static arena_type make_client(Args&&... args) {
            return arena_type{ std::forward<Args>(args)... };
        }
    private:
        std::shared_ptr<I> impl_;
    };

    using serial_client = arena_type<detail::arena_serial_impl>;
    using omp_client = arena_type<detail::arena_omp_impl>;
    using tbb_client = arena_type<detail::arena_tbb_impl>;

} // namespace tcm
