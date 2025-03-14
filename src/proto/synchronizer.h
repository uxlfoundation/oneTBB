/*
    Copyright (C) 2005-2011, Intel Corporation

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

#if INTEL_PRIVATE
/* !!! WARNING: !!!
 * Interface of synchronizer has to be changed along with with interfaces of mutexes to comply C++0x concepts.
 * So, current implementation of interface is strictly for usage as template argument of tbb containers.
 */
#endif
#if INTEL_TRIAL==3
#define __TBB_BTS 1
#endif

#ifndef __TBB_synchronizer_H
#define __TBB_synchronizer_H

#include "tbb_stddef.h"
#include "atomic.h"
#include <stdexcept>

#ifndef __TBB_ASSERT_RACEPOINT
#define __TBB_ASSERT_RACEPOINT(a)
#endif
#define __TBB_ASSERT_RACEPOINT_D(s) __TBB_ASSERT_RACEPOINT(("L%02xD%uO", (s.lock_bit<<4)+s.alive_bit, s.owners))

namespace tbb {
//! TBB-wide, all synchronization types
// TODO: 'struct' enables "using tbb::access" and forces access:: before any tag (unless inherited)
//   but 'namespace access' enables "using namespace tbb::access" which doesn't enforces access::
//   but note, there is a global function access() defined by POSIX.1. So, compiler can ask to prefix with tbb::
//   and the workaround is "using tbb::access" in non-global namespaces.
struct access {
    template<typename T>
    struct const_ : T {
        const_(const T &t) : T(t) {}
    };
    //! No access required
    struct none {};
    //! Serial access (not thread-safe)
    struct serial {};
    //! Pure owning with unsafe access
    struct preserve {};
    //! Concurrent access
    struct concurrent {
        bool is_shared;
        //TODO: is_upgradable
        concurrent(bool shared = false) : is_shared(shared) {}
    };
    //! Concurrent access with number of trials
    struct concurrent_with_repeats : concurrent {
        unsigned short ntrials; // to keep size of this structure = 4 bytes
        concurrent_with_repeats(unsigned n, const concurrent &level)
            : concurrent(level), ntrials(static_cast<unsigned short>(n)) {}
    };
    // TODO: concurrent_with_timeout and concurrent_try_upgradable tags
    //! Serial const access (not thread-safe)
    typedef const_<serial> const_serial;
    //! Concurrent const access
    typedef const_<concurrent> const_concurrent;
    //! Concurrent const access with number of trials
    typedef const_<concurrent_with_repeats> const_concurrent_with_repeats;
    //! Exclusive concurrent access
    static concurrent exclusive() { return concurrent(/*is_shared*/false); }
    //! Exclusive concurrent const access
    static const_concurrent const_exclusive() { return const_concurrent(exclusive()); }
    //! Exclusive concurrent access with number of trials
    static concurrent_with_repeats exclusive( unsigned n ) { return concurrent_with_repeats( n, exclusive() ); }
    //! Exclusive concurrent const access with number of trials
    static const_concurrent_with_repeats const_exclusive( unsigned n ) { return const_concurrent_with_repeats(exclusive(n)); }
    //! Shared concurrent access
    static concurrent shared() { return concurrent(/*is_shared*/true); }
    //! Shared concurrent const access
    static const_concurrent const_shared() { return const_concurrent(shared()); }
    //! Shared concurrent access with number of trials
    static concurrent_with_repeats shared( unsigned n ) { return concurrent_with_repeats( n, shared() ); }
    //! Shared concurrent const access with number of trials
    static const_concurrent_with_repeats const_shared( unsigned n ) { return const_concurrent_with_repeats(shared(n)); }
    //! Results of acquisition/locking operation
    enum result_t {//TODO: lock_result_t?
        acquired, busy, timeout, doomed
    };
};

template<typename T>
struct access_tag_traits {
    static const bool is_const = false;
    typedef T mutable_type;
    typedef access::const_<T> const_type;
};
template<>
struct access_tag_traits<access::none> {
    static const bool is_const = true;
    typedef access::none mutable_type;
    typedef access::none const_type;
};
template<>
struct access_tag_traits<access::preserve> {
    static const bool is_const = true;
    typedef access::preserve mutable_type;
    typedef access::preserve const_type;
};
template<typename C>
struct access_tag_traits<access::const_<C> > {
    static const bool is_const = true;
    typedef C mutable_type;
    typedef access::const_<C> const_type;
};
template<typename T>
struct access_tag_traits<const T> {
    static const bool is_const = true;
    typedef T mutable_type;
    typedef access::const_<T> const_type;
};

//! @cond INTERNAL
namespace internal {
    // moving templates with excplicit specializations out of classes

#if __TBB_BTS
    static inline int32_t __fetchbts4(volatile void *ptr, uintptr_t pos)
    {
        int32_t result=0;
        __asm__ __volatile__("lock\nbts %3,%1\n"
                             "adc $0, %0\n"
                              : "=r"(result), "=m"(*(int32_t *)ptr)
                              : "0"(result), "r"(pos), "m"(*(int32_t *)ptr)
                              : "memory");
        return result;
    }
#endif

    // Traits-like class that specifies tags recognized by a synchronizer.
    template<bool RW> struct access_specializator {
        static access::concurrent default_access() { return access::exclusive(); }
        static access::const_concurrent const_access() { return access::const_exclusive(); }
    };
    template<> struct access_specializator<true>  {
        static access::concurrent default_access() { return access::exclusive(); }
        static access::const_concurrent const_access() { return access::const_shared(); }
    };

    template<typename Mutex, bool RW = Mutex::is_rw_mutex>
    struct mutex_handler_base : protected Mutex::scoped_lock {
        mutex_handler_base(bool) {}
        template<typename Synchronizer>
        bool internal_try_lock(Synchronizer &sync) {
            return Mutex::scoped_lock::try_acquire(sync);
        }
        bool internal_is_write() const { return false; }
    };
    template<typename Mutex>
    struct mutex_handler_base<Mutex, true> : protected Mutex::scoped_lock {
        bool is_write; // TODO: can be optimized out by Mutex refactoring
        mutex_handler_base(bool w) : is_write( w ) {}
        template<typename Synchronizer>
        bool internal_try_lock(Synchronizer &sync) {
            return Mutex::scoped_lock::try_acquire(sync, is_write);
        }
        bool internal_is_write() const { return is_write; }
    };

    // Simple memento state based on access type class
    template<typename Access>
    struct memento_state : Access {
        typedef Access access_type;
        memento_state( const Access &a ) : Access(a) {}
        Access get_state() const { return *this; }
    };
    // Mementos for derivatives of access::concurrent must also be derived
    template<> struct memento_state<access::concurrent_with_repeats> : memento_state<access::concurrent> {
        typedef access::concurrent_with_repeats access_type;
        uint16_t ntrials;
        memento_state( const access_type &a ) : memento_state<access::concurrent>(a), ntrials(a.ntrials) {}
        access_type get_state() const { return access::concurrent_with_repeats(ntrials, *this); }
    };

    //common synchronizer base
    struct synchronizer_base {
        typedef uint16_t uint16;
        typedef uint32_t uint32;
        typedef memento_state<access::none> none_state;
        typedef memento_state<access::serial> serial_state;
        typedef memento_state<access::preserve> preserve_state;
        typedef memento_state<access::concurrent> concurrent_state;
        typedef memento_state<access::concurrent_with_repeats> repeats_state;

        template<typename State>
        struct reset_memento : no_assign {
            typedef typename State::access_type access_type;
            State &state;
            reset_memento( State &m ) : state(m) {}
        };
    };
} // namespace internal
//! @endcond

class serial_synchronizer : public internal::synchronizer_base {
public:
    template<typename Access>
    struct memento {
        typedef internal::memento_state<typename access_tag_traits<Access>::mutable_type> state_type;
    };
    struct access_traits {
        static access::serial default_access() { return access::serial(); }
        static access::const_serial const_access() { return access::serial(); }
    };
    typedef void version_type; // access by value is not supported here

    serial_synchronizer() {}
    //! Acquire the lock without contention while constructing
    template<typename Access>
    serial_synchronizer( typename memento<Access>::state_type & ) { }
    void init_lock( preserve_state & ) {}
    access::result_t try_lock( preserve_state & ) { return access::timeout; }
    access::result_t blocking_try_lock( preserve_state & ) { return access::timeout; }
    // Doom *this.  Return true if there are no unreleased "acquires".
    bool unlock( preserve_state & ) { return true; }
    ~serial_synchronizer() {}
};

#if 0 // TODO: change interface to "methodless" memento state types.
//! A synchronizer for any tbb mutex with "simple" deletion protocol.
template<typename Mutex>
class simple_synchronizer : protected Mutex {
    typedef typename Mutex::scoped_lock scoped_t;

    struct handler_base : protected internal::mutex_handler_base<Mutex> {
        //! Create for required access
        handler_base ( bool shared ) : internal::mutex_handler_base<Mutex>(!shared) {}
#if INTEL_PRIVATE
        // TODO: these are not supported yet by mutexes
        //! Move and change access if necessary. empty() returns true if unsuccessful
        handler_base ( handler_base &a, simple_synchronizer &sync );
#endif//INTEL_PRIVATE
        //! Acquire uncontended
        void init_lock( simple_synchronizer &sync ) {
            internal_try_lock( sync ); // TODO: mutex should provide better implementation
        }
        //! Attempt to acquire an access right. Can't block [for a long time]. Returns true if successful; false otherwise
        bool try_lock( simple_synchronizer &sync ) {
            if( !internal_try_lock( sync ) ) {
                // we are unlucky, prepare for longer wait up to n trials
                internal::AtomicBackoff trials;
                do {
                    if( !trials.bounded_pause() )
                        return false;
                } while( !internal_try_lock( sync ) );
            }
            return true;
        }
        //! Return true if all "acquires" have been released and *this is doomed already.
        bool unlock( simple_synchronizer & ) {
            scoped_t::release();
            return false;
        }
    };

public:
    class concurrent_handler : public handler_base {
    public:
        //! Create for required access
        concurrent_handler ( const access::concurrent a ) : handler_base(a.is_shared) {}
        //! Try_acquire() returned false. May block. Complete transaction and decide to retry operation or return.
        access::result_t try_finish_blocking_lock( simple_synchronizer & ) {
            __TBB_Yield();
            return access::busy;
        }
        access::concurrent get_status() const { // synchronizer is free to forget constness of access
            return access::concurrent( handler_base::internal_is_write() );
        }
    };

    class concurrent_with_repeats_handler : public handler_base {
    public:
        //! Create for required access
        concurrent_with_repeats_handler ( const access::concurrent_with_repeats a ) : handler_base(a.is_shared), ntrials(a.ntrials) {}
        //! Try_acquire() returned false. May block. Complete transaction and decide to retry operation or return.
        access::result_t try_finish_blocking_lock( simple_synchronizer & ) {
            __TBB_Yield();
            if( !ntrials > 0) return access::timeout;
            --ntrials;
            return access::busy;
        }
        access::concurrent_with_repeats get_status() const { // synchronizer is free to forget constness
            return access::concurrent_with_repeats( ntrials,
                access::concurrent( handler_base::internal_is_write() ) );
        }
    protected:
        uint16_t ntrials;
    };

    // memento mori.. memento access request/status
    template<typename Access>
    struct memento {
        typedef typename internal::memento_handler<serial_synchronizer,
            typename access_tag_traits<Access>::mutable_type>::type handler_type;
    };

    typedef Mutex mutex_type;
    typedef internal::access_specializator<Mutex::is_rw_mutex> access;
    typedef void version_type; // access by value is not supported here

    simple_synchronizer() {}
    //! Acquire the lock without contention while constructing
    template<typename Access>
    simple_synchronizer(typename memento<Access>::handler_type &ah) {
        ah.init_lock( *this );
    }
    ~simple_synchronizer() {}

    // Doom *this.  Return true if there are no unreleased "acquires".
    bool doom() {
        scoped_t lock( *this ); // acquire exclusively
        return true;
    }
};

// A versioned synchronizer for any tbb mutex with "simple" deletion protocol.
template<typename Mutex>
class simple_versioned_synchronizer : public simple_synchronizer {
public:
    typedef size_t version_type;
    
    void touch_version();
    version_type get_version();
    bool is_locked() const; // non-implementable with current mutexes

protected:
    volatile version_type my_version;
};
#endif//0


//! A versioned mutual exclusive synchronizer with "deferred" deletion protocol.
class versioned_synchronizer : public internal::synchronizer_base {
public:
    struct access_traits {
        typedef access::concurrent default_access_type;
        typedef access::const_concurrent const_access_type;
        static access::concurrent default_access() { return access::exclusive(); }
        static access::const_concurrent const_access() { return access::const_exclusive(); }
        static const bool has_deferred_deletion_support = true;
        static const bool has_multiple_owners_support = true;
        static const bool has_exclusive_access_support = true;
        static const bool has_shared_access_support = false;
        static const bool has_versioned_lock_support = true;
    };
    typedef uint16 version_type;

    // memento state
    template<typename Access>
    struct memento { // do not inherit from memento_state to map const and non-const access to the same state type.
        typedef internal::memento_state<typename access_tag_traits<Access>::mutable_type> state_type;
    };

    // block of common empty methods which can't be inherited due to overloading
    access::result_t try_lock( none_state & ) { return access::timeout; }
    access::result_t try_lock( serial_state & ) { return access::acquired; }//TODO: probably, do atomic-less real locking?
    bool unlock( none_state & ) { return false; }
    bool unlock( serial_state & ) { return false; }
    bool is_locked( none_state & ) { return false; }
    bool is_locked( serial_state & ) { return false; } // TODO:ok???

    //! Acquire a lock without contention
    template<typename State>
    void init_lock( State &memento ) {
        state.init_lock( memento );
    }
    //! Concurrent initialization or resurrection after dooming with acquiring ownership. Returns true if success [and an item can be re-constructed]
    template<typename State>
    access::result_t try_lock( const reset_memento<State> &rmemento ) {
        state_t s = state, old = s;
        if( s.alive_bit ) return access::timeout; // as opposite to doomed
        if( s.lock_bit ) return access::busy;
        __TBB_ASSERT_RACEPOINT_D(s);
        old.lifetime = 0; s.word = 0; s.alive_bit = 1;
        s.init_lock( rmemento.state );
        s.word = as_atomic(state.word).compare_and_swap( s.word, old.word );
        return s.word == old.word ? access::acquired : access::busy;
    }
    //TODO: try_blocking_lock() for reset() ?
    //! Try acquire ownership
    access::result_t try_lock( preserve_state & ) {
        state_t s = state; // no fence, just double-check optimization
        if( !s.alive_bit ) return access::doomed;
        if( s.owners > stop_owners ) return access::busy;
        __TBB_ASSERT_RACEPOINT_D(s);
        s.word = as_atomic(state.word)++;
        if( !s.alive_bit || s.owners > stop_owners ) { // we must decrease it back
            __TBB_ASSERT_RACEPOINT_D(s);
            s.word = --as_atomic(state.word);
            if( s.alive_bit && !s.owners && !s.lock_bit ) {
                // but we don't want to be a doomer, increase again instead of dooming.
                state_t old = s; s.lifetime = 1;
                __TBB_ASSERT_RACEPOINT_D(old);
                s.word = as_atomic(state.word).compare_and_swap( s.word, old.word );
                return s.word == old.word? access::acquired : s.alive_bit ? access::busy : access::doomed;
            }
            __TBB_ASSERT(s.owners < fatal_owners, "Broken synchronizer instance");
            if( s.owners >= fatal_owners )
                throw std::overflow_error("Excess attach operation");
            return s.alive_bit? access::busy : access::doomed;
        }
        return access::acquired;
    }
    //! Unlock ownership, return true if the last owner
    bool unlock( preserve_state & ) {
        state_t s = state; // no fence, just optimization
        __TBB_ASSERT(s.alive_bit, "Doomed before unlocked");
        __TBB_ASSERT_RACEPOINT_D(s);
        if(s.owners == 1 && !s.lock_bit) { // it's the last one, use one atomic op to unset alive_bit also
            state_t old = s; s.lock_bit = 1; s.lifetime = 0; //alive_bit = 0 owners = 0
            s.word = as_atomic(state.word).compare_and_swap( s.word, old.word );
            if( s.word == old.word ) return true; // success, alive_bit is unset by me
            __TBB_ASSERT_RACEPOINT_D(s);
            __TBB_ASSERT(s.alive_bit, "Doomed before unlocked");
        }
        s.word = --as_atomic(state.word);
        __TBB_ASSERT(s.alive_bit, "Doomed before unlocked");// before decrement, nobody would unset alive_bit
        if( !s.owners && !s.lock_bit ) {
            state_t old = s; s.lock_bit = 1; s.lifetime = 0; // alive_bit always set together with lock_bit
            __TBB_ASSERT_RACEPOINT_D(old);
            s.word = as_atomic(state.word).compare_and_swap( s.word, old.word );
            if( s.word == old.word ) return true; // success, alive_bit is unset by me
        }
        else if( s.owners >= fatal_owners ) {
            __TBB_ASSERT(s.owners < fatal_owners, "Broken synchronizer instance");
            as_atomic(state.word)++; // try to fix
            throw std::underflow_error("Excess detach operation");
        }
        return false;
    }
    //! Try acquire exclusive access
    access::result_t try_lock( concurrent_state & ) {
        state_t s; s.word = as_atomic(state.word);
        if( !s.alive_bit ) return access::doomed;
        if( s.lock_bit ) return access::busy;
        // Note, alive_bit always set together with lock_bit
        // but at this point, other threads can doom, destroy, and recycle the object
        __TBB_ASSERT_RACEPOINT_D(s);
#if __TBB_BTS
        if( !internal::__fetchbts4(&state, 16) )
#else
        state_t old = s; s.lock_bit = 1; // using half-word op to avoid contention with owners
        // TODO: add support for architestures without half-word atomics
        // TODO: use bit_test_set() instead
        s.lock_version = as_atomic(state.lock_version).compare_and_swap( s.lock_version, old.lock_version );
        if( s.lock_version == old.lock_version )
#endif
        { // success
            __TBB_ASSERT_RACEPOINT(());
            s = state; // but is it doomed?
            if( s.alive_bit ) return access::acquired;
            else { // revert to unlocked
                s.lock_bit = 0;
                as_atomic(state.lock_version) = s.lock_version;
                return access::doomed;
            }
        } else
            return access::busy;
    }
    //! Unlock access, return true if the last owner
    bool unlock( concurrent_state & ) {
        state_t s; s.lock_version = state.lock_version; // no fence needed
        __TBB_ASSERT(s.lock_bit, "Excess unlock operation");
        if( !s.lock_bit ) {
            throw std::underflow_error("Excess unlock operation");
        }
        __TBB_ASSERT_RACEPOINT(());
        as_atomic(state.lock_version) = s.lock_version + 1; // store with release
        // after this point anything could happen: another lock is acquired, last owner detached...
        __TBB_ASSERT_RACEPOINT(());
        s.word = as_atomic(state.word); // load with acquire TODO: use one full memory barrier instead?
        if( s.alive_bit && !s.owners && !s.lock_bit ) {
            state_t old = s; s.lock_bit = 1; s.lifetime = 0; // alive_bit always set together with lock_bit
            __TBB_ASSERT_RACEPOINT_D(old);
            s.word = as_atomic(state.word).compare_and_swap( s.word, old.word );
            if( s.word == old.word ) return true; // success, alive_bit is unset by me
        }
        return false;
    }
    //! Transform ownership into acquired lock
    bool move_to( concurrent_state &/*dest*/, preserve_state &/*from_expiring*/ ) {
        state_t old, s = state; // no fence, just optimization
        __TBB_ASSERT(s.owners, NULL); __TBB_ASSERT(s.alive_bit, NULL);
        if( !s.lock_bit ) { // try make it by one shot
            old = s; s.lock_bit = 1; s.lifetime--;
            __TBB_ASSERT_RACEPOINT_D(old);
            s.word = as_atomic(state.word).compare_and_swap( s.word, old.word );
            __TBB_ASSERT(s.alive_bit, NULL);
            if( s.word == old.word ) return true; // success, moved
            __TBB_ASSERT(s.owners, NULL);
        }
        //reduce contention by working with lock_bit only
        do { // TODO: !!! use bit_test_and_set instead !!!
            for( internal::atomic_backoff trials; s.lock_bit; s.lock_version = as_atomic(state.lock_version) ) {
                trials.pause(); // wait until unlocked
                __TBB_ASSERT_RACEPOINT(());
            }
            __TBB_ASSERT_RACEPOINT(());
            old.lock_version = s.lock_version; s.lock_bit = 1;
            s.lock_version = as_atomic(state.lock_version).compare_and_swap( s.lock_version, old.lock_version );
        } while( s.lock_version != old.lock_version );
        __TBB_ASSERT_RACEPOINT(());
        as_atomic(state.lifetime)--; // don't bother with !owners, we have lock
        __TBB_ASSERT(state.alive_bit, NULL);
        return true;
    }
    //! Transform acquired lock into ownership
    bool move_to( preserve_state &/*dest*/, concurrent_state &/*from_expiring*/ ) {
        state_t s;
        for(;;) {
            s.word = as_atomic(state.word)++;
            __TBB_ASSERT(s.lock_bit, NULL); __TBB_ASSERT(s.alive_bit, NULL);
            if( s.owners > stop_owners ) { // no enough room, we must decrease it back
                __TBB_ASSERT_RACEPOINT_D(s);
                s.word = --as_atomic(state.word);
                __TBB_ASSERT(s.owners < fatal_owners, "Broken synchronizer instance");
                if( s.owners >= fatal_owners )
                    throw std::overflow_error("Excess attach operation");
                __TBB_Yield(); // continue;
                __TBB_ASSERT_RACEPOINT_D(s);
            } else break;
        }
        __TBB_ASSERT_RACEPOINT_D(s);
        as_atomic(state.lock_version) = s.lock_version + 1; // store with release
        __TBB_ASSERT(state.alive_bit, NULL);
        return true;
    }
    //! Transfer state of the same type
    template<typename State>
    bool move_to( State &dest, State &from_expiring ) {
        dest = from_expiring;// TODO: check, is it necessary?
        return true;
    }
    //! Try acquire exclusive access within N trials
    access::result_t try_lock( repeats_state &rep ) {
        access::result_t r = try_lock( static_cast<concurrent_state&>(rep) );
        if( rep.ntrials ) --rep.ntrials;
        else if( r == access::busy )
            return access::timeout;
        return r;
    }
    //! Try acquire until acquired, timeout or doomed
    template<typename State>
    access::result_t blocking_try_lock( State &memento ) {
        access::result_t r = try_lock( memento );
        if( r == access::busy ) {
            //internal::AtomicBackoff backoff;
            do {
                //backoff.pause();
                __TBB_Yield();
                __TBB_ASSERT_RACEPOINT(());
                r = try_lock( memento );
            } while( r == access::busy );
        }
        return r;
    }
    //! Recycle the synchronizer with doomed state (after execution of destructor of an item). Call then try_lock(reset) for re-initialization
    void recycle() {
        __TBB_ASSERT( state.lock_bit && !state.alive_bit, "recycle() can be used only after unlock() returns access::doomed");
        state.lock_version = 0;
    }
    //! Return special memento type for the first time locking by try_lock
    template<typename State>
    reset_memento<State> reset( State &m ) {
        return reset_memento<State>( m );
    }
    //! Return memento instance for given access (for "auto" of C++0x)
    template<typename Access>
    typename memento<Access>::state_type get_memento( const Access &a ) {
        return typename memento<Access>::state_type( a );
    }
    //! Returns lock version
    version_type get_version() const {
        return const_cast<volatile uint16 &>(state.lock_version); // compiler fence but no HW fence
    }
    //! Returns true if attached by any owner(s)
    bool is_locked( const access::preserve & ) const {
        state_t s; s.lifetime = const_cast<volatile uint16 &>(state.lifetime); // compiler fence but no HW fence
        return s.owners > 0;
    }
    //! Returns true if locked for access
    bool is_locked( const access::concurrent & ) const {
        state_t s; s.lock_version = const_cast<volatile uint16 &>(state.lock_version); // compiler fence but no HW fence
        return s.lock_bit;
    }
    //! Returns true if locked by any sync level except access::preserved
    bool is_locked() const {
        state_t s; s.word = const_cast<volatile uint32 &>(state.word); // compiler fence but no HW fence
        return s.lock_bit;// TODO?: && s.owners;
    }
    //! Returns true if doomed
    bool is_doomed() const {
        state_t s; s.lifetime = const_cast<volatile uint16 &>(state.lifetime); // compiler fence but no HW fence
        return !s.alive_bit;
    }

    explicit versioned_synchronizer(bool alive = true) {
        __TBB_ASSERT( 1024 + stop_owners < fatal_owners && fatal_owners < max_owners, NULL);
        __TBB_ASSERT( &state.lock_version > &state.lifetime, NULL);
        state.word = 0; state.alive_bit = alive;
    }
    //! Acquire the lock without contention while constructing
    template<typename Memento>
    versioned_synchronizer( Memento &m ) { state.word = 0; state.alive_bit = 1; state.init_lock( m ); }
    ~versioned_synchronizer() {}

protected:
    template<typename T>
    static inline atomic<T>& as_atomic( T& t ) {
        return *(atomic<T>*)&t;
    }
    union state_t {
        uint32 word;
        struct {
            uint16 lifetime;
            uint16 lock_version;
        };
        struct {
            uint16 owners : 15;
            uint16 alive_bit : 1;
            uint16 lock_bit : 1;
            uint16 : 15;
        };
        void init_lock( none_state & ) {}
        void init_lock( serial_state & ) {}
        void init_lock( preserve_state & ) { owners = 1; }
        void init_lock( concurrent_state & ) { lock_bit = 1; }
    } mutable state;
    static const uint16 max_owners = (1 << 15)-1;
    static const uint16 fatal_owners = max_owners - 127;
    static const uint16 stop_owners = 3 << 13; // safety margin = (fatal_owners - stop_owners) = ~8000 threads
};

}//namespace tbb

#endif//__TBB_synchronizer_H
