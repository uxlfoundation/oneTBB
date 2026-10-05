/*
    Copyright (c) 2026 UXL Foundation Contributors

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

#include <oneapi/tbb/parallel_for_each.h>
#include <atomic>
#include <list>

using Item = int;
std::atomic<int> sum{0};

void Foo(const Item& item) {
    sum.fetch_add(item, std::memory_order_relaxed);
}

/*begin_serial_apply_foo*/
void SerialApplyFooToList( const std::list<Item>& list ) {
    for( std::list<Item>::const_iterator i=list.begin(); i!=list.end(); ++i )
        Foo(*i);
}
/*end_serial_apply_foo*/

/*begin_apply_foo*/
class ApplyFoo {
public:
    void operator()( const Item& item ) const {
        Foo(item);
    }
};
/*end_apply_foo*/

/*begin_parallel_apply_foo*/
void ParallelApplyFooToList( const std::list<Item>& list ) {
    oneapi::tbb::parallel_for_each( list.begin(), list.end(), ApplyFoo() );
}
/*end_parallel_apply_foo*/

int main() {
    const std::list<Item> items{1, 2, 3, 4};
    const std::list<Item> empty;

    SerialApplyFooToList(items);
    if (sum.load() != 10) return 1;
    SerialApplyFooToList(empty);
    if (sum.load() != 10) return 1;

    sum.store(0);
    ParallelApplyFooToList(items);
    if (sum.load() != 10) return 1;
    ParallelApplyFooToList(empty);
    return sum.load() == 10 ? 0 : 1;
}
