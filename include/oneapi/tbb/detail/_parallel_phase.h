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

#ifndef __TBB__parallel_phase_H
#define __TBB__parallel_phase_H

#include <cstdint>
#include <type_traits>

namespace tbb {
namespace detail {
namespace d1 {
namespace phase {

struct start{};
struct end{};
struct tag_base{};

template <typename Boundary, std::uint64_t ID>
struct tag : tag_base {
    using boundary_type = Boundary;
    static constexpr std::uint64_t value = ID;
};

enum tag_ids {
    end_fast_leave = 1
};

template <typename Boundary, typename... Tags>
struct combine_tags;

template <typename Boundary>
struct combine_tags<Boundary> {
    static constexpr std::uint64_t value = 0;
};

template <typename Boundary, typename Tag, typename... Tags>
struct combine_tags<Boundary, Tag, Tags...> {
    static constexpr std::uint64_t value =
       (std::is_same<typename Tag::boundary_type, Boundary>::value ? Tag::value : 0) | combine_tags<Boundary, Tags...>::value;
};

template <typename... Tags>
struct valid_flags : tbb::detail::conjunction<std::is_base_of<tag_base, Tags>...> {};

} // namespace phase
} // namespace d1
} // namespace detail
} // namespace tbb

#endif /* __TBB__parallel_phase_H */
