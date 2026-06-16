#ifndef __TBB_bravo_rw_mutex_H
#define __TBB_bravo_rw_mutex_H

#include "detail/_assert.h"
#include "detail/_utils.h"
#include <atomic>
#include <thread>

namespace tbb {
namespace detail {
namespace d1 {

struct BRAVO_rw_mutex_base {
    using reader_slot_type = std::atomic<BRAVO_rw_mutex_base*>;

    static constexpr std::size_t num_visible_readers = 512;
    static inline constexpr std::size_t slowdown_guard = 9;
    static inline reader_slot_type visible_readers[num_visible_readers];
};

template <typename UnderlyingRWMutex>
class BRAVO_rw_mutex : public BRAVO_rw_mutex_base {
    using underlying_rw_mutex_type = UnderlyingRWMutex;
    using underlying_scoped_lock = typename underlying_rw_mutex_type::scoped_lock;
    using clock_type = std::chrono::steady_clock;

    underlying_rw_mutex_type            m_underlying_rw_mutex;
    alignas(64) std::atomic<bool>                   m_rbias{false};
    std::atomic<clock_type::time_point> m_inhibit_until{};
public:
    static_assert(underlying_rw_mutex_type::is_rw_mutex, "Underlying mutex is not a reader-writer mutex");
    static constexpr bool is_rw_mutex = true;
    static constexpr bool is_recursive_mutex = underlying_rw_mutex_type::is_recursive_mutex;
    static constexpr bool is_fair_mutex = underlying_rw_mutex_type::is_fair_mutex;

    BRAVO_rw_mutex() noexcept = default;
    ~BRAVO_rw_mutex() = default;

    BRAVO_rw_mutex(const BRAVO_rw_mutex&) = delete;
    BRAVO_rw_mutex& operator=(const BRAVO_rw_mutex&) = delete;

    class scoped_lock : public underlying_scoped_lock {
        reader_slot_type*   m_slot{nullptr};
    public:
        constexpr scoped_lock() noexcept = default;

        scoped_lock(BRAVO_rw_mutex& mutex, bool write = true) {
            acquire(mutex, write);
        }
        ~scoped_lock() {
            if (m_slot != nullptr || this->m_mutex != nullptr) {
                release();
            }
        }

        scoped_lock(const scoped_lock&) = delete;
        scoped_lock& operator=(const scoped_lock&) = delete;

        void acquire(BRAVO_rw_mutex& mutex, bool write = true) {
            if (write) {
                // Acquire the underlying mutex first
                underlying_scoped_lock::acquire(mutex.m_underlying_rw_mutex, /*write = */true);

                // If RBias is set, wait for pending fast-path readers to leave
                if (mutex.m_rbias.load(std::memory_order_relaxed)) {
                    // prevent more fast-path readers to lock the mutex
                    mutex.m_rbias.store(false, std::memory_order_relaxed);
                    std::atomic_thread_fence(std::memory_order_seq_cst);
                    
                    auto start = clock_type::now();

                    for (std::size_t i = 0; i < num_visible_readers; ++i) {
                        spin_wait_while_eq(visible_readers[i], &mutex);
                    }

                    auto now = clock_type::now();

                    mutex.m_inhibit_until.store(now + (now - start) * slowdown_guard,
                                                std::memory_order_relaxed);
                }
            } else { // reader
                if (mutex.m_rbias.load(std::memory_order_acquire)) {
                    m_slot = visible_readers + hash(mutex);
                    BRAVO_rw_mutex_base* expected = nullptr;
                    if (m_slot->load(std::memory_order_relaxed) == expected && m_slot->compare_exchange_strong(expected, &mutex)) {
                        if (mutex.m_rbias.load(std::memory_order_acquire)) {
                            // fast-path, acquired
                            this->m_is_writer = false;
                            return;
                        }
                        m_slot->store(nullptr);
                    }
                    m_slot = nullptr;
                }
                // slow-path
                underlying_scoped_lock::acquire(mutex.m_underlying_rw_mutex, /*write = */false);
                if (!mutex.m_rbias.load(std::memory_order_acquire) && 
                    clock_type::now() >= mutex.m_inhibit_until.load(std::memory_order_relaxed))
                {
                    mutex.m_rbias.store(true, std::memory_order_release);
                }
            }
        }

        bool try_acquire(BRAVO_rw_mutex&, bool = true) {
            throw "Unimplemented";
            return false;
        }

        void release() {
            if (this->m_is_writer) {
                underlying_scoped_lock::release();
            } else if (m_slot != nullptr) {
                // fast-path reader
                // __TBB_ASSERT(m_slot == &mutex, nullptr);
                m_slot->store(nullptr, std::memory_order_release);
                m_slot = nullptr;
            } else {
                // slow-path reader
                underlying_scoped_lock::release();
            }
        }

        bool upgrade_to_writer() {
            throw "Unimplemented";
            return false;
        }
        bool downgrade_to_reader() {
            throw "Unimplemented";
            return false;
        }

        static std::uint32_t mix32(std::uint64_t z) {
            z = (z ^ (z >> 33)) * 0xff51afd7ed558ccdull;
            z = (z ^ (z >> 33)) * 0xc4ceb9fe1a85ec53ull;
            return static_cast<std::uint32_t>(z >> 32);
        }

        static std::size_t hash(BRAVO_rw_mutex& mutex) {
            static thread_local std::uint64_t bravo_thread_id_hash = std::hash<std::thread::id>{}(std::this_thread::get_id());

            std::uint64_t z = reinterpret_cast<std::uintptr_t>(&mutex) ^
                              (bravo_thread_id_hash * 0x9E3779B97F4A7C15ull);
            return mix32(z) & (num_visible_readers - 1);
        }
    };
};

} // namespace d1
} // namespace detail
} // namespace tbb

#endif // __TBB_bravo_rw_mutex_H
