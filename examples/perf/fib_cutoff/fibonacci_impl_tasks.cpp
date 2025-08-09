/*
    Copyright (c) 2005-2020 Intel Corporation

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

#include <cstdio>
#include <cstdlib>

#include "tbb/task_group.h"

namespace impl = tbb::detail::d1;

extern long CutOff;

long SerialFib( const long n ) {
    if( n<2 )
        return n;
    else
        return SerialFib(n-1)+SerialFib(n-2);
}

struct continuation_task : public impl::task {
    continuation_task(std::uint32_t ref_count) : m_ref_count{ref_count}
    {}

    impl::task* release() {
        return (--m_ref_count == 0)? this : nullptr;
    }

    std::atomic<std::uint64_t> m_ref_count;
};

struct fib_continuation : continuation_task {
    fib_continuation(long& s, std::uint32_t ref_count, continuation_task& cont, impl::small_object_allocator& a)
        : continuation_task(ref_count), sum(s), continuation(&cont), alloc(a) {}

    impl::task* finalize(impl::execution_data& ed) {
        impl::task* next = continuation->release();
        alloc.delete_object(this, ed);
        return next;
    }
    impl::task* execute(impl::execution_data& ed) override {
        sum = x + y;
        return finalize(ed);
    }
    impl::task* cancel(impl::execution_data& ed) override {
        return finalize(ed);
    }

    long x{ 0 }, y{ 0 };
    long& sum;
    continuation_task* continuation;
    impl::small_object_allocator alloc;
};

struct fib_computation: public impl::task {
    fib_computation(long n, long* x, continuation_task& cont, impl::small_object_allocator& a)
        : n(n), x(x), continuation(&cont), alloc(a) {}

    impl::task* finalize(impl::execution_data& ed) {
        impl::task* next = continuation->release();
        alloc.delete_object(this, ed);
        return next;
    }
    impl::task* execute(impl::execution_data& ed) override {
        impl::task* next = this; // assume recycling
        if( n < CutOff ) {
            *x = SerialFib(n);
            next = finalize(ed);
        } else {
            auto& c = *alloc.new_object<fib_continuation>(*x, 2, *continuation, alloc);
            auto& b = *alloc.new_object<fib_computation>(n-1, &c.x, c, alloc);
            impl::spawn(b, *ed.context);

            // Recycling
            n -= 2;
            continuation = &c;
            x = &c.y;
        }
        return next;
    }
    impl::task* cancel(impl::execution_data& ed) override {
        return finalize(ed);
    }

    long n;
    long* x;
    continuation_task* continuation;
    impl::small_object_allocator alloc;
};

struct finish_task : continuation_task {
    finish_task(impl::wait_context& w) : continuation_task(1), wctx(w) {}
    impl::task* execute(impl::execution_data&) override {
        wctx.release();
        return nullptr;
    }
    impl::task* cancel(impl::execution_data&) override {
        wctx.release();
        return nullptr;
    }
    impl::wait_context& wctx;
};

long ParallelFib( const long n ) {
    long sum = 0;

    tbb::task_group_context tgctx;
    impl::wait_context wctx(1);
    finish_task finish(wctx);
    impl::small_object_allocator alloc;
    auto start = alloc.new_object<fib_computation>(n, &sum, finish, alloc);
    impl::execute_and_wait(*start, tgctx, wctx, tgctx);

    return sum;
}

