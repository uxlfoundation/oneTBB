/*
   Copyright (c) 2023 Intel Corporation

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include "tcm/detail/_tcm_assert.h"
#include "tcm/detail/hwloc_utils.h"
#include "tcm/detail/_environment.h"
#include "tcm.h"
#include "tcm_permit_rep.h"

#if __linux__
#include "linux/cgroup_info.h"
#endif

#include "tracing/_tracer.h"

// MSVC Warning: unreferenced formal parameter
__TCM_SUPPRESS_WARNING_WITH_PUSH(4100)
#include "hwloc.h"
__TCM_SUPPRESS_WARNING_POP

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <vector>
#include <queue>
#include <stack>

#if __TCM_ENABLE_TRACER
#include <iostream>
#endif

namespace tcm {
namespace internal {

std::stack<tcm_permit_handle_t>& get_active_permit_container() {
    thread_local std::stack<tcm_permit_handle_t> tls_active_permit;
    return tls_active_permit;
}

/**
 *******************************************************************************
 * Permit state helpers
 *******************************************************************************
 */

#if TCM_USE_ASSERT
bool is_void(const tcm_permit_state_t& state) {
    return TCM_PERMIT_STATE_VOID == state;
}
#endif

bool is_active(const tcm_permit_state_t& state) {
    return TCM_PERMIT_STATE_ACTIVE == state;
}

bool is_inactive(const tcm_permit_state_t& state) {
    return TCM_PERMIT_STATE_INACTIVE == state;
}

bool is_idle(const tcm_permit_state_t& state) {
    return TCM_PERMIT_STATE_IDLE == state;
}

bool is_pending(const tcm_permit_state_t& state) {
    return TCM_PERMIT_STATE_PENDING == state;
}

bool is_owning_resources(const tcm_permit_state_t& state) {
    return is_active(state) || is_idle(state);
}

bool is_rigid_concurrency(const tcm_permit_flags_t& flags) {
    return flags.rigid_concurrency;
}

bool is_negotiable(const tcm_permit_state_t& state, const tcm_permit_flags_t& flags) {
    if (is_active(state) && is_rigid_concurrency(flags)) {
        return false;
    }
    return true;
}

bool is_constrained(const tcm_permit_handle_t ph) {
    return bool(ph->request.cpu_constraints);
}

bool is_nested(const tcm_permit_handle_t ph,
               std::memory_order order = std::memory_order_relaxed)
{
    return ph->data.is_nested.load(order);
}

void change_state_relaxed(tcm_permit_data_t& permit_data, tcm_permit_state_t state) {
    // prepare and commit permit modification calls are not needed because only single atomic
    // variable is changed.
    permit_data.state.store(state, std::memory_order_relaxed);
}

inline tcm_permit_state_t get_permit_state(const tcm_permit_data_t& permit_data,
                                           std::memory_order order = std::memory_order_relaxed) {
    return permit_data.state.load(order);
}

/**
 * Determines whether placing a permit data structure in internals can be avoided because the
 * permit's data is not considered further directly, but had been reflected in other invariants.
 *
 * Example: Unconstrained rigid concurrency permits cannot be negotiated while in ACTIVE state.
 */
bool participates_in_subscription_compute(const tcm_permit_handle_t ph) {
    return !is_rigid_concurrency(ph->data.flags) || is_constrained(ph);
}

/**
 *******************************************************************************
 * End of permit state helpers
 *******************************************************************************
 */

int get_mask_concurrency(const tcm_cpu_mask_t& mask) {
    __TCM_ASSERT(mask, "Existing mask is expected.");
    int mc = hwloc_bitmap_weight(mask);
    return mc;
}

float tcm_oversubscription_factor();

int get_oversubscribed_mask_concurrency(const tcm_cpu_mask_t &mask,
                                        float oversubscription_factor = tcm_oversubscription_factor())
{
    return int(get_mask_concurrency(mask) * oversubscription_factor);
}


bool sum_constraints_bounds(int32_t& sum_min, int32_t& sum_max, const tcm_permit_request_t& request)
{
    __TCM_ASSERT(request.cpu_constraints, "Nothing to sum up from.");
    bool is_request_sane = true;
    sum_min = sum_max = 0;
    int32_t adjusted_max_initializer = request.max_sw_threads;
    if (tcm_automatic == adjusted_max_initializer) {
        adjusted_max_initializer = 0;
    }
    for (uint32_t i = 0; i < request.constraints_size; ++i) {
        const tcm_cpu_constraints_t& c = request.cpu_constraints[i];
        int32_t adjusted_min = 0;
        if (c.min_concurrency != tcm_automatic) {
            if (c.min_concurrency < 0 || std::numeric_limits<int32_t>::max() - sum_min < c.min_concurrency) {
                is_request_sane = false;
                break;
            }
            adjusted_min = c.min_concurrency;
        }
        sum_min += adjusted_min;

        int32_t adjusted_max = adjusted_max_initializer;
        if (c.max_concurrency != tcm_automatic) {
            if (c.max_concurrency < 0 || std::numeric_limits<int32_t>::max() - sum_max < c.max_concurrency) {
                is_request_sane = false;
                break;
            }
            adjusted_max = c.max_concurrency;
        } else if (c.mask) {
            const int32_t mask_concurrency = get_mask_concurrency(c.mask);
            if (mask_concurrency > 0) {
                adjusted_max = mask_concurrency;
            }
        } else if (tcm_automatic == c.numa_id && tcm_automatic == c.core_type_id &&
                   tcm_automatic == c.threads_per_core)
        {
          // Neither low-level nor high-level constraints specified
          is_request_sane = false;
          break;
        }
        sum_max += adjusted_max;

        if (adjusted_max < adjusted_min) {
            is_request_sane = false;
            break;
        }
    }
    __TCM_ASSERT(0 <= sum_min && 0 <= sum_max, "Incorrect invariant");
    return is_request_sane;
}


// Computes the currently used amount of resources by specified permit data
uint32_t get_permit_grant(const tcm_permit_data_t& pd) {
    uint32_t permit_grant = 0;
    for (unsigned i = 0; i < pd.size; ++i)
        permit_grant += pd.concurrency[i].load(std::memory_order_relaxed);
    return permit_grant;
}

// Computes the currently used amount of resources by specified permit handle
uint32_t get_permit_grant(tcm_permit_handle_t ph) { return get_permit_grant(ph->data); }

// Computes the negotiable part of the permit (i.e. grant minus required)
uint32_t get_num_negotiable(const tcm_permit_handle_t& handle) {
    __TCM_ASSERT(0 <= handle->request.min_sw_threads,
                 "Exact number for required threads must be known.");

    // WARNING: This function is used in a container comparator. Therefore, the permit state
    // affecting the results of this function should not be modified before the permit is removed
    // from the corresponding container. Otherwise, accessing containers using the updated permit
    // might lead to undefined behavior.

    // Expectations:
    // - Inherited resource cannot be negotiated as it does not belong to the nested request.
    // - If requested minimum equals to zero, then negotiable part is permit's grant w/o inherited.
    // - Otherwise, inherited is summed with the grant as it can be used to satisfy the minimum.

    const uint32_t grant = get_permit_grant(handle);
    const uint32_t requested_minimum = uint32_t(handle->request.min_sw_threads);
    if (requested_minimum == 0) // TODO: Remove when zero minimum requests are disallowed
        return grant;

    const uint32_t actual_num_resources = grant + uint32_t(is_nested(handle));
    if (actual_num_resources < requested_minimum)
        return 0;           // Permit might have no resources (e.g., in PENDING state)

    return actual_num_resources - requested_minimum;
}

uint32_t permit_unhappiness(const tcm_permit_handle_t& ph) {
    __TCM_ASSERT(ph->request.max_sw_threads > 0, "Exact number of desired resources is unknown");
    const uint32_t desired = uint32_t(ph->request.max_sw_threads);

    const uint32_t grant = get_permit_grant(ph) + uint32_t(is_nested(ph));

    __TCM_ASSERT(desired >= grant, "More than desired is distributed to the permit");
    return /* permit unhappiness = */desired - grant;
}

/**
********************************************************************************
* Permit comparators
********************************************************************************
*/
// Comparator returns true if the first permit owns more negotiable resources than the second
// permit. Both permits must be in the IDLE state.
struct greater_idled_resources_t {
    bool operator()(const tcm_permit_handle_t& lhs, const tcm_permit_handle_t& rhs) const {
        __TCM_ASSERT(is_idle(get_permit_state(lhs->data)) && is_idle(get_permit_state(rhs->data)),
                     "Expecting permits in IDLE state");
        const auto lhs_value = get_permit_grant(lhs);
        const auto rhs_value = get_permit_grant(rhs);
        return lhs_value != rhs_value ? lhs_value > rhs_value : lhs > rhs;
    }
};

struct greater_negotiable_t {
    bool operator()(const tcm_permit_handle_t& lhs, const tcm_permit_handle_t& rhs) const {
        const auto lhs_value = get_num_negotiable(lhs);
        const auto rhs_value = get_num_negotiable(rhs);
        return lhs_value != rhs_value ? lhs_value > rhs_value : lhs > rhs;
    }
};

struct less_min_request_t {
    bool operator()(const tcm_permit_handle_t& lhs, const tcm_permit_handle_t& rhs) const {
        const auto lhs_min_request = lhs->request.min_sw_threads;
        const auto rhs_min_request = rhs->request.min_sw_threads;
        return lhs_min_request != rhs_min_request ? lhs_min_request < rhs_min_request : lhs < rhs;
    }
};

struct less_unhappy_t {
    bool operator()(const tcm_permit_handle_t& lhs, const tcm_permit_handle_t& rhs) const {
        return permit_unhappiness(lhs) < permit_unhappiness(rhs);
    }
};

/**
********************************************************************************
* End of permit comparators
********************************************************************************
*/


void prepare_permit_modification(tcm_permit_handle_t ph) {
    uint64_t prev_epoch = ph->epoch.fetch_add(1, std::memory_order_relaxed);
    __TCM_ASSERT(prev_epoch % 2 == 0, "Previous epoch value must be even.");
    suppress_unused_warning(prev_epoch);
}

void commit_permit_modification(tcm_permit_handle_t ph) {
    uint64_t prev_epoch = ph->epoch.fetch_add(1, std::memory_order_release);
    __TCM_ASSERT(prev_epoch % 2 != 0, "Previous epoch value must be odd.");
    suppress_unused_warning(prev_epoch);
}

//! Returns available platform resources, taking into account possible degree
//! of oversubscription.
uint32_t platform_resources(unsigned int process_concurrency) {
  static uint32_t concurrency = uint32_t(tcm_oversubscription_factor() * process_concurrency);
  // TODO: Consider returning not less than one resource
  return concurrency;
}

// RAII helpers for CPU masks
void free_cpu_mask(tcm_cpu_mask_t mask_ptr) { hwloc_bitmap_free(mask_ptr); }

struct cpu_mask_deleter_t {
    void operator()(tcm_cpu_mask_t* mask_ptr) { free_cpu_mask(*mask_ptr); }
};

struct cpu_mask_array_deleter_t {
    cpu_mask_array_deleter_t(uint32_t size) : m_size(size) {}
    void operator()(tcm_cpu_mask_t* cpu_masks) const {
        for (uint32_t i = 0; i < m_size; ++i) { free_cpu_mask(cpu_masks[i]); }
        delete [] cpu_masks;
    }
private:
    const uint32_t m_size;
};


int32_t infer_constraint_min_concurrency(int32_t min_concurrency_value) {
    if (tcm_automatic == min_concurrency_value)
        // Returning zero since the subconstraint may be used as an optional source of
        // resources, e.g., if requested concurrency cannot be satisfied with the other
        // subconstraints
        return 0;

    __TCM_ASSERT(min_concurrency_value >= 0,
                 "Incorrect value for constraint.min_concurrency was found.");

    return min_concurrency_value;
}

int32_t infer_constraint_max_concurrency(int32_t max_concurrency_value, uint32_t fallback_value,
                                         const tcm_cpu_mask_t& mask)
{
    if (tcm_automatic != max_concurrency_value) {
      __TCM_ASSERT(max_concurrency_value > 0,
                   "Found incorrect value for constraint.max_concurrency.");
      return max_concurrency_value;
    }

    if (mask) {
      // Use oversubscribed mask concurrency to avoid inability to allocate maximum requested
      // resources within the given mask
      max_concurrency_value = get_oversubscribed_mask_concurrency(mask);

      if (max_concurrency_value < 0) {
        // Fail to get the concurrency of the provided mask or the mask is inifinitely set, use
        // fallback value.
        max_concurrency_value = fallback_value;
      }
    }

    // max_concurrency_value might still have automatic value, because of the use of high level
    // constraint description. In this case, we defer the inference of it to a later stage
    return max_concurrency_value;
}


struct fitting_result_t {
    bool is_required_satisfied{false};
    // Depending on the value of 'is_required_satisfied', contains either concurrency
    // necessary to fulfill desired concurrency (is_required_satisfied == true) or to
    // fulfill required concurrency (is_required_satisfied == false)
    uint32_t needed_concurrency{0};
    tcm_cpu_mask_t mask{nullptr};
};


/**
 * Describes how to modify a single permit
 */
struct permit_change_t {
    tcm_permit_handle_t ph = nullptr;        // permit handle to update
    tcm_permit_state_t new_state = 0;        // state to transition into
    std::vector<uint32_t> new_concurrencies; // concurrencies to set
    uint32_t inherited_concurrency_idx;      // index of the constraint to store inherited concurrency
};

struct callback_args_t {
    tcm_permit_handle_t ph;
    void* callback_arg;
    tcm_callback_flags_t reason;
};

/**
 * Describes the callbacks and arguments to pass there.
 */
typedef std::unordered_multimap<tcm_callback_t, callback_args_t> update_callbacks_t;

void merge_callback_invocations(update_callbacks_t& callbacks, const update_callbacks_t& other) {
    for (const auto& item : other) {
        const tcm_callback_t& callback = item.first;
        const callback_args_t& other_args = item.second;

        auto range = callbacks.equal_range(callback);

        if (range.first == callbacks.end()) {
            // This is a new element for the map
            callbacks.insert( {callback, other_args} );
            continue;
        }

        bool is_permit_in_range = false;
        for (auto it = range.first; it != range.second; ++it) {
            callback_args_t& args = it->second;
            if (args.ph == other_args.ph) {
                args.reason.new_concurrency |= other_args.reason.new_concurrency;
                args.reason.new_state |= other_args.reason.new_state;
                is_permit_in_range = true;
            }
        }

        if (!is_permit_in_range) {
            callbacks.insert( {callback, other_args} );
        }
    }
}

void invoke_callbacks(const update_callbacks_t& callbacks) {
    __TCM_PROFILE_THIS_FUNCTION();

    for (const auto& n : callbacks) {
        const tcm_callback_t& callback = n.first;
        const callback_args_t& args = n.second;

        __TCM_ASSERT(callback, "Incorrect invariant: missing callback is in the invocation list.");
        callback(args.ph, args.callback_arg, args.reason);

        // Consider implementing special handling for various return codes from callback function.
    }
}

// Permit changing helpers
uint32_t grab_permit_resources(tcm_permit_handle_t ph) {
    uint32_t current_grant = 0;

    auto& pd = ph->data;
    for (uint32_t i = 0; i < pd.size; ++i)
        current_grant += pd.concurrency[i].exchange(0, std::memory_order_relaxed);

    return current_grant;
}

uint32_t release_resources_moving_to_new_state(tcm_permit_handle_t ph,
                                               tcm_permit_state_t new_state)
{
    __TCM_ASSERT(is_void(new_state)    ||
                 is_pending(new_state) ||
                 is_inactive(new_state), "Inconsistent request.");

    auto& pd = ph->data;
    uint32_t current_grant = 0;

    prepare_permit_modification(ph);
    {
        for (uint32_t i = 0; i < pd.size; ++i) {
            current_grant += pd.concurrency[i].exchange(0, std::memory_order_relaxed);
        }
        pd.state.store(new_state, std::memory_order_relaxed);
    }
    commit_permit_modification(ph);

    return current_grant;
}

uint32_t move_to_inactive(tcm_permit_handle_t ph) {
    return release_resources_moving_to_new_state(ph, TCM_PERMIT_STATE_INACTIVE);
}

uint32_t move_to_pending(tcm_permit_handle_t ph) {
    return release_resources_moving_to_new_state(ph, TCM_PERMIT_STATE_PENDING);
}


fitting_result_t try_fit_concurrency(const int32_t min_threads, const int32_t max_threads,
                                     const int available)
{
    bool can_satisfy_required{true};
    uint32_t next_level_requirement{0};

    int diff = available - max_threads;
    if (diff >= 0) {
        next_level_requirement = 0;
    } else if (available >= min_threads) {
        next_level_requirement = uint32_t(-diff);
    } else {
        next_level_requirement = uint32_t(min_threads - available);
        can_satisfy_required = false;
    }

    return fitting_result_t{can_satisfy_required, next_level_requirement, /*mask*/nullptr};
}

struct ThreadComposabilityManagerData {
  ThreadComposabilityManagerData() {
    // TODO: parse topology only once
    system_topology::construct();
    system_topology& topology = system_topology::instance();
    // TODO: avoid unnecessary process mask allocation by reusing the one from
    // the system_topology?

    // TODO: make process_mask have const-qualifier so that it is not modified
    process_mask = topology.allocate_process_affinity_mask();
    if (process_mask) {
      process_concurrency = topology.get_process_concurrency();
    } else {
      // TODO: add manual parsing and weight calculation of the process mask
      process_concurrency = (uint32_t)std::thread::hardware_concurrency();
    }

#if __linux__
    int cfs_constrained_cpus;
    if (cgroup_info<>::is_cpu_constrained(cfs_constrained_cpus)) {
        process_concurrency = std::min((int)process_concurrency, cfs_constrained_cpus);
    }
#endif

    available_concurrency = platform_resources(process_concurrency);
    __TCM_ASSERT(available_concurrency >= 1, nullptr);

    initially_available_concurrency = available_concurrency;
  }

  ~ThreadComposabilityManagerData() {
    hwloc_bitmap_free(process_mask);
    system_topology::destroy();
  }

  std::mutex data_mutex{};

  tcm_client_id_t client_id = 1;

  //! The count of available resources.
  uint32_t process_concurrency = 0;

  //! The count of currently available resources.
  uint32_t available_concurrency = 0;

  //! The count of resources that were available at the program start.
  uint32_t initially_available_concurrency;

  //! The epoch that changes whenever the state of the TCM changes.
  //!  Used to avoid unnecessary resource redistribution when nothing has changed
  //!  from the previous permit update
  uint32_t tcm_state_epoch = 0;

  //! The CPU mask of the process
  tcm_cpu_mask_t process_mask = nullptr;

  //! The existing permits
  std::set<tcm_permit_handle_t, less_min_request_t> pending_permits{};
  std::set<tcm_permit_handle_t, greater_idled_resources_t> idle_permits{};
  std::set<tcm_permit_handle_t, greater_negotiable_t> active_permits{};

  //! The permit that was lazily deactivated
  tcm_permit_handle_t lazy_inactive_permit = nullptr;

  //! The map of callbacks per each client. Callbacks are used during
  //! renegotiation of permits.
  std::unordered_map<tcm_client_id_t, tcm_callback_t> client_to_callback_map{};

  //! The multimap of permits associated with the given client.
  std::unordered_multimap<tcm_client_id_t, tcm_permit_handle_t> client_to_permit_mmap{};
};


#if TCM_USE_ASSERT
/**
 * Checks for existence of an item within a container that allows iterator-based access.
 *
 * Algorithm is linear to avoid the use of container's custom-comparators.
 */
template <typename Container>
bool iterating_over_inclusion_check(Container const& c, tcm_permit_handle_t ph) {
    for (auto const& e : c) {
        if (e == ph)
            return true;
    }
    return false;
}
#endif    // TCM_USE_ASSERT

/**
 * Removes permit handle from the corresponding container.
 *
 * Make sure the state of the permit that affects containers' comparators is not modified while it
 * was inside. Otherwise, container might not be able to find that permit.
 */
void remove_permit(ThreadComposabilityManagerData& data, tcm_permit_handle_t ph,
                   const tcm_permit_state_t& current_state)
{
    std::size_t n = 0;
    if (is_pending(current_state)) {
        n = data.pending_permits.erase(ph);
    } else if (is_idle(current_state)) {
        n = data.idle_permits.erase(ph);
    } else if (is_active(current_state) && participates_in_subscription_compute(ph)) {
        n = data.active_permits.erase(ph);
    } else if (ph == data.lazy_inactive_permit) {
        data.lazy_inactive_permit = nullptr;
    }

    __TCM_ASSERT_EX(1 == n || is_inactive(current_state) ||
                    !participates_in_subscription_compute(ph), "Incorrect invariant");
    __TCM_ASSERT(!iterating_over_inclusion_check(data.active_permits, ph) &&
                 !iterating_over_inclusion_check(data.idle_permits, ph) &&
                 !iterating_over_inclusion_check(data.pending_permits, ph),
                 "Incorrect invariant");
}

/**
 * Adds permit handle to the corresponding container.
 *
 * Make sure the state of the permit that affects containers' comparators is not modified after it
 * is added. Otherwise, container might not be able to find that permit on subsequent calls.
 */
void add_permit(ThreadComposabilityManagerData& data, tcm_permit_handle_t ph,
                const tcm_permit_state_t& new_state)
{
    __TCM_ASSERT(!iterating_over_inclusion_check(data.active_permits, ph) &&
                 !iterating_over_inclusion_check(data.idle_permits, ph) &&
                 !iterating_over_inclusion_check(data.pending_permits, ph),
                 "permit_handle is expected to be absent from containers");

    if (is_pending(new_state)) {
        data.pending_permits.insert(ph);
    } else if (is_idle(new_state)) {
        data.idle_permits.insert(ph);
    } else if (is_active(new_state) &&
               // Rigid concurrency permits are not negotiable while in ACTIVE state, so don't need
               // to store them for further consideration, except for constrained rigid concurrency
               // permits that are used for calculating mask subscription.
               participates_in_subscription_compute(ph))
    {
        data.active_permits.insert(ph);
    }
}

// Helper moves permit between the permit containers from the ThreadComposabilityManagerData struct
// in accordance with the current and new states of the permit
void move_permit(ThreadComposabilityManagerData& data, tcm_permit_handle_t ph,
                 const tcm_permit_state_t& current_state, const tcm_permit_state_t& new_state)
{
    remove_permit(data, ph, current_state);
    add_permit(data, ph, new_state);
}


bool has_unused_resources(const ThreadComposabilityManagerData& data) {
    return data.available_concurrency || !data.idle_permits.empty();
}

bool has_resource_demand(const ThreadComposabilityManagerData& data) {
    return !data.active_permits.empty() || !data.pending_permits.empty();
}

void note_tcm_state_change(ThreadComposabilityManagerData& data) {
    data.tcm_state_epoch++;
}

/**
 * Updates single permit according to the change. Records what is changed and returns concurrency
 * delta.
 */
int32_t update(tcm_permit_data_t& permit, tcm_permit_state_t current_state,
               const permit_change_t& change, tcm_callback_flags_t& reason)
{
    int32_t concurrency_delta = 0;

    prepare_permit_modification(change.ph);
    {
        if (current_state != change.new_state) {
            permit.state.store(change.new_state, std::memory_order_relaxed);
            reason.new_state = true;
        }

        permit.inherited_concurrency_idx.store(change.inherited_concurrency_idx, std::memory_order_relaxed);
        for (std::size_t i = 0; i < change.new_concurrencies.size(); ++i) {
            const uint32_t old_concurrency = permit.concurrency[i].load(std::memory_order_relaxed);
            const uint32_t new_concurrency = change.new_concurrencies[i];
            if (old_concurrency != new_concurrency) {
                permit.concurrency[i].store(new_concurrency, std::memory_order_relaxed);
                reason.new_concurrency = true;
                concurrency_delta += old_concurrency - new_concurrency;
            }
        }
    }
    commit_permit_modification(change.ph);

    return concurrency_delta;
}

/**
 * Applies given change to the permit, maintaining internal state and populating callbacks container
 * if necessary.
 * Returns concurrency delta.
 */
int32_t apply(const permit_change_t& change, ThreadComposabilityManagerData& data,
              const tcm_permit_handle_t initiator, const bool remove_initiator_first,
              update_callbacks_t& callbacks)
{
    const tcm_permit_handle_t permit_handle = change.ph;
    callback_args_t args{permit_handle, /*callback_arg*/nullptr, /*reason invoking callback*/{}};
    tcm_callback_flags_t& reason = args.reason;

    tcm_permit_data_t& permit = permit_handle->data;
    tcm_permit_state_t state = get_permit_state(permit);

    if (permit_handle != initiator || remove_initiator_first) {
        remove_permit(data, permit_handle, state);
    }

    int32_t concurrency_delta = update(permit, state, change, reason);

    add_permit(data, permit_handle, change.new_state);

    // Record callback invocation for modified permits, except for an initiator of the change
    if (permit_handle != initiator && (reason.new_concurrency || reason.new_state)) {
        tcm_callback_t callback = data.client_to_callback_map[permit.client_id];
        if (callback) {
            args.callback_arg = permit_handle->callback_arg;
            callbacks.insert( {callback, args} );
        }
    }

    return concurrency_delta;
}

/**
 * Applies changes to permits according to the given updates. Returns the list of callbacks and
 * their arguments to be invoked.
 *
 * The callback is added into the returned list only if there was an actual change to the permit.
 */
update_callbacks_t apply(ThreadComposabilityManagerData& data,
                         const std::vector<permit_change_t>& updates,
                         const tcm_permit_handle_t initiator,
                         const bool remove_initiator_first = true)
{
    __TCM_PROFILE_THIS_FUNCTION();
    update_callbacks_t callbacks;

    int32_t concurrency_delta = 0;
    for (unsigned i = 0; i < updates.size(); ++i) {
        __TCM_ASSERT(updates[i].ph != initiator || updates.size() - 1 == i,
                     "Initiator of updates should be the last in the list");
        concurrency_delta += apply(updates[i], data, initiator, remove_initiator_first, callbacks);
#if __TCM_ENABLE_PERMIT_TRACER
        if (i < updates.size() - 1)
            __TCM_PROFILE_PERMIT(updates[i].ph, "negotiated");
#endif
    }

#if TCM_USE_ASSERT
    if (concurrency_delta < 0) {
        __TCM_ASSERT(data.available_concurrency + concurrency_delta < data.available_concurrency,
                     "Underflow detected");
    } else {
        __TCM_ASSERT(data.available_concurrency <= data.available_concurrency + concurrency_delta,
                     "Overflow detected");
    }
#endif

    data.available_concurrency += concurrency_delta;
    note_tcm_state_change(data);

    return callbacks;
}


// Describes a stakeholder of specific resources subset with cached negotiable value,
// which equals to permitted concurrency value on the subconstraint minus minimum
// requested concurrency on that subconstraint.
struct stakeholder_t {
    tcm_permit_handle_t ph = nullptr;

    // tcm_automatic if the whole permit is considered rather than its particular constraint
    int32_t constraint_index = tcm_automatic;

    uint32_t num_negotiable = 0; // Negotiable part of the permit. Equals to grant - min_sw_threads
};

struct greater_negotiable_stakeholder_t {
    bool operator()(const stakeholder_t& lhs, const stakeholder_t& rhs) const {
        return lhs.num_negotiable > rhs.num_negotiable;
    }
};

typedef std::priority_queue<stakeholder_t, std::vector<stakeholder_t>,
                            greater_negotiable_stakeholder_t> renegotiable_resources_queue_t;

// The container of negotiable stakeholders. Filled in while trying to satisfy a request.
struct negotiable_snapshot_t {
    static const int32_t among_all_constraints = tcm_automatic;

    uint32_t num_negotiable_idle() const { return m_negotiable_idle; }
    uint32_t num_negotiable_active() const { return m_negotiable_active; }
    uint32_t num_negotiable() const { return num_negotiable_idle() + num_negotiable_active(); }

    uint32_t num_immediately_available() const { return m_immediately_available; }
    void set_immediately_available(uint32_t value) { m_immediately_available = value; }

    uint32_t num_available() const { return num_immediately_available() + num_negotiable(); }

    // TODO: remove, since the min and max concurrencies should be automatically inferred at the
    // time this function is called
    void set_adjusted_concurrencies(uint32_t min_concurrency, uint32_t max_concurrency) {
        __TCM_ASSERT(min_concurrency <= max_concurrency,
                     "Minimum concurrency must be less or equal to maximum concurrency.");
        m_adjusted_min_concurrency = min_concurrency;
        m_adjusted_max_concurrency = max_concurrency;
    }
    uint32_t adjusted_min_concurrency() const { return m_adjusted_min_concurrency; }
    uint32_t adjusted_max_concurrency() const { return m_adjusted_max_concurrency; }

    renegotiable_resources_queue_t idle_permits() const { return m_permits_idle; }
    renegotiable_resources_queue_t active_permits() const { return m_permits_active; }

    void add(const stakeholder_t& stakeholder, const tcm_permit_state_t& state) {
      /* negotiable == grant - minimum sw threads, if constraint_index equals to
         among_all_constraints. Otherwise, negotiable == grant - minimum concurrency
         within specific subconstraint, determined by the constraint_index in the array of
         permit's constraints. */

        __TCM_ASSERT(is_owning_resources(get_permit_state(stakeholder.ph->data)),
                     "Only permits with owning resources can be added into negotiable snapshot." );

        const auto search_item = std::make_pair(stakeholder.ph, stakeholder.constraint_index);
        if (m_included_permits.find(search_item) != m_included_permits.end())
            // There is such an item in the queue already. Do not duplicate it.
            return;

        if (is_active(state)) {
            m_negotiable_active += stakeholder.num_negotiable;
            m_permits_active.push(stakeholder);
        } else {
            m_negotiable_idle += stakeholder.num_negotiable;
            m_permits_idle.push(stakeholder);
        }
    }

private:
    // The amount of resources that can grant without negotiations.
    uint32_t m_immediately_available = 0;

    // The amount of resources that can be negotiated from IDLE permits
    uint32_t m_negotiable_idle = 0;

    // The amount of resources that can be negotiated, that is the sum of (grant
    // concurrency minus minimum required) expressions (possibly, per constraint) across
    // active permits in the snapshot.
    uint32_t m_negotiable_active = 0;

    // Adjusted concurrencies for specific constraint (or full permit if no constraints)
    uint32_t m_adjusted_min_concurrency = 0, m_adjusted_max_concurrency = 0;

    // Permits containing negotiable resources from IDLE permits, ordered from the most to the least
    // resources amount available for renegotiation
    renegotiable_resources_queue_t m_permits_idle =
        renegotiable_resources_queue_t(greater_negotiable_stakeholder_t{});

    // Permits containing negotiable resources from ACTIVE permits, ordered from the most to the
    // least resources amount available for renegotiation
    renegotiable_resources_queue_t m_permits_active =
        renegotiable_resources_queue_t(greater_negotiable_stakeholder_t{});

    // The set of permits that have been added to the queue
    std::set< std::pair<tcm_permit_handle_t, /*constraint_index*/int32_t>>
    m_included_permits;
};


class ThreadComposabilityManagerBase : public ThreadComposabilityManagerData {
public:
  virtual ~ThreadComposabilityManagerBase() {}

  tcm_client_id_t register_client(tcm_callback_t r) {
      __TCM_PROFILE_THIS_FUNCTION();
      const std::lock_guard<std::mutex> l(data_mutex);
      tcm_client_id_t clid = client_id++;
      client_to_callback_map[clid] = r;
      return clid;
  }

  struct client_resources_t {
      uint32_t concurrency = 0;
      std::vector<tcm_permit_handle_t> permit_handles;
  };

  void unregister_client(tcm_client_id_t clid) {
      __TCM_PROFILE_THIS_FUNCTION();

      bool should_renegotiate = false;
      std::vector<tcm_permit_handle_t> client_permits;
      client_resources_t client_resources;
      {
          const std::lock_guard<std::mutex> l(data_mutex);
          client_resources = clear_up_internals_from(clid);
          __TCM_ASSERT(available_concurrency <= available_concurrency + client_resources.concurrency,
                     "Overflow detected");
          available_concurrency += client_resources.concurrency;
          should_renegotiate = client_resources.concurrency > 0 && has_resource_demand(*this);

          __TCM_ASSERT(client_to_callback_map.count(clid) == 1, "The client_id was not registered.");
          client_to_callback_map.erase(clid);
          note_tcm_state_change(*this);
#if __TCM_ENABLE_PERMIT_TRACER
          for (const auto& ph : client_resources.permit_handles)
              __TCM_PROFILE_PERMIT(ph, "abandoned");
#endif
      }

      for (const auto& ph : client_resources.permit_handles)
          deallocate_permit(ph);

      if (should_renegotiate)
          renegotiate_permits(/*initiator*/nullptr);
  }

 /**
  * Allocates and copies constraints for the specified permit request.
  *
  * Assumes actual pointer to constraints is written to the passed constraints argument.
  */
  void copy_constraints(tcm_cpu_constraints_t* dst, tcm_cpu_constraints_t* src, uint32_t size) {
      __TCM_ASSERT(dst, "No constraints to copy to");
      __TCM_ASSERT(src, "No constraints to copy from");
      for (uint32_t i = 0; i < size; ++i) {
          dst[i] = src[i];
          if (src[i].mask) {
              dst[i].mask = hwloc_bitmap_dup(src[i].mask);
          }
      }
  }


  /**
   * Deallocates permit's CPU masks and masks in the constraints of the request.
   */
  void deallocate_masks(tcm_cpu_constraints_t* constraints, tcm_cpu_mask_t* masks, uint32_t size) {
      __TCM_ASSERT(constraints && masks, "No constraints and CPU masks to deallocate");
      for (uint32_t i = 0; i < size; ++i) {
          hwloc_bitmap_free(constraints[i].mask);
          hwloc_bitmap_free(masks[i]);
      }
  }

  void deallocate_permit(tcm_permit_handle_t permit_handle) {
      __TCM_ASSERT(permit_handle, nullptr);

      tcm_permit_request_t& request = permit_handle->request;
      tcm_permit_data_t& data = permit_handle->data;

      if (request.cpu_constraints) {
          deallocate_masks(request.cpu_constraints, data.cpu_mask, data.size);
      }

      using type = std::atomic<uint32_t>;
      for (uint32_t i = 0; i < data.size; ++i) {
          data.concurrency[i].~type();
      }

      permit_handle->~tcm_permit_rep_t();

      std::free(permit_handle);
  }

  void copy_request(tcm_permit_request_t& to, const tcm_permit_request_t& from,
                    const bool copy_masks)
  {
    tcm_cpu_constraints_t* internal_cpu_constraints = to.cpu_constraints;
    __TCM_ASSERT(to.constraints_size == from.constraints_size, "Constraints sizes are different.");
    to = from;
    to.cpu_constraints = internal_cpu_constraints; // Restore the pointer to TCM's memory

    for (uint32_t i = 0; i < to.constraints_size; ++i) {
        tcm_cpu_mask_t internal_mask = to.cpu_constraints[i].mask;

        __TCM_ASSERT(
          copy_masks || internal_mask == nullptr ||
          0 == hwloc_bitmap_compare(internal_mask, from.cpu_constraints[i].mask),
          "Changing of the mask when re-requesting resources for existing permit is not supported."
        );

        to.cpu_constraints[i] = from.cpu_constraints[i];
        to.cpu_constraints[i].mask = internal_mask;

        if (copy_masks && from.cpu_constraints[i].mask) {
            if (to.cpu_constraints[i].mask) {
                hwloc_bitmap_copy(to.cpu_constraints[i].mask, from.cpu_constraints[i].mask);
            } else {
                to.cpu_constraints[i].mask = hwloc_bitmap_dup(from.cpu_constraints[i].mask);
            }
        }
    }
  }

  void determine_nested_permit(tcm_permit_handle_t& ph) {
    auto& permit_stack = get_active_permit_container();
    prepare_permit_modification(ph);
    if (!permit_stack.empty()) {
        auto top_permit = permit_stack.top();
        if (ph != top_permit) {
            ph->data.is_nested.store(true, std::memory_order_relaxed);
        }
    } else {
        ph->data.is_nested.store(false, std::memory_order_relaxed);
    }
    commit_permit_modification(ph);
  }

  void deduce_request_arguments(tcm_permit_request_t& request, const int32_t sum_constraints_min) {
    if (tcm_automatic == request.min_sw_threads) {
      request.min_sw_threads = sum_constraints_min;
    }

    bool has_automatic_in_constraints_max_concurrency = false;
    for (uint32_t i = 0; i < request.constraints_size; ++i) {
      tcm_cpu_constraints_t& c = request.cpu_constraints[i];

      c.min_concurrency = infer_constraint_min_concurrency(c.min_concurrency);
      c.max_concurrency = infer_constraint_max_concurrency(c.max_concurrency,
                                                           /*fallback_value*/process_concurrency,
                                                           c.mask);

      // Assert states that if max_concurrency of a constraint is not explicitly specified, then
      // there is enough data in the constraint (mask or high-level interface is used) to infer the
      // value for max_concurrency later
      __TCM_ASSERT(c.max_concurrency != tcm_automatic ||
                   (c.mask || tcm_automatic != c.numa_id || tcm_automatic != c.core_type_id ||
                    tcm_automatic != c.threads_per_core), "Incorrect invariant");

      // If at least one constraint has automatic setting for its desired concurrency, then the
      // desired resources amount is inferred automatically while satisfying the request
      has_automatic_in_constraints_max_concurrency |= c.max_concurrency == tcm_automatic;
    }

    if (!has_automatic_in_constraints_max_concurrency && tcm_automatic == request.max_sw_threads) {
      request.max_sw_threads = process_concurrency;
    }
  }

  void copy_permit(tcm_permit_handle_t ph, tcm_permit_t* permit) {
    bool reading_succeeded = false;
    while (!reading_succeeded) {
      reading_succeeded = try_copy_permit(ph, permit);
    }
  }

  tcm_result_t request_permit(tcm_client_id_t clid, const tcm_permit_request_t& req,
                              void* callback_arg, tcm_permit_handle_t* permit_handle,
                              tcm_permit_t* permit, const int32_t sum_constraints_min)
  {
    __TCM_PROFILE_THIS_FUNCTION();

    bool additional_concurrency_available = false;

    tcm_permit_handle_t& ph = *permit_handle;
    const bool is_requesting_new_permit = bool(!ph);
    const tcm_permit_state_t new_permit_state =
        req.flags.request_as_inactive
        ? tcm_permit_state_t(TCM_PERMIT_STATE_INACTIVE)
        : tcm_permit_state_t(TCM_PERMIT_STATE_PENDING);
    if (is_requesting_new_permit) {
      ph = make_new_permit(clid, req, new_permit_state);
      if (!ph) { // Could not allocate memory for the new permit
          return TCM_RESULT_ERROR_UNKNOWN;
      }
    } else if (req.flags.request_as_inactive) {
        // It is client responsibility to ensure permit re-initialization happens serially
        __TCM_ASSERT(ph->epoch.load(std::memory_order_relaxed) % 2 == 0,
                     "Permit is being concurrently modified");
        tcm_permit_state_t state = get_permit_state(ph->data);
        if (is_owning_resources(state) || is_pending(state) ||
            req.constraints_size != ph->request.constraints_size)
        {
            return TCM_RESULT_ERROR_INVALID_ARGUMENT;
        }
        copy_request(ph->request, req, /*allow_updating_masks*/true);
        ph->callback_arg = callback_arg;
        ph->data.flags = req.flags;
        __TCM_PROFILE_PERMIT(ph, "requested as inactive");

        // TODO: Allow permit handle re-allocation even if sizes of constraints do not match
        // (may require updating handle in client-to-permit multimap)
    }

    // TODO: Grant concurrency to rigid nested permit instantly
    // when there are no available resources

    update_callbacks_t callbacks;
    if (!req.flags.request_as_inactive) {
      const std::lock_guard<std::mutex> l(data_mutex);

      uint32_t initially_available = available_concurrency;

      // TODO: Consider adding the permit to containers after the concurrency level
      // calculation to avoid early renegotiation
      if (is_requesting_new_permit) {
          client_to_permit_mmap.emplace(ph->data.client_id, ph);
      } else {
          __TCM_ASSERT(is_valid(ph), "Permit request structure must exist.");
          // It is important to remove the permit first from corresponding container since updating
          // either part of the representation (request or grant) leads to wrong position of the
          // permit inside its ordered container, which results in wrong implementational
          // assumptions and incorrect results
          remove_permit(*this, ph, get_permit_state(ph->data));

          copy_request(ph->request, req, /*copy_masks*/false);

          // Request is being updated for existing permit. To avoid in-the-middle
          // negotiations for that permit change its state to PENDING until its new
          // required/minimum parameters are further satisfied.
          const uint32_t released = move_to_pending(ph);
          __TCM_ASSERT(available_concurrency <= available_concurrency + released, "Overflow detected");
          available_concurrency += released;
          note_tcm_state_change(*this);
      }
      determine_nested_permit(ph);
      deduce_request_arguments(ph->request, sum_constraints_min);

      ph->data.flags.request_as_inactive = false;
      ph->callback_arg = callback_arg;

      std::vector<permit_change_t> updates = adjust_existing_permit(ph->request, ph);

      if (!updates.empty()) {
          callbacks = apply(*this, updates, /*initiator*/ph, /*remove_initiator_first*/false);
      } else {
          add_permit(*this, ph, TCM_PERMIT_STATE_PENDING);
      }

      __TCM_PROFILE_PERMIT(ph, "requested");

      // Client might re-request for less number of resources
      additional_concurrency_available = available_concurrency > initially_available;
      ph->data.tcm_epoch_snapshot = tcm_state_epoch;
    }

    invoke_callbacks(callbacks); // Invocation of the callbacks is happenning outside of the lock

    if (additional_concurrency_available) {
        renegotiate_permits(/*initiator*/ph);
    }

    if (permit) {
      copy_permit(ph, permit);
    }

    return TCM_RESULT_SUCCESS;
  }

  tcm_result_t get_permit(tcm_permit_handle_t ph, tcm_permit_t* permit) {
    __TCM_ASSERT(ph && permit, nullptr);

    // NOTE: expected that ph is valid - but direct call is_valid() can lead to races

    permit->flags.stale = false;
    if (!try_copy_permit(ph, permit)) {
      permit->flags.stale = true;
    }

    return TCM_RESULT_SUCCESS;
  }

  tcm_result_t idle_permit(tcm_permit_handle_t ph) {
    __TCM_PROFILE_THIS_FUNCTION();
    bool shall_negotiate_resources = false;
    {
      const std::lock_guard<std::mutex> l(data_mutex);
      __TCM_ASSERT(is_valid(ph), "Idling non-existing permit");
      tcm_permit_data_t& pd = ph->data;
      tcm_permit_state_t curr_state = get_permit_state(pd);
      if (!is_active(curr_state)) {
          return TCM_RESULT_ERROR_INVALID_ARGUMENT;
      }
      change_state_relaxed(pd, TCM_PERMIT_STATE_IDLE);
      move_permit(*this, ph, curr_state, /*new_state*/TCM_PERMIT_STATE_IDLE);
      __TCM_PROFILE_PERMIT(ph, "idled");
      shall_negotiate_resources = has_resource_demand(*this);
    }
    if (shall_negotiate_resources) {
      renegotiate_permits(/*initiator*/nullptr);
    }
    return TCM_RESULT_SUCCESS;
  }

  tcm_result_t activate_permit(tcm_permit_handle_t ph) {
    __TCM_PROFILE_THIS_FUNCTION();

    update_callbacks_t callbacks;
    {
      // TODO: Consider permit activation without acquiring the lock.
      const std::lock_guard<std::mutex> l(data_mutex);
      __TCM_ASSERT(is_valid(ph), "Activating non-existing permit.");

      tcm_permit_data_t& pd = ph->data;
      tcm_permit_state_t curr_state = get_permit_state(pd);

      if (is_active(curr_state)) {
          // TODO: Consider this call as an invitation to renegotiate resources and make one more
          // try to satisfy passed permit handle (especially, if there is an opportunity to
          // re-distribute significantly more/less resources), even if it is a rigid concurrency
          // permit.
          return TCM_RESULT_SUCCESS;
      } else if (!is_inactive(curr_state) && !is_idle(curr_state)) {
          return TCM_RESULT_ERROR_INVALID_ARGUMENT;
      }

      // Activating in case when TCM state didn't change between idling/deactivation
      // and activation so availability of resources wasn't updated
      if (this->tcm_state_epoch == ph->data.tcm_epoch_snapshot &&
          !ph->data.flags.request_as_inactive) // Not initially requested as inactive hence has some
                                               // grant, but getting the grant at this point is an
                                               // overkill
      {
         remove_permit(*this, ph, curr_state);
         change_state_relaxed(ph->data, TCM_PERMIT_STATE_ACTIVE);
         determine_nested_permit(ph);
         add_permit(*this, ph, TCM_PERMIT_STATE_ACTIVE);
         __TCM_PROFILE_PERMIT(ph, "lazily activated");
         return TCM_RESULT_SUCCESS;
      }

      if (const uint32_t grant = get_permit_grant(pd)) {
          // Activating without spending much time on resources search in case minimum is already
          // satisfied
          __TCM_ASSERT(ph == lazy_inactive_permit || is_idle(curr_state), "Broken invariant");
          remove_permit(*this, ph, curr_state);
          determine_nested_permit(ph);
          uint32_t implicit_outer_concurrency = uint32_t(is_nested(ph));
          __TCM_ASSERT(grant + implicit_outer_concurrency >= uint32_t(ph->request.min_sw_threads),
                       "Grant of resources cannot be less than the minimum");
          __TCM_ASSERT(ph->request.max_sw_threads > 0, "Request's desired concurrency is unknown");
          const uint32_t desired_concurrency = uint32_t(ph->request.max_sw_threads)-implicit_outer_concurrency;
          // TODO: Re-distribute available concurrency for constrained permits as well instead of
          // simply activating them
          if (grant < desired_concurrency && available_concurrency > 0 && !is_constrained(ph)) {
              const uint32_t concurrency_change = std::min(available_concurrency,
                                                           desired_concurrency - grant);
              available_concurrency -= concurrency_change;
              ph->data.concurrency[0] += concurrency_change;
              note_tcm_state_change(*this);
          }
          ph->data.state.store(TCM_PERMIT_STATE_ACTIVE, std::memory_order_relaxed);
          add_permit(*this, ph, TCM_PERMIT_STATE_ACTIVE);
      } else {
          __TCM_ASSERT(is_inactive(curr_state), "Inactive permit is expected");
          determine_nested_permit(ph);
          ph->data.flags.request_as_inactive = false;
          std::vector<permit_change_t> updates = adjust_existing_permit(ph->request, ph);
          if (!updates.empty()) {
              // Specifying not to remove the initiator first is only to save on unnecessary search
              // in the containers as there is no container for inactive permits
              callbacks = apply(*this, updates, /*initiator*/ph, /*remove_initiator_first*/false);
          } else {
              change_state_relaxed(ph->data, TCM_PERMIT_STATE_PENDING);
              add_permit(*this, ph, TCM_PERMIT_STATE_PENDING);
          }
      }
      __TCM_PROFILE_PERMIT(ph, "activated");
      ph->data.tcm_epoch_snapshot = tcm_state_epoch;
    }

    invoke_callbacks(callbacks);

    return TCM_RESULT_SUCCESS;
  }

  tcm_result_t deactivate_permit(tcm_permit_handle_t ph) {
    __TCM_PROFILE_THIS_FUNCTION();

    __TCM_ASSERT(ph, "Invalid permit handle");

    tcm_permit_data_t& pd = ph->data;
    tcm_permit_state_t curr_state = get_permit_state(pd);

    if (is_inactive(curr_state)) {
        __TCM_PROFILE_PERMIT(ph, "deactivated inactive (lockless)");
        return TCM_RESULT_SUCCESS; // Quick response avoiding taking the lock
    }

    bool shall_negotiate_resources = false;
    {
      const std::lock_guard<std::mutex> l(data_mutex);
      __TCM_ASSERT(is_valid(ph), "Deactivating non-existing permit.");

      curr_state = get_permit_state(pd);
      if (is_inactive(curr_state)) {
          __TCM_PROFILE_PERMIT(ph, "deactivated inactive");
          return TCM_RESULT_SUCCESS;
      }

      remove_permit(*this, ph, curr_state);

      if (is_owning_resources(curr_state)) {
          // TODO: consider using adjust_existing_permit
          const bool has_another_lazy_inactive_permit =
              lazy_inactive_permit != nullptr && lazy_inactive_permit != ph;
          if (has_another_lazy_inactive_permit || has_resource_demand(*this)) {
              // TODO: Consider releasing only in demand resources
              auto previously_available_concurrency = available_concurrency;
              available_concurrency += move_to_inactive(ph);
              __TCM_ASSERT(previously_available_concurrency <= available_concurrency, "Overflow detected");
              shall_negotiate_resources = previously_available_concurrency < available_concurrency;
              note_tcm_state_change(*this);
              // TODO: Consider reading the TCM epoch onto the stack here to compare it against the
              // current epoch inside renegotite_permits() function. This might save unnecessary
              // renegotiation in case of concurrent changes to permits.
              __TCM_PROFILE_PERMIT(ph, "deactivated");
          } else {
              lazy_inactive_permit = ph;
              change_state_relaxed(pd, /*new_state*/TCM_PERMIT_STATE_INACTIVE);
              __TCM_PROFILE_PERMIT(ph, "lazily deactivated");
          }
      } else {
          __TCM_ASSERT(is_pending(curr_state), "Unexpected permit state");
          change_state_relaxed(pd, /*new_state*/TCM_PERMIT_STATE_INACTIVE);
          __TCM_PROFILE_PERMIT(ph, "deactivated pending");
      }
    }
    if (shall_negotiate_resources) {
        // Specify 'nullptr' in order to notify the client in case the resources from this permit
        // are better utilized
        renegotiate_permits(/*initiator*/nullptr);
    }

    return TCM_RESULT_SUCCESS;
  }

  /**
   * Clears internal structures from permit handle so that TCM does not know about the given permit
   * anymore. Returns concurrency that permit possessed.
   */
  uint32_t clear_up_internals_from(tcm_permit_handle_t permit_handle) {
      __TCM_ASSERT(permit_handle, nullptr);

      auto client_phs = client_to_permit_mmap.equal_range(permit_handle->data.client_id);
      for (auto it = client_phs.first; it != client_phs.second; ++it) {
          if (it->second == permit_handle) {
              client_to_permit_mmap.erase(it);
              break;
          }
      }

      remove_permit(*this, permit_handle, get_permit_state(permit_handle->data));

      return get_permit_grant(permit_handle);
  }

  /**
   * Clears internal structures from permits of a client so that TCM does not know about them
   * anymore. Returns overall concurrency a client has and permit handles to delete.
   */
  client_resources_t clear_up_internals_from(tcm_client_id_t clid) {
      __TCM_PROFILE_THIS_FUNCTION();

      __TCM_ASSERT(clid < client_id && client_to_callback_map.count(clid) == 1,
                   "The client_id is not known.");

      client_resources_t overall_resources;
      auto range = client_to_permit_mmap.equal_range(clid);
      for (auto i = range.first; i != range.second; ++i) {
          tcm_permit_handle_t permit_handle = i->second;
          overall_resources.concurrency += get_permit_grant(permit_handle);
          overall_resources.permit_handles.push_back(permit_handle);
          remove_permit(*this, permit_handle, get_permit_state(permit_handle->data));
      }
      client_to_permit_mmap.erase(range.first, range.second);

      return overall_resources;
  }

  tcm_result_t release_permit(tcm_permit_handle_t handle) {
    __TCM_PROFILE_THIS_FUNCTION();

    bool should_renegotiate = false;
    {
        const std::lock_guard<std::mutex> l(data_mutex);
        __TCM_ASSERT(is_valid(handle), "Releasing of an invalid permit");

        const uint32_t released_concurrency = clear_up_internals_from(handle);

        __TCM_ASSERT(available_concurrency <= available_concurrency + released_concurrency,
                     "Overflow detected");
        available_concurrency += released_concurrency;
        should_renegotiate = released_concurrency > 0 && has_resource_demand(*this);
        note_tcm_state_change(*this);
        __TCM_PROFILE_PERMIT(handle, "released");
    }

    // Permit representation has been erased from internal structures, i.e. TCM does not know about
    // it anymore. Thus, can deallocate permit representation without holding a lock.
    deallocate_permit(handle);

    if (should_renegotiate) {
        renegotiate_permits(/*initiator*/nullptr);
    }

    return TCM_RESULT_SUCCESS;
  }

  tcm_result_t register_thread(tcm_permit_handle_t ph) {
    __TCM_ASSERT(ph, nullptr);
    // TODO: profile only permit_handle and thread_id
    get_active_permit_container().push(ph);
    return TCM_RESULT_SUCCESS;
  }

  tcm_result_t unregister_thread() {
    auto& permit_stack = get_active_permit_container();
    __TCM_ASSERT(!permit_stack.empty(), "Attempt unregistering non-registered thread.");
    permit_stack.pop();
    // TODO: profile only permit_handle and thread_id
    return TCM_RESULT_SUCCESS;
  }

  uint32_t platform_resources() const {
    return initially_available_concurrency;
  }

protected:
  tcm_permit_epoch_t prepare_permit_copying(tcm_permit_handle_t ph) const {
    return ph->epoch.load(std::memory_order_acquire);
  }

  bool has_copying_succeeded(tcm_permit_handle_t permit_handle, tcm_permit_epoch_t c) const {
    return c == permit_handle->epoch.load(std::memory_order_relaxed);
  }

  bool is_safe_to_copy(const tcm_permit_epoch_t& e) const { return e % 2 == 0; }

  bool try_copy_permit(tcm_permit_handle_t ph, tcm_permit_t* permit) const {
    tcm_permit_epoch_t e = prepare_permit_copying(ph);

    if (!is_safe_to_copy(e))
      return false; // someone else is modifying this permit

    tcm_permit_data_t& pd = ph->data;

    __TCM_ASSERT(permit->concurrencies, "Permit concurrencies field contains null pointer.");
    __TCM_ASSERT(permit->size == pd.size, "Permit and request size inconsistency.");
    __TCM_ASSERT(!permit->cpu_masks || pd.cpu_mask,
                 "Permit does not have CPU mask(s) while their copy is requested.");

    const bool copy_masks = bool(permit->cpu_masks);
    for (uint32_t i = 0; i < pd.size; ++i) {
      permit->concurrencies[i] = pd.concurrency[i].load(std::memory_order_relaxed);

      if (copy_masks) {
        __TCM_ASSERT(
            bool(permit->cpu_masks[i]),
            std::string("Pointer to copy " + std::to_string(i) + "-th mask to is nullptr").c_str()
        );
        __TCM_ASSERT(
            bool(pd.cpu_mask[i]),
            std::string("Pointer to copy " + std::to_string(i) + "-th mask from is nullptr").c_str()
        );

        hwloc_bitmap_copy(permit->cpu_masks[i], pd.cpu_mask[i]);
      }
    }
    auto state_to_copy = get_permit_state(pd);
    if (!is_pending(state_to_copy) && is_nested(ph)) {
      const auto constraint_idx = pd.inherited_concurrency_idx.load(std::memory_order_relaxed);
      permit->concurrencies[constraint_idx] += 1;
    }

    permit->size = pd.size;
    permit->state = state_to_copy;
    permit->flags = pd.flags;

    return has_copying_succeeded(ph, e);
  }

#if TCM_USE_ASSERT
  // Helper to determine whether the permit is not released yet. Must be called under data_mutex.
  bool is_valid(tcm_permit_handle_t ph) const {
      return
          ph &&
          (pending_permits.cend() != std::find(pending_permits.cbegin(), pending_permits.cend(), ph)
           || idle_permits.cend() != std::find(idle_permits.cbegin(), idle_permits.cend(), ph)
           || active_permits.cend() != std::find(active_permits.cbegin(), active_permits.cend(), ph)
           || is_inactive(get_permit_state(ph->data)) || !participates_in_subscription_compute(ph));
  }
#endif

  bool skip_permit_negotiation(tcm_permit_handle_t ph, tcm_permit_handle_t initiator) const
  {
    if (ph == initiator)    // renegotiate for one that asked
      return false;

    const tcm_permit_data_t& pd = ph->data;
    const tcm_permit_state_t state = get_permit_state(pd);

    if (!is_negotiable(state, pd.flags)) {
      return true;
    }

    if (is_active(state))
      return false;

    if (is_pending(state))
      return false;

    return true;
  }

    // TODO: rename "try_satisfy" method into something like "calculate mask occupancy"

    // Tries to satisfy requested concurrency on the specific mask
    negotiable_snapshot_t try_satisfy(tcm_permit_handle_t ph,
                                      const tcm_cpu_constraints_t& constraint,
                                      const uint32_t current_concurrency, tcm_cpu_mask_t mask)
    {
        __TCM_ASSERT(constraint.min_concurrency >= 0, "Cannot satisfy indefinite constraint.");
        const uint32_t constraint_min = constraint.min_concurrency;

        __TCM_ASSERT(constraint.max_concurrency > 0, "Cannot satisfy indefinite constraint.");
        const uint32_t constraint_max = constraint.max_concurrency;

        return try_satisfy(ph, constraint_min, constraint_max, current_concurrency, mask);
    }

    negotiable_snapshot_t try_satisfy(tcm_permit_handle_t ph,
                                      const uint32_t constraint_min, const uint32_t constraint_max,
                                      const uint32_t current_concurrency, tcm_cpu_mask_t mask)
    {
        negotiable_snapshot_t stakeholders;
        stakeholders.set_adjusted_concurrencies(constraint_min, constraint_max);

        // TODO: cache masks
        tcm_cpu_mask_t per_constraint_union_mask = hwloc_bitmap_alloc();
        tcm_cpu_mask_t common_mask = hwloc_bitmap_alloc();
        __TCM_ASSERT(per_constraint_union_mask && common_mask,
                     "HWLOC was unable to allocate the bitmap(s).");
        std::unique_ptr<tcm_cpu_mask_t, cpu_mask_deleter_t> unique_result_mask(&per_constraint_union_mask);
        std::unique_ptr<tcm_cpu_mask_t, cpu_mask_deleter_t> unique_common_mask(&common_mask);

        hwloc_bitmap_or(common_mask, common_mask, mask);
        uint32_t common_concurrency = 0;

        // min_required is the maximum among the amount of unavailable resources needed
        // to satisfy the required concurrency (i.e. constraint.min_concurrency).
        // max_desired is the maximum among the amount of resources needed to satisfy the
        // desired concurrency (i.e. constraint.max_concurrency). It is set only when
        // min_required is unset (i.e. required can be satisfied).
        uint32_t min_required = 0, max_desired = 0; // TODO: these are calculated but not
                                                    // used.

        __TCM_ASSERT(constraint_max >= current_concurrency, "Incorrectly satisfied constraint found");
        uint32_t available_concurrency_snapshot = available_concurrency;
        // Holds the number of resources available immediately, not requiring any negotiations
        int available_min =
            std::min(available_concurrency_snapshot, constraint_max - current_concurrency);

        // The following is not an exhaustive search, but a reasonable trade-off between
        // correct resource tracking and algorithm complexity.
        // TODO: Consider implementing other approaches if needed.

        // Going through masks of existing permits one by one, compute mask subscription
        // and try squeezing more resources out of it.

        // Masks that do not intersect when separately applied
        std::queue<stakeholder_t> separate_masks;

        // TODO: rename 'stakeholders' to 'resource_competitors'

        // TODO: refactor lambda-function to avoid side-effects
        auto find_resource_competitors = [&](auto& permit_handles) {
            for (const tcm_permit_handle_t& ph_i : permit_handles) {
                tcm_permit_request_t& req_i = ph_i->request;
                tcm_permit_data_t& pd_i = ph_i->data;
                tcm_permit_state_t ph_i_state = get_permit_state(pd_i);

                __TCM_ASSERT(is_owning_resources(ph_i_state), "Nothing to negotiate resources from");

                if (ph_i == ph)
                    continue;   // The being satisfied permit is not a resource-competitor to itself

                for (unsigned constr_idx = 0; constr_idx < pd_i.size; ++constr_idx) {
                    // Start as if it is a permit with unconstrained request
                    tcm_cpu_mask_t cpu_mask = process_mask;
                    int32_t required_concurrency = req_i.min_sw_threads;
                    int32_t index = negotiable_snapshot_t::among_all_constraints;
                    if (is_constrained(ph_i)) { // If it turned out to be a permit with constraints
                        __TCM_ASSERT(pd_i.cpu_mask[constr_idx], "Mask must be present for each subconstraint.");
                        cpu_mask = pd_i.cpu_mask[constr_idx];
                        required_concurrency = req_i.cpu_constraints[constr_idx].min_concurrency;
                        index = constr_idx;
                    }

                    const uint32_t granted =
                        pd_i.concurrency[constr_idx].load(std::memory_order_relaxed);
                    __TCM_ASSERT(int32_t(granted) >= required_concurrency,
                                 "An invalid grant was found.");

                    uint32_t negotiable = granted;
                    if (!is_idle(ph_i_state)) {
                        // For active permits can only negotiate up to not less than required
                        negotiable -= required_concurrency;
                    }

                    stakeholder_t stakeholder{ph_i, index, negotiable};

                    // Determines whether the stakeholder actually contributes to the subscription
                    // of the interested part of the platform and can be negotiated, so that it is
                    // added to the stakeholders list.
                    bool add = false;

                    // Try fitting for an individual mask
                    if (hwloc_bitmap_intersects(mask, cpu_mask)) {
                        hwloc_bitmap_or(per_constraint_union_mask, mask, cpu_mask);
                        const int mc = get_oversubscribed_mask_concurrency(per_constraint_union_mask);
                        __TCM_ASSERT(uint32_t(mc) >= granted, "Incorrectly granted permit detected.");
                        const int available = mc - granted;
                        available_min = std::min(available_min, available);
                        const auto fitting_result =
                            try_fit_concurrency(constraint_min, constraint_max, available);
                        if (!fitting_result.is_required_satisfied) {
                            min_required = std::max(min_required, fitting_result.needed_concurrency);
                        } else if (min_required == 0) { // not oversubscribed yet
                            max_desired = std::max(max_desired, fitting_result.needed_concurrency);
                        }
                        if (is_negotiable(ph_i_state, pd_i.flags)) {
                            add = true;
                        }
                    }

                    // Try fitting into compound masks
                    if (hwloc_bitmap_intersects(common_mask, cpu_mask)) {
                        // TODO: extract common with the above part
                        hwloc_bitmap_or(common_mask, common_mask, cpu_mask);
                        const int mc = get_oversubscribed_mask_concurrency(common_mask);
                        common_concurrency += granted;
                        __TCM_ASSERT(uint32_t(mc) >= common_concurrency,
                                     "Incorrectly granted permit detected.");
                        const int available = mc - common_concurrency;
                        available_min = std::min(available_min, available);
                        const auto fitting_result =
                            try_fit_concurrency(constraint_min, constraint_max, available);
                        if (!fitting_result.is_required_satisfied) {
                            min_required = std::max(min_required, fitting_result.needed_concurrency);
                        } else if (min_required == 0) { // not oversubscribed yet
                            max_desired = std::max(max_desired, fitting_result.needed_concurrency);
                        }
                        if (is_negotiable(ph_i_state, pd_i.flags)) {
                            add = true;
                        }
                    } else {
                        separate_masks.push(stakeholder);
                    }

                    // Adding only IDLE stakeholders if required concurrency has already been
                    // satisfied for the permit, for which resources are being searched
                    if (add) {
                        stakeholders.add(stakeholder, ph_i_state);
                    }
                } // for each mask in a permit
            } // for each existing permit
        };

        find_resource_competitors(idle_permits);
        find_resource_competitors(active_permits);

        auto find_leftover_competitors = [&] {
            // try applying the masks that did not intersect previously, but might
            // started intersecting after the loop above completes
            std::size_t num_processed_masks = separate_masks.size();
            bool has_union_applied = false;
            while (!separate_masks.empty()) {
                auto& stakeholder = separate_masks.front();
                const tcm_permit_data_t &pd_i = stakeholder.ph->data;

                tcm_cpu_mask_t m = process_mask; // Use process mask for unconstrained requests
                if (pd_i.cpu_mask)
                    m = pd_i.cpu_mask[stakeholder.constraint_index];

                if (hwloc_bitmap_intersects(common_mask, m)) {
                    // TODO: extract common with the above part
                    hwloc_bitmap_or(common_mask, common_mask, m);
                    const int mc = get_oversubscribed_mask_concurrency(common_mask);
                    const auto c =
                        pd_i.concurrency[stakeholder.constraint_index].load(std::memory_order_relaxed);
                    common_concurrency += c;
                    __TCM_ASSERT(uint32_t(mc) >= common_concurrency,
                                 "Incorrectly granted permit is detected.");

                    const int available = mc - common_concurrency;
                    available_min = std::min(available_min, available);
                    auto fitting_result = try_fit_concurrency(constraint_min, constraint_max, available);
                    if (!fitting_result.is_required_satisfied)
                        min_required = std::max(min_required, fitting_result.needed_concurrency);
                    else if (min_required == 0) { // not oversubscribed yet
                        max_desired = std::max(max_desired, fitting_result.needed_concurrency);
                    }

                    const tcm_permit_state_t ph_i_state = get_permit_state(pd_i);
                    if (is_negotiable(ph_i_state, pd_i.flags)) {
                        stakeholders.add(stakeholder, ph_i_state);
                    }
                    has_union_applied = true;
                } else if (separate_masks.size() != 1) {
                    // it might intersect after uniting other masks
                    separate_masks.push(stakeholder);
                }

                separate_masks.pop();

                if (--num_processed_masks > 0)
                    continue;

                if (has_union_applied) {
                    has_union_applied = false;
                    num_processed_masks = separate_masks.size();
                } else {
                    // we've gone through all the masks in the container and failed to find
                    // any other mask that intersects with the current union of masks
                    // (common mask)
                    break;
                }
            }
        };
        find_leftover_competitors();

        // Add current concurrency to treat it as the immediately available
        stakeholders.set_immediately_available(current_concurrency + available_min);

        return stakeholders;
    }

    system_topology& platform_topology(int* num_numa_nodes, int** numa_indices,
                      int* num_core_types, int** core_types_indices)
    {
        // TODO: Move getting process topology information into TCM initialization phase.
        system_topology& topology = system_topology::instance();
        topology.fill_topology_information(*num_numa_nodes, *numa_indices, *num_core_types, *core_types_indices);

        __TCM_ASSERT(numa_indices, "Numa indices array was not initialized.");
        __TCM_ASSERT(core_types_indices, "Core types indices array was not initialized.");
        __TCM_ASSERT(*num_numa_nodes > 0, "At least one NUMA node should be present.");
        __TCM_ASSERT(*num_core_types > 0, "At least one core type should be present.");
        return topology;
    }

    negotiable_snapshot_t try_satisfy_high_level_constraints(tcm_permit_handle_t ph,
                                                             const tcm_cpu_constraints_t& constraint,
                                                             const uint32_t current_concurrency,
                                                             tcm_cpu_mask_t pd_mask)
    {
        __TCM_ASSERT(!constraint.mask, "Constraint mask must not exist.");
        __TCM_ASSERT(constraint.min_concurrency >= 0,
                     "Constraint's min_concurrency must be known.");
        const uint32_t constraint_min = uint32_t(constraint.min_concurrency);

        int num_numa_nodes = 0, num_core_types = 0;
        int* numa_indices = nullptr; int* core_types_indices = nullptr;
        system_topology& topology = platform_topology(&num_numa_nodes, &numa_indices,
                                                      &num_core_types, &core_types_indices);

        negotiable_snapshot_t result_snapshot;
        int32_t result_max_concurrency = 0;

        if (constraint.numa_id == tcm_any) {
            bool has_assigned_once = false;
            bool is_desired_satisfied = false;

            tcm_cpu_mask_t result_mask = hwloc_bitmap_alloc();
            std::unique_ptr<tcm_cpu_mask_t, cpu_mask_deleter_t> result_mask_guard(&result_mask);
            tcm_cpu_mask_t temp_mask = hwloc_bitmap_alloc();
            std::unique_ptr<tcm_cpu_mask_t, cpu_mask_deleter_t> temp_mask_guard(&temp_mask);

            for (int i = 0; i < num_numa_nodes; ++i) {
                // TODO: separate mask filling and attempts of its satisfaction.
                topology.fill_constraints_affinity_mask(temp_mask, numa_indices[i],
                                                        constraint.core_type_id,
                                                        constraint.threads_per_core);

                // TODO: Subtract the masks from other subconstraints here so that two subconstrains
                // do not occupy the same mask

                if ( hwloc_bitmap_iszero(temp_mask) )
                    continue;   // The result mask is empty, continue with the next numa node

                const uint32_t constraint_max = infer_constraint_max_concurrency(
                    constraint.max_concurrency, /*fallback_value*/process_concurrency, temp_mask
                );

                __TCM_ASSERT(constraint_min <= constraint_max, "Broken concurrency in constraint");

                negotiable_snapshot_t stakeholders = try_satisfy(ph, constraint_min, constraint_max,
                                                                 current_concurrency, temp_mask);

                // TODO: move snapshot selection in separate function.
                // Among the masks satisfying the request, choosing the one with smaller
                // resources availability to maximize probability of satisfying other
                // subconstraints that might require more resources.
                if (is_desired_satisfied && constraint_max <= stakeholders.num_available()) {
                    // Both masks can satisfy the maximum requested resources. Trying to find
                    // the one with smaller total availability, but keeping those requiring
                    // lesser negotiations.
                    if (constraint_max <= result_snapshot.num_immediately_available() &&
                        constraint_max <= stakeholders.num_immediately_available())
                    {
                        // Both results satisfy desired concurrency without negotiations
                        if (stakeholders.num_available() < result_snapshot.num_available()) {
                            // Found the mask with smaller availability
                            result_snapshot = stakeholders;
                            hwloc_bitmap_copy(result_mask, temp_mask);
                        }
                    } else if (result_snapshot.num_immediately_available()
                               < stakeholders.num_immediately_available())
                    {
                        result_snapshot = stakeholders; // Found the mask with lesser negotiations
                        hwloc_bitmap_copy(result_mask, temp_mask);
                    }
                } else if (!has_assigned_once ||
                           result_snapshot.num_available() < stakeholders.num_available())
                {
                    has_assigned_once = true;
                    is_desired_satisfied = constraint_max <= stakeholders.num_available();
                    result_snapshot = stakeholders;
                    hwloc_bitmap_copy(result_mask, temp_mask);
                }
            } // for each numa index
            if (constraint_min <= result_snapshot.num_available()) {
                // Able to satisfy minimum for this constraint, remember the mask. Max concurrency
                // gets re-inferred at a later point
                hwloc_bitmap_copy(pd_mask, result_mask);
            }
        } else {
            // NUMA node ID specified explicitly or left default
            topology.fill_constraints_affinity_mask(pd_mask, constraint.numa_id,
                                                    constraint.core_type_id,
                                                    constraint.threads_per_core);

            __TCM_ASSERT(!hwloc_bitmap_iszero(pd_mask),
                         "Intersection of constraint masks filtered out all resources");

            result_max_concurrency = infer_constraint_max_concurrency(
              constraint.max_concurrency, /*fallback_value*/process_concurrency, pd_mask
            );
            __TCM_ASSERT(result_max_concurrency > 0, "Incorrect invariant.");
            __TCM_ASSERT(constraint_min <= uint32_t(result_max_concurrency),
                         "Broken concurrency in constraint");

            result_snapshot = try_satisfy(ph, constraint_min, uint32_t(result_max_concurrency),
                                          current_concurrency, pd_mask);
        }

        if (result_max_concurrency > 0) {
            // Remember desired concurrency for this constraint
            int32_t& constraint_max_concurrency = const_cast<int32_t&>(constraint.max_concurrency);
            constraint_max_concurrency = result_max_concurrency;
        }

        return result_snapshot;
    }

    struct stakeholder_cache {
        stakeholder_cache(std::size_t stakeholders_size)
                : total_immediately_available(0), total_negotiable(0)
                , stakeholders{stakeholders_size} {}

        uint32_t total_immediately_available = 0;
        uint32_t total_negotiable = 0;
        uint32_t adjusted_min_sw_threads = 0;
        uint32_t adjusted_max_sw_threads = 0;
        std::vector<negotiable_snapshot_t> stakeholders; // Per constraint negotiable snapshot.
    };
    // Loops over all the constraints in the given array and populate the list of negotiable
    // snapshots.
    // TODO: rename to "calculate constraint snapshots" or something
    void try_satisfy_constraints(stakeholder_cache& sc, const tcm_permit_request_t& req,
                                 tcm_permit_handle_t ph)
    {
        tcm_cpu_mask_t* cpu_masks = new tcm_cpu_mask_t[req.constraints_size]{nullptr};
        for (uint32_t i = 0; i < req.constraints_size; ++i) {
            cpu_masks[i] = hwloc_bitmap_alloc();
            __TCM_ASSERT(cpu_masks[i], "hwloc_bitmap_alloc() failed to allocate bitmap");
        }
        std::unique_ptr<tcm_cpu_mask_t[], cpu_mask_array_deleter_t> masks_guard(
            cpu_masks, req.constraints_size
        );
        std::vector<uint32_t> indeterminate_constraint_indices;
        uint32_t num_satisfiable = 0;
        tcm_permit_data_t& pd = ph->data;
        for (uint32_t constr_idx = 0; constr_idx < req.constraints_size; ++constr_idx) {
            const tcm_cpu_constraints_t& constraint = req.cpu_constraints[constr_idx];
            __TCM_ASSERT(
                constraint.max_concurrency == tcm_automatic ||
                constraint.min_concurrency <= constraint.max_concurrency,
                "Invalid constraint."
            );

            negotiable_snapshot_t& snapshot = sc.stakeholders[constr_idx];
            tcm_cpu_mask_t& pd_mask = pd.cpu_mask[constr_idx];
            const bool is_mask_set = !hwloc_bitmap_iszero(pd_mask);
            const uint32_t current_concurrency = pd.concurrency[constr_idx]
                .load(std::memory_order_relaxed);
            if (constraint.mask || is_mask_set) {
                if (!is_mask_set) {
                    // Assigning the mask for the first time, it should not change afterwards
                    __TCM_ASSERT(!hwloc_bitmap_iszero(constraint.mask),
                                 "No mask to intersect with.");
                    hwloc_bitmap_and(pd_mask, constraint.mask, process_mask);
                }

                int32_t& constraint_max = const_cast<int32_t&>(constraint.max_concurrency);
                constraint_max = infer_constraint_max_concurrency(
                    constraint_max, /*fallback_value*/process_concurrency, pd_mask
                );
                __TCM_ASSERT(constraint.max_concurrency > 0, "Incorrect invariant");

                snapshot = try_satisfy(ph, constraint, current_concurrency, pd_mask);
            } else {
                // assume hwloc_bitmap_copy works in case the same mask is passed as its
                // src and dst arguments
                snapshot = try_satisfy_high_level_constraints(ph, constraint, current_concurrency,
                                                              /*temp_mask*/cpu_masks[constr_idx]);
                indeterminate_constraint_indices.push_back(constr_idx);
            }

            uint32_t able_to_satisfy = std::min( // works even if max_concurrency is negative
                uint32_t(constraint.max_concurrency), snapshot.num_available()
            );
            __TCM_ASSERT(constraint.min_concurrency >= 0, "Incorrect invariant");
            if (uint32_t(constraint.min_concurrency) <= able_to_satisfy) {
                num_satisfiable += able_to_satisfy;
                //snapshot.num_immediately_available() + snapshot.num_negotiable_idle();
            }

            sc.total_negotiable += snapshot.num_negotiable();
            sc.total_immediately_available += snapshot.num_immediately_available();
        }
        __TCM_ASSERT(req.min_sw_threads >= 0, "Required concurrency is unknown");
        if (uint32_t(req.min_sw_threads) <= num_satisfiable) {
            // Able to satisfy demand for required resources, pin down the masks for high level
            // constraints description
            for (auto constraint_index : indeterminate_constraint_indices) {
                tcm_cpu_mask_t destination = pd.cpu_mask[constraint_index];
                tcm_cpu_mask_t source = cpu_masks[constraint_index];
                const int result = hwloc_bitmap_copy(destination, source);
                __TCM_ASSERT_EX(result == 0, "Error copying masks");
                int32_t& max_concurrency = req.cpu_constraints[constraint_index].max_concurrency;
                max_concurrency = infer_constraint_max_concurrency(
                    max_concurrency, /*fallback_value*/process_concurrency, destination
                );
            }
        }
    }


    struct fulfillment_decision_t { // per constraint decision
        uint32_t to_assign = 0; // Concurrency to set
        uint32_t to_negotiate = 0; // Concurrency that needs to be negotiated out from the permits.
                                   // Its value is included into the concurrency field.

        renegotiable_resources_queue_t idle_permits; // Contributing permits in IDLE state
        renegotiable_resources_queue_t active_permits; // Contributing permits in ACTIVE state
    };

    struct fulfillment_t {
        fulfillment_t(std::size_t size = 0)
            : num_satisfiable(0), num_negotiable(0), num_negotiable_active(0),
              pending_constraints_indices(0), decisions(size) {}

        uint32_t num_satisfiable;      // the maximum number of resources that can be satisfied. If
                                       // less than min_sw_threads, the permit cannot be activated,
                                       // hence must be left in the PENDING state.

        uint32_t num_negotiable;  // the amount of resources required to negotiate before activating
                                  // the permit. (included into num_satisfiable)

        uint32_t num_negotiable_active;  // the amount of negotiable resources from active permits
                                         // (included into num_negotiable)

        std::vector<int> pending_constraints_indices; // indices of the constraints array whose
                                                      // required concurrency cannot be satisfied.
        std::vector<fulfillment_decision_t> decisions; // per constraint prescription
        uint32_t to_inherit_idx = 0; // Index of the constraint which inherits the primary thread from outer permit
    };

    // Distributes as max as possible of the desired concurrency
    // TODO: rename to something like 'request_saturation'?
    fulfillment_t calculate_updates(tcm_permit_handle_t ph,
                                    const stakeholder_cache& cache)
    {
        __TCM_PROFILE_THIS_FUNCTION();

        const std::vector<negotiable_snapshot_t>& sh = cache.stakeholders;
        fulfillment_t fulfillment(/*decisions array size*/sh.size());
        std::vector<fulfillment_decision_t>& decision = fulfillment.decisions;

        // To try satisfying a constrained request means distributing as much as possible resources
        // from the [min_sw_threads, max_sw_threads] interval among buckets specified through
        // constraints array. We call the request "satisfied" if we are able to distribute its
        // desired concurrency, that is max_sw_threads.
        // For nested requests, the number of resources to find is decremented by 1 due to
        // reuse of the primary thread from parent permit.
        uint32_t left_to_find = cache.adjusted_min_sw_threads;
        bool has_implicit_resource = is_nested(ph);
        // Distributing the required concurrency first
        for (std::size_t i = 0; i < sh.size() && (left_to_find || has_implicit_resource); ++i) {
            const negotiable_snapshot_t& cns = sh[i]; // negotiable snapshot for single constraint
            uint32_t to_find_i = std::min(left_to_find, cns.adjusted_max_concurrency());
            uint32_t new_concurrency = std::min(cns.num_immediately_available(), to_find_i);

            uint32_t to_negotiate = to_find_i - new_concurrency;
            to_negotiate = std::min(cns.num_negotiable(), to_negotiate);

            new_concurrency += to_negotiate; // Concurrency that was found for the constraint

            decision[i].to_negotiate = to_negotiate;
            fulfillment.num_negotiable += to_negotiate;
            fulfillment.num_negotiable_active +=
                std::max(0, int32_t(to_negotiate) - int32_t(cns.num_negotiable_idle()));

            if (new_concurrency < cns.adjusted_min_concurrency()) {
                // Last resort to satisfy the minimum concurrency requirement for the constraint
                // is to inherit resource of a primary thread from the parent permit.
                // Otherwise the permit is going to be left in PENDING state.
                if (new_concurrency + uint32_t(has_implicit_resource) >= cns.adjusted_min_concurrency()) {
                    fulfillment.to_inherit_idx = (uint32_t)i;
                    has_implicit_resource = false;
                } else {
                    // Cannot negotiate necessary amount of resources for constraint. The permit is
                    // going to be left in PENDING state.
                    fulfillment.pending_constraints_indices.push_back(static_cast<int>(i));
                    continue;
                }
            }

            decision[i].to_assign = new_concurrency;
            fulfillment.num_satisfiable += new_concurrency;

            __TCM_ASSERT(new_concurrency <= left_to_find,
                         (std::string("Incorrect calculation of concurrency to assign for the ") +
                          std::to_string(i) + std::string("-th constraint of permit handle ") +
                          std::to_string(std::uintptr_t(ph))).c_str());
            suppress_unused_warning(ph);
            left_to_find -= new_concurrency;

            decision[i].idle_permits = cns.idle_permits();
            decision[i].active_permits = cns.active_permits();
        }

        if (left_to_find > 0 || !fulfillment.pending_constraints_indices.empty()) {
            // Not able to satisfy demand for required resources either overall or on a single
            // constraint
            return fulfillment;
        }

        // TODO: Improve the algorithm in the following:
        // 1) Distribute the resources which do not require negotiation first (i.e.
        //    cache.total_immediately_available())
        // 2) Balance the left to be negotiated to avoid end up having to distribute the amount
        //    that is less than any subconstraint.min_concurrency

        // Try to find resources when minimum has already been satisfied. Negotiate only from IDLE,
        // since the resources there are not used
        left_to_find = cache.adjusted_max_sw_threads - cache.adjusted_min_sw_threads;
        for (std::size_t i = 0; i < sh.size() && left_to_find; ++i) {
            const negotiable_snapshot_t& cns = sh[i];

            if (has_implicit_resource) {
                // Primary thread has not been yet accounted, trying to squeeze it where possible
                if (decision[i].to_assign < cns.adjusted_max_concurrency()) {
                    fulfillment.to_inherit_idx = (uint32_t)i;
                    has_implicit_resource = false;
                }
            }

            __TCM_ASSERT(decision[i].to_assign >= decision[i].to_negotiate, "Underflow detected");
            const uint32_t allocated_from_available = decision[i].to_assign - decision[i].to_negotiate;
            const bool is_immediate_resources_available =
                (cns.num_immediately_available() - allocated_from_available) > 0;

            const bool is_idle_resources_available =
                decision[i].to_negotiate < cns.num_negotiable_idle();

            if (!is_immediate_resources_available && !is_idle_resources_available) {
                // Satisfying minimum requires negotiation beyond IDLE resources. Do not grab more
                // than required in this constraint then.
                continue;
            }

            // Satisfying minimum for this constraint does not require negotiation. Try satisfying
            // more to reach desired concurrency, still without negotiating active, but possibly
            // idle permits.
            uint32_t to_find_i = cns.adjusted_max_concurrency() - cns.adjusted_min_concurrency();
            to_find_i = std::min(left_to_find, to_find_i);

            const uint32_t available_i = std::max(
                0, int32_t(cns.num_immediately_available()) - int32_t(decision[i].to_assign)
            );
            auto assign_further = std::min(available_i, to_find_i);
            to_find_i -= assign_further;

            const uint32_t to_negotiate = std::min(
                to_find_i, cns.num_negotiable_idle() - decision[i].to_negotiate
            );

            assign_further += to_negotiate; // Concurrency found for i-th constraint, some of which
                                            // needs to be negotiated

            // Do not negotiate from active permits if minimum has been satisfied, but continue
            // negotiating idle permits if desired concurrency not reached.
            decision[i].to_assign += assign_further;
            fulfillment.num_satisfiable += assign_further;
            decision[i].to_negotiate += to_negotiate;
            fulfillment.num_negotiable += to_negotiate;

            __TCM_ASSERT(assign_further <= left_to_find,
                         (std::string("Trying to satisfy more than needed for the ") +
                          std::to_string(i) + std::string("-th constraint of permit handle ") +
                          std::to_string(std::uintptr_t(ph))).c_str());

            left_to_find -= assign_further;

            // Adding only IDLE contributors since required resources has been already satisfied in
            // the previous loop
            decision[i].idle_permits = cns.idle_permits();
        }

        __TCM_ASSERT(fulfillment.num_negotiable_active <= fulfillment.num_negotiable,
                     "Number of negotiated active must be included into total negotiable.");

        // for (std::size_t i = 0; i < sh.size() && left_to_find; ++i) {
        //     // TODO: Rewrite the algorithm to fix the following:
        //     // 1) Distribute the resources which do not require negotiation first (i.e.
        //     //    cache.total_immediately_available())
        //     // 2) Balance the left to be negotiated to avoid end up having to distribute the amount
        //     //    that is less than any subconstraint.min_concurrency
        //     uint32_t to_find_i = std::min(left_to_find, sh[i].adjusted_max_concurrency());
        //     uint32_t new_concurrency = std::min(sh[i].num_immediately_available(), to_find_i);

        //     uint32_t to_negotiate = to_find_i - new_concurrency;
        //     to_negotiate = std::min(sh[i].num_negotiable(), to_negotiate);

        //     new_concurrency += to_negotiate; // Concurrency that was found for the constraint

        //     decision[i].to_negotiate = to_negotiate;
        //     fulfillment.num_negotiable += to_negotiate;

        //     if (new_concurrency < sh[i].adjusted_min_concurrency()) {
        //         // Cannot negotiate necessary amount of resources for constraint. The permit is
        //         // going to be left in PENDING state.
        //         fulfillment.pending_constraints_indices.push_back(i);
        //         // Negative value means cannot satisfy required concurrency.
        //         decision[i].need = new_concurrency - sh[i].adjusted_min_concurrency();
        //     } else {
        //         // Positive value means cannot satisfy desired concurrency.
        //         decision[i].need = sh[i].adjusted_max_concurrency() - new_concurrency;
        //     }
        //     decision[i].to_assign = new_concurrency;
        //     fulfillment.num_satisfiable += new_concurrency;

        //     __TCM_ASSERT(new_concurrency <= left_to_find,
        //                  (std::string("Incorrect calculation of concurrency to assign for the ") +
        //                   std::to_string(i) + std::string("-th constraint of permit handle ") +
        //                   std::to_string(std::uintptr_t(ph))).c_str());
        //     left_to_find -= new_concurrency;

        //     decision[i].permits = sh[i].get_contributing_permits();
        // }

        return fulfillment;
    }

    std::vector<permit_change_t> negotiate(fulfillment_t& f, const tcm_permit_request_t& /*req*/,
                                           const tcm_permit_handle_t& ph)
    {
        __TCM_PROFILE_THIS_FUNCTION();

        std::vector<permit_change_t> result;
        permit_change_t requested_permit{ph, TCM_PERMIT_STATE_ACTIVE, {}, f.to_inherit_idx};
        std::vector<uint32_t>& requested_permit_concurrencies = requested_permit.new_concurrencies;
        std::unordered_multimap<tcm_permit_handle_t, permit_change_t> new_grants;
        std::unordered_set<tcm_permit_handle_t> handles;

        for (fulfillment_decision_t& fd : f.decisions) {
            requested_permit_concurrencies.push_back(fd.to_assign);

            auto update_stakeholder_concurrency = [&](auto& permits) {
                // Minimizing the number of negotiations
                while (fd.to_negotiate && !permits.empty()) { // There is something to negotiate for
                                                              // current constraint from
                                                              // contributing permits
                    stakeholder_t st = permits.top(); permits.pop();

                    uint32_t current_negotiation = std::min(fd.to_negotiate, st.num_negotiable);

                    tcm_permit_data_t& st_data = st.ph->data;
                    std::vector<uint32_t> new_concurrencies{st_data.size};
                    // TODO: introduce "dump_concurrencies" helper
                    for (std::size_t i = 0; i < new_concurrencies.size(); ++i) {
                        new_concurrencies[i] = st_data.concurrency[i].load(std::memory_order_relaxed);
                    }
                    uint32_t minimum = st.ph->request.min_sw_threads;
                    if (st.constraint_index == negotiable_snapshot_t::among_all_constraints) {
                        st.constraint_index = 0;
                    } else {
                        __TCM_ASSERT(st.ph->request.cpu_constraints,
                                     "Accessing constraints of unconstrained request");
                        minimum = st.ph->request.cpu_constraints[st.constraint_index].
                            min_concurrency;
                    }

                    __TCM_ASSERT(current_negotiation <= new_concurrencies[st.constraint_index],
                                 "Underflow detected.");

                    new_concurrencies[st.constraint_index] -= current_negotiation;

                    tcm_permit_state_t new_state = get_permit_state(st_data);
                    const uint32_t actual_grant = new_concurrencies[st.constraint_index] +
                                                  /*inherited*/uint32_t(is_nested(st.ph));
                    const bool shall_deactivate =
                        is_rigid_concurrency(st_data.flags) || actual_grant < minimum;
                    if (shall_deactivate) {
                        std::fill(new_concurrencies.begin(), new_concurrencies.end(), 0);
                        new_state = TCM_PERMIT_STATE_INACTIVE;
                    }
                    permit_change_t pc{st.ph, new_state, new_concurrencies, {}};
                    new_grants.insert( std::make_pair(st.ph, pc) );

                    handles.insert(st.ph);

                    fd.to_negotiate -= current_negotiation;
                } // while there is something to negotiate
            };
            update_stakeholder_concurrency(fd.idle_permits);
            update_stakeholder_concurrency(fd.active_permits);
        } // for each constraint decision

        // Merging constraints negotiations for each permit handle
        for (tcm_permit_handle_t curr_ph : handles) {
            auto range = new_grants.equal_range(curr_ph);
            permit_change_t pc = range.first->second;
            std::vector<uint32_t>& concurrencies = pc.new_concurrencies;
            for (auto it = ++range.first; it != range.second; ++it) {
                auto& current_concurrencies = it->second.new_concurrencies;
                for (std::size_t i = 0; i < current_concurrencies.size(); ++i) {
                    // "merge" means to use negotiated value (which is less than the current)
                    if (current_concurrencies[i] < concurrencies[i]) {
                        // TODO: fix by summing up per constraint subtractions and subtract the sum
                        // from original constraint concurrency (i.e. from data.concurrency[i])
                        concurrencies[i] = current_concurrencies[i];
                    }
                }
            }
            result.push_back( std::move(pc) );
        }

        // The resources for the requested permit might be found in existing permits. To avoid
        // underflows on the count of available resources when applying gathered permit changes it
        // is imporant to reclaim necessary resources first and only after that assign these
        // reclaimed resources to the requested permit. This explains why the change for the
        // requested permit is added last to the permit changes container.
        result.push_back(std::move(requested_permit));
        return result;
    }

    uint32_t infer_desired_resources_num(const tcm_permit_request_t& req) {
        __TCM_ASSERT(req.max_sw_threads == tcm_automatic, "Nothing to infer");
        __TCM_ASSERT(req.constraints_size > 0,
                     "For non-constrained requests desired amount of resources must be known");

        uint32_t sum_max_concurrency = 0;
        for (uint32_t i = 0; i < req.constraints_size; ++i) {
            tcm_cpu_constraints_t& c = req.cpu_constraints[i];

            __TCM_ASSERT(c.max_concurrency > 0, "Desired constraint concurrency is unknown");
            sum_max_concurrency += c.max_concurrency;
        }

        return std::min(sum_max_concurrency, process_concurrency);
    }

    bool has_masks_set(const tcm_permit_handle_t permit_handle) {
        bool has_any_mask_empty = false;
        const tcm_permit_data_t& permit_data = permit_handle->data;
        for (uint32_t i = 0; i < permit_data.size; ++i) {
            has_any_mask_empty |= bool(hwloc_bitmap_iszero(permit_data.cpu_mask[i]));
            __TCM_ASSERT(has_any_mask_empty ||
                         permit_handle->request.cpu_constraints[i].max_concurrency > 0,
                         "Constraint max concurrency is unknown");
        }
        return !has_any_mask_empty;
    }

    //! Tries to meet the requested concurrency. Must be called under the data_mutex.
    //! Returns @fulfillment_t
    fulfillment_t try_satisfy_request(const tcm_permit_request_t& req, tcm_permit_handle_t ph,
                                      uint32_t available_concurrency_snapshot)
    {
        // TODO: Determine whether permit can have dynamic flags, i.e. flags can be
        // changed during resource re-requesting. Otherwise, copying of flags seems
        // unnecessary anywhere else except for the first time resources are
        // requested and representation of corresponding permit is allocated.

        __TCM_PROFILE_THIS_FUNCTION();


        int32_t primary_thread = is_nested(ph) ? 1 : 0;
        int32_t adjusted_min_sw_threads = std::max(0, req.min_sw_threads-primary_thread);
        int32_t adjusted_max_sw_threads;

        stakeholder_cache sc{/*constraints array length*/ph->data.size};
        if (is_constrained(ph) && process_mask) {
            __TCM_ASSERT(req.constraints_size > 0, "Size of constraints array is not specified.");
            __TCM_ASSERT(req.min_sw_threads >= 0, "Wrong assumption");
            try_satisfy_constraints(sc, req, ph);
            const bool has_determined_constraints = has_masks_set(ph);
            if (has_determined_constraints && tcm_automatic == req.max_sw_threads) {
                int32_t& max_sw_threads = const_cast<int32_t&>(req.max_sw_threads);
                max_sw_threads = infer_desired_resources_num(req);
                __TCM_ASSERT(req.max_sw_threads > 0, "Desired amount of resources is unknown");
            }
            adjusted_max_sw_threads = std::max(0, req.max_sw_threads-primary_thread);
        } else {
            __TCM_ASSERT(req.max_sw_threads >= 0, "Cannot satisfy indefinite request.");

            // TODO: Unify the approach by acting as if it is the permit with single constraint that
            // has process mask set as its cpu_mask. This allows reusing of the
            // try_satisfy_constraints() method

            const tcm_permit_data_t& pd = ph->data;
            const uint32_t current_concurrency = pd.concurrency[0].load(std::memory_order_relaxed);

            __TCM_ASSERT(1 == pd.size, "Act as if it is the permit with single constraint");

            adjusted_max_sw_threads = std::max(0, req.max_sw_threads-primary_thread);

            negotiable_snapshot_t& snapshot = sc.stakeholders[0];
            snapshot.set_immediately_available(current_concurrency + available_concurrency_snapshot);
            snapshot.set_adjusted_concurrencies(req.min_sw_threads, req.max_sw_threads);

            auto gather_stakeholders = [&](auto& permits) {
                for (const tcm_permit_handle_t& ph_i : permits) {
                    if (ph_i == ph) {
                        // This might be the permit for which the amount of allotted resources is
                        // reconsidered. Skip it in calculation of contributing stakeholders.
                        continue;
                    }

                    const tcm_permit_state_t ph_state = get_permit_state(ph_i->data);
                    const tcm_permit_flags_t ph_flags = ph_i->data.flags;

                    __TCM_ASSERT(is_owning_resources(ph_state),
                                 "Should gather only from permits that own resources");

                    if (!is_negotiable(ph_state, ph_flags))
                        continue;

                    uint32_t negotiable = 0;
                    if (is_idle(ph_state)) {
                        // IDLE are negotiated up to deactivation (i.e. grant < min_sw_threads)
                        negotiable = get_permit_grant(ph_i);
                    } else {
                        __TCM_ASSERT(!is_rigid_concurrency(ph_flags),
                                     "Active rigid concurrency permits cannot negotiate");
                        negotiable = get_num_negotiable(ph_i);
                    }

                    if (negotiable > 0) {
                        stakeholder_t stakeholder{
                            ph_i, negotiable_snapshot_t::among_all_constraints, negotiable
                        };
                        snapshot.add(stakeholder, ph_state);
                    }
                }
            };
            // Considering that all the permits affect the current one
            // TODO: break gathering of stats when necessary amount of resources has been found
            gather_stakeholders(idle_permits);
            __TCM_ASSERT(req.min_sw_threads >= 0, "Required number of resources should be deduced");
            if (current_concurrency < uint32_t(req.min_sw_threads))  {
                // Consider ACTIVE permits for negotiation as well since the required number of
                // resources is not yet found.
                gather_stakeholders(active_permits);
            }

            sc.total_negotiable = snapshot.num_negotiable();
            sc.total_immediately_available = snapshot.num_immediately_available();
        }

        sc.adjusted_min_sw_threads = adjusted_min_sw_threads;
        sc.adjusted_max_sw_threads = adjusted_max_sw_threads;

        const bool is_all_request_data_known = req.max_sw_threads > 0;
        if (is_all_request_data_known) {
            fulfillment_t fulfillment = calculate_updates(ph, sc);
            return fulfillment;
        }
        return {};
    }

    virtual void renegotiate_permits(tcm_permit_handle_t initiator) = 0;

    virtual std::vector<permit_change_t> adjust_existing_permit(const tcm_permit_request_t& req,
                                                                tcm_permit_handle_t permit) = 0;

    unsigned infer_permit_size(const tcm_permit_request_t& request) const {
        unsigned permit_size = sizeof(tcm_permit_rep_t);
        unsigned concurrency_array_size = sizeof(std::atomic<uint32_t>);
        if (request.cpu_constraints) {
            const uint32_t size = request.constraints_size;
            permit_size += size * sizeof(tcm_cpu_constraints_t); // request's constraints
            permit_size += size * sizeof(tcm_cpu_mask_t);        // permit's cpu_mask
            concurrency_array_size *= size;                      // permit's concurrency
        }
        permit_size += concurrency_array_size;
        return permit_size;
    }

    tcm_permit_handle_t
    make_new_permit(const tcm_client_id_t clid, const tcm_permit_request_t& req,
                    const tcm_permit_state_t state)
    {
        __TCM_PROFILE_THIS_FUNCTION();
        unsigned permit_size = infer_permit_size(req);

        // Pointing to a permit representation in memory as a byte array in order to simplify
        // pointer arithmetic
        char* permit_rep_as_byte_array = reinterpret_cast<char*>(std::malloc(permit_size));

        if (!permit_rep_as_byte_array) {
            return nullptr;
        }
        // No need for nullifying the allocated memory as it is fully initialized below. Some parts
        // of it are initialized from the request (e.g., the masks in the constraints array), for
        // the others - zero initializing constructors are called.

        /*
          Memory layout for internal permit representation (tcm_permit_rep_t):
          0x00 (lower memory address)
           ||
           ||   tcm_permit_rep_t* (aka tcm_permit_handle_t)
           || +---------------------------------------------+
           || |              tcm_permit_rep_t               |
           || +---------------------------------------------+
           || |    tcm_permit_rep_t->data.concurrency[]     |
           || +---------------------------------------------+
           || |     tcm_permit_rep_t->data.cpu_mask[]       | <- allotted iff constraints specified
           || +---------------------------------------------+
           || | tcm_permit_rep_t->request.cpu_constraints[] | <- allotted iff constraints specified
           || +---------------------------------------------+
          \||/
           \/
          0xFF (higher memory address)
        */
// Suppressing MSVC warning about possible buffer overrun: the writable size is 'permit_size' bytes,
// but 'XX' bytes might be written
__TCM_SUPPRESS_WARNING_WITH_PUSH(6386)
        tcm_permit_handle_t ph = new(permit_rep_as_byte_array) tcm_permit_rep_t;
__TCM_SUPPRESS_WARNING_POP
        ph->epoch.store(0, std::memory_order_relaxed);
        tcm_permit_data_t& pd = ph->data;

        pd.client_id = clid;

        using concurrency_t = decltype(pd.concurrency);
        using concurrency_item_t = std::remove_pointer_t<concurrency_t>;
        using mask_t = decltype(pd.cpu_mask);
        using constraints_t = decltype(req.cpu_constraints);

        const unsigned concurrency_offset = sizeof(tcm_permit_rep_t);

        pd.concurrency = reinterpret_cast<concurrency_t>(
            permit_rep_as_byte_array + concurrency_offset
        );

        pd.size = 1;
        pd.cpu_mask = nullptr;

        // Avoid dependency on the client memory allocated for permit request. It helps to avoid:
        //
        //   a) Crashes in case client memory gets destroyed while the associated permit is not yet
        //      released.
        //
        //   b) Data races in case of a client updating the permit request for re-requesting
        //      resources for an existing permit, while other thread is reading it inside TCM.
        tcm_permit_request_t& pr = ph->request;
        pr = req;               // Do shallow copy first

        if (bool(req.cpu_constraints)) {
            __TCM_ASSERT(req.constraints_size > 0, "Missing size for CPU constraints array");
            pd.size = req.constraints_size;

            const unsigned concurrency_size = pd.size * sizeof(concurrency_item_t);
            const unsigned mask_size = pd.size * sizeof(std::remove_pointer_t<mask_t>);
            const unsigned mask_offset = concurrency_offset + concurrency_size;
            const unsigned constraints_offset = mask_offset + mask_size;

            pd.cpu_mask = reinterpret_cast<mask_t>(permit_rep_as_byte_array + mask_offset);
            // cpu mask is a POD, don't need to call constructor for it
            for (uint32_t i = 0; i < pd.size; ++i) {
                pd.cpu_mask[i] = hwloc_bitmap_alloc();
                __TCM_ASSERT(hwloc_bitmap_iszero(pd.cpu_mask[i]), "Not empty mask");
            }

            pr.cpu_constraints = reinterpret_cast<constraints_t>(
                permit_rep_as_byte_array + constraints_offset
            );
            // CPU constraints is a POD, don't need to call constructor for it
            copy_constraints(pr.cpu_constraints, req.cpu_constraints, req.constraints_size);
        }

        pd.concurrency = new(pd.concurrency) concurrency_item_t[pd.size]{0};
        pd.state.store(state, std::memory_order_relaxed);
        pd.flags = req.flags;
        // tcm_epoch_snapshot will be properly set later under mutex during request for resources.
        pd.tcm_epoch_snapshot = 0;
        pd.is_nested.store(false, std::memory_order_relaxed);
        pd.inherited_concurrency_idx.store(0, std::memory_order_relaxed);

        return ph;
    }
}; // class ThreadComposabilityBase

class ThreadComposabilityFairBalance : public ThreadComposabilityManagerBase {
  // Stores the keys in the map of existing permits.
  std::deque<tcm_permit_handle_t> renegotiation_deque;

protected:
    // Called once there is possibility to better utilize resources (i.e. resources are released or
    // became idle).
    void renegotiate_permits(tcm_permit_handle_t initiator) override {
        // Algorithm:
        // - Determine resources availability, including those in IDLE state
        // - Try activating pending permits
        // - Determine other demand for resources (compute desired minus current for active permits)
        // - Saturate the demand starting from the least unhappy (i.e. the one with minimal
        //   'desired - current' value) active permit
        __TCM_PROFILE_THIS_FUNCTION();

        update_callbacks_t callbacks;
        {
            const std::lock_guard<std::mutex> lock(data_mutex);

            // Process of PENDING permits first in order to activate as many permits as possible
            std::vector<tcm_permit_handle_t> pending_permits_copy(
                pending_permits.cbegin(), pending_permits.cend()
            );
            for (auto& ph : pending_permits_copy) {
                const tcm_permit_request_t& pr = ph->request;
                fulfillment_t ff = try_satisfy_request(pr, ph, available_concurrency);
                __TCM_ASSERT(pr.min_sw_threads >= 0, "Min SW Threads must be known");
                int32_t primary_thread = uint32_t(is_nested(ph));
                int32_t adjusted_min_sw_threads = std::max(0, pr.min_sw_threads-primary_thread);
                const bool is_required_fits = ff.num_satisfiable >= uint32_t(adjusted_min_sw_threads) &&
                    ff.pending_constraints_indices.empty();
                if (!is_required_fits) {
                    // Required concurrency cannot be satisfied, skipping the permit
                    continue;   // As there might be pending requests for other part of the platform
                }
                std::vector<permit_change_t> updates = negotiate(ff, pr, ph);
                update_callbacks_t additional_callbacks = apply(*this, updates, /*initiator*/nullptr);

                // Avoid invoking callback for the same permit multiple times
                merge_callback_invocations(callbacks, additional_callbacks);
            }

            if (!has_unused_resources(*this)) {
                goto callback_invoke;
            }

            std::priority_queue<tcm_permit_handle_t, std::vector<tcm_permit_handle_t>,
                                less_unhappy_t> unsatisfied_permits;

            // TODO: Consider maintaining unsatisfied permits container as the following loop might
            // take long time due to high number of the active permits
            for (auto& ph : active_permits) {
                // TODO: Consider also skipping permits that have just moved from pending to active
                // in the previous loop
                if (skip_permit_negotiation(ph, initiator)) {
                    continue;
                }

                const bool is_permit_fully_satisfied = bool( permit_unhappiness(ph) == 0 );
                if (is_permit_fully_satisfied) {
                    continue;    // grant == desired concurrency for this permit, skip it.
                }
                unsatisfied_permits.push(ph);
            }

            while (has_unused_resources(*this) && !unsatisfied_permits.empty()) {
                tcm_permit_handle_t ph = unsatisfied_permits.top(); unsatisfied_permits.pop();

                // Should not consider ACTIVE permits for negotiation since required number of
                // resources has been given already.
                fulfillment_t ff = try_satisfy_request(ph->request, ph, available_concurrency);
                std::vector<permit_change_t> updates = negotiate(ff, ph->request, ph);
                update_callbacks_t additional_callbacks = apply(*this, updates, /*initiator*/nullptr);

                // Avoid invoking callback for the same permit multiple times
                merge_callback_invocations(callbacks, additional_callbacks);
            }
        } // end of mutex section

    callback_invoke:
        invoke_callbacks(callbacks);
    }

    std::vector<permit_change_t> adjust_existing_permit(const tcm_permit_request_t &req,
                                                        tcm_permit_handle_t ph) override
    {
        // The data_mutex lock must be taken
        __TCM_PROFILE_THIS_FUNCTION();

        if (lazy_inactive_permit) {
            available_concurrency += grab_permit_resources(lazy_inactive_permit);
            note_tcm_state_change(*this);
            lazy_inactive_permit = nullptr;
        }

        // Trying to squeeze resources out of the platform, returning permits that share
        // resources needed by that ph
        fulfillment_t ff = try_satisfy_request(req, ph, available_concurrency);

        int32_t primary_thread = uint32_t(is_nested(ph));
        int32_t adjusted_min_sw_threads = std::max(0, req.min_sw_threads-primary_thread);
        if (int32_t(ff.num_satisfiable) < adjusted_min_sw_threads) {
            return {}; // Also works if min_sw_threads == tcm_automatic
        }

        if ( !ff.pending_constraints_indices.empty() ) {
            // Failed to satisfy the minimum concurrency for at least one constraint. The permit
            // should be left in the PENDING state.
            return {};
        }
        __TCM_ASSERT(ff.num_negotiable <= ff.num_satisfiable,
                     "Number of negotiated must be included into total number of found resources.");
        __TCM_ASSERT(adjusted_min_sw_threads <= int32_t(ff.num_satisfiable) &&
                                           int32_t(ff.num_satisfiable) <= req.max_sw_threads,
                     "Found resources should be within the requested limits.");

        std::vector<permit_change_t> updates = negotiate(ff, req, ph);
        return updates;
    }
}; // ThreadComposabilityFairBalance
} // namespace internal

using ThreadComposabilityManager = internal::ThreadComposabilityFairBalance;

class theTCM {
  static ThreadComposabilityManager* tcm_ptr;
  static std::size_t reference_count;
  static std::mutex tcm_mutex;
  static internal::environment tcm_env;
public:
  static bool is_enabled() {
    return tcm_env.tcm_enable == 1;
  }

  static bool tcm_enable_variable_explicitly_set() {
    return tcm_env.tcm_enable != tcm_automatic;
  }

  static bool tcm_in_dev_environment() {
    static bool in_dev_env = std::getenv("TCMROOT") || std::getenv("ONEAPI_ROOT") ||
                             std::getenv("TBBROOT") || std::getenv("CMPLR_ROOT");
    return in_dev_env;
  }

  friend float internal::tcm_oversubscription_factor();

  static void increase_ref_count() {
    std::lock_guard<std::mutex> l(tcm_mutex);
    if (reference_count++)
      return;
    tcm_ptr = new ThreadComposabilityManager{};
  }

  static internal::environment& get_tcm_env() {
    return tcm_env;
  }

  static ThreadComposabilityManager& instance() {
    __TCM_ASSERT(tcm_ptr != nullptr, "Access to uninitialized resource manager.");
    return *tcm_ptr;
  }

  static void decrease_ref_count() {
    ThreadComposabilityManager* rm_instance_to_delete = tcm_ptr;
    {
      std::lock_guard<std::mutex> l(tcm_mutex);
      __TCM_ASSERT(reference_count != 0, "Incorrect reference count.");
      if (--reference_count)
        return;
      tcm_ptr = nullptr;
      delete rm_instance_to_delete;
    }
  }

  static void consider_suggesting_usage() {
    __TCM_ASSERT(!is_enabled(), nullptr);
    static std::atomic<std::size_t> connection_attempts{0};
    constexpr std::size_t max_failed_connection_attempts = 2;
    std::size_t previous_attempts = connection_attempts.fetch_add(1, std::memory_order_relaxed);
    if (previous_attempts == max_failed_connection_attempts - 1) {
      std::fprintf(stderr,
        "Note: Several threading libraries could use Thread Composability Manager.\n"
        "Hint: If CPUs are overutilized, setting the TCM_ENABLE environment variable to 1\n"
        "may improve performance. For more details, search for \"avoid cpu overutilization\"\n"
        "at https://uxlfoundation.github.io/oneTBB/\n"
        "To suppress this message, set TCM_ENABLE to either 0 or 1.\n"
      );
    }
  }
};


ThreadComposabilityManager* theTCM::tcm_ptr{nullptr};
std::size_t theTCM::reference_count{0};
std::mutex theTCM::tcm_mutex{};
internal::environment theTCM::tcm_env{};

float internal::tcm_oversubscription_factor() {
  static const float oversb_factor = theTCM::tcm_env.tcm_oversubscription_factor;
  __TCM_ASSERT(oversb_factor > std::numeric_limits<float>::epsilon(),
                "Incorrect value of oversubscription factor.");
  return oversb_factor;
}

} // namespace tcm

extern "C" {

///////////////////////////////////////////////////////////////////////////////
/// @brief Initialize the connection to Thread Composability Manager
///
/// @details
///     - The client must call this function before calling any other
///       resource management function.
///
/// @returns
///     - ::TCM_RESULT_SUCCESS
///     - ::TCM_RESULT_ERROR_UNKNOWN
tcm_result_t tcmConnect(tcm_callback_t callback, tcm_client_id_t* client_id)
{
  using tcm::theTCM;

  if (!theTCM::is_enabled()) {
    if (!theTCM::tcm_enable_variable_explicitly_set() && theTCM::tcm_in_dev_environment()) {
      theTCM::consider_suggesting_usage();
    }
    return TCM_RESULT_ERROR_UNKNOWN;
  } else if (!client_id) {
    return TCM_RESULT_ERROR_INVALID_ARGUMENT;
  }

  theTCM::increase_ref_count();
  auto& mgr = theTCM::instance();
  *client_id = mgr.register_client(callback);
  return TCM_RESULT_SUCCESS;
}

/// @brief Terminate the connection with Thread Composability Manager
///
/// @details
///     - Must be called whenever the client, which is seen as a set of permits associated
///       with the given client_id, finishes its work with Thread Composability Manager
///       and no other calls, possibly except for tcmConnect are expected to be made
///       from that client.
///
/// @returns
///     - ::TCM_RESULT_SUCCESS
///     - ::TCM_RESULT_ERROR_UNKNOWN
tcm_result_t tcmDisconnect(tcm_client_id_t client_id)
{
  using tcm::theTCM;

  auto& mgr = theTCM::instance();
  mgr.unregister_client(client_id);
  theTCM::decrease_ref_count();

  return TCM_RESULT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Request a new permit
///
/// @details
///     - The client must call this function to request the permit.
///
/// @returns
///     - ::TCM_RESULT_SUCCESS
///     - ::TCM_RESULT_ERROR_UNKNOWN
tcm_result_t tcmRequestPermit(tcm_client_id_t client_id,
                              tcm_permit_request_t request,
                              void* callback_arg,
                              tcm_permit_handle_t *permit_handle,
                              tcm_permit_t* permit)
{
  using tcm::theTCM;

  int32_t sum_min = 0, sum_max = request.max_sw_threads;
  if (request.cpu_constraints) {
    if (request.constraints_size <= 0)
      return TCM_RESULT_ERROR_INVALID_ARGUMENT;

    const bool is_request_sane = tcm::internal::sum_constraints_bounds(sum_min, sum_max, request);
    if (!is_request_sane) {
      return TCM_RESULT_ERROR_INVALID_ARGUMENT;
    }
  } else if (request.constraints_size != 0) {
    return TCM_RESULT_ERROR_INVALID_ARGUMENT;
  }

  if (request.min_sw_threads != tcm_automatic && request.min_sw_threads < sum_min) {
    return TCM_RESULT_ERROR_INVALID_ARGUMENT;
  } else if (request.max_sw_threads != tcm_automatic && sum_max < request.max_sw_threads) {
    return TCM_RESULT_ERROR_INVALID_ARGUMENT;
  } else if (request.max_sw_threads != tcm_automatic &&
             request.max_sw_threads < request.min_sw_threads) {
    return TCM_RESULT_ERROR_INVALID_ARGUMENT;
  } else if (!permit_handle) {
    return TCM_RESULT_ERROR_INVALID_ARGUMENT;
  }

  auto& mgr = theTCM::instance();
  if (request.min_sw_threads > static_cast<int32_t>(mgr.platform_resources())) {
      return TCM_RESULT_ERROR_INVALID_ARGUMENT;
  }
  return mgr.request_permit(client_id, request, callback_arg, permit_handle, permit, sum_min);
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Writes the current permit data into passed argument.
///
/// @details
///     - The client calls this function whenever it wants to read the permit.
///       In paricular, after tcmIdlePermit and tcmActivatePermit calls, and
///       during invocation of the client's callback.
///
/// @returns
///     - ::TCM_RESULT_SUCCESS
///     - ::TCM_RESULT_ERROR_UNKNOWN
tcm_result_t tcmGetPermitData(tcm_permit_handle_t permit_handle, tcm_permit_t* permit) {
  using tcm::theTCM;

  if (!permit_handle || !permit)
    return TCM_RESULT_ERROR_UNKNOWN;

  auto& mgr = theTCM::instance();
  return mgr.get_permit(permit_handle, permit);
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Idles a permit
///
/// @details
///     - The client must call this function to mark the permit as idle.
///
/// @returns
///     - ::TCM_RESULT_SUCCESS
///     - ::TCM_RESULT_ERROR_UNKNOWN
tcm_result_t tcmIdlePermit(tcm_permit_handle_t p) {
  using tcm::theTCM;

  if (p) {
    return theTCM::instance().idle_permit(p);
  }
  return TCM_RESULT_ERROR_UNKNOWN;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Activates a permit
///
/// @details
///     - The client must call this function to activate the permit.
///
/// @returns
///     - ::TCM_RESULT_SUCCESS
///     - ::TCM_RESULT_ERROR_UNKNOWN
tcm_result_t tcmActivatePermit(tcm_permit_handle_t p) {
  using tcm::theTCM;

  if (p) {
    return theTCM::instance().activate_permit(p);
  }
  return TCM_RESULT_ERROR_UNKNOWN;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Deactivates a permit
///
/// @details
///     - The client must call this function to deactivate the permit.
///
/// @returns
///     - ::TCM_RESULT_SUCCESS
///     - ::TCM_RESULT_ERROR_UNKNOWN
tcm_result_t tcmDeactivatePermit(tcm_permit_handle_t p) {
  using tcm::theTCM;

  if (p) {
    return theTCM::instance().deactivate_permit(p);
  }
  return TCM_RESULT_ERROR_UNKNOWN;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Releases a permit
///
/// @details
///     - The client must call this function to release the permit.
///
/// @returns
///     - ::TCM_RESULT_SUCCESS
///     - ::TCM_RESULT_ERROR_UNKNOWN
tcm_result_t tcmReleasePermit(tcm_permit_handle_t handle) {
  using tcm::theTCM;

  if (handle) {
    return theTCM::instance().release_permit(handle);
  }

  return TCM_RESULT_ERROR_INVALID_ARGUMENT;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Registers a thread
///
/// @details
///     - The client must call this function when the thread allocated as part of the permit starts the work.
///
/// @returns
///     - ::TCM_RESULT_SUCCESS
///     - ::TCM_RESULT_ERROR_UNKNOWN
tcm_result_t tcmRegisterThread(tcm_permit_handle_t p) {
  using tcm::theTCM;
  if (p) {
    return theTCM::instance().register_thread(p);
  }
  return TCM_RESULT_ERROR_UNKNOWN;
}


///////////////////////////////////////////////////////////////////////////////
/// @brief Unregisters a thread
///
/// @details
///     - The client must call this function when the thread allocated as part of the permit ends the work.
///
/// @returns
///     - ::TCM_RESULT_SUCCESS
///     - ::TCM_RESULT_ERROR_UNKNOWN
tcm_result_t tcmUnregisterThread() {
  using tcm::theTCM;

  return theTCM::instance().unregister_thread();
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Provides TCM meta information for clients into provided character buffer
///
/// @returns
///     - ::TCM_RESULT_SUCCESS
///     - ::TCM_RESULT_ERROR_INVALID_ARGUMENT
///     - ::TCM_RESULT_ERROR_UNKNOWN
tcm_result_t tcmGetVersionInfo(char* buffer, uint32_t buffer_size) {
  if (!buffer) {
    return TCM_RESULT_ERROR_INVALID_ARGUMENT;
  }

  int result = tcm::internal::environment::get_version_string(tcm::theTCM::get_tcm_env(), buffer, buffer_size);
  if (result < 0) {
    return TCM_RESULT_ERROR_UNKNOWN;
  }

  return TCM_RESULT_SUCCESS;
}

}
