/*
    Copyright (c) 2021-2023 Intel Corporation
*/

#include "tcm/detail/_tcm_assert.h"
#include "tcm/detail/hwloc_utils.h"
#include "tcm/detail/_environment.h"
#include "tcm.h"

// MSVC Warning: unreferenced formal parameter
__TCM_SUPPRESS_WARNING_WITH_PUSH(4100)
#include "hwloc.h"
__TCM_SUPPRESS_WARNING_POP

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <vector>
#include <queue>

#if __TCM_ENABLE_TRACER
#include <iostream>
#endif

struct tcm_permit_data_t {
  tcm_client_id_t client_id;
  std::atomic<uint32_t>* concurrency;
  tcm_cpu_mask_t* cpu_mask;
  uint32_t size;
  std::atomic<tcm_permit_state_t> state;
  tcm_permit_flags_t flags;
};

extern "C" {

typedef uint64_t tcm_permit_epoch_t;

struct tcm_permit_rep_t {
  std::atomic<tcm_permit_epoch_t> epoch;
  tcm_permit_request_t request; // Holds latest corresponding request
  tcm_permit_data_t data;
};

} // extern "C"

namespace tcm {
namespace internal {

#if __TCM_ENABLE_TRACER
struct tracer {
  const std::string s_;
  tracer(const std::string &s) : s_(s) {
    std::cout << "Entering " << s_ << std::endl;
  }

  void log(const std::string &msg) {
    std::cout << msg << std::endl;
  }

  ~tracer() {
    std::cout << "Leaving " << s_ << std::endl;
  }
};
#else
struct tracer {
  tracer(const std::string &) {}
  void log(const std::string&) {}
  ~tracer() {}
};
#endif

bool operator==(const tcm_permit_flags_t& lhs, const tcm_permit_flags_t& rhs) {
  return
    lhs.stale == rhs.stale &&
    lhs.rigid_concurrency == rhs.rigid_concurrency &&
    lhs.exclusive == rhs.exclusive;
}

bool operator==(const tcm_permit_request_t& lhs, const tcm_permit_request_t& rhs) {
  return
    lhs.min_sw_threads == rhs.min_sw_threads &&
    lhs.max_sw_threads == rhs.max_sw_threads &&
    lhs.cpu_constraints == rhs.cpu_constraints &&
    lhs.flags == rhs.flags;
}


/**
 *******************************************************************************
 * Permit state helpers
 *******************************************************************************
 */
bool is_void(const tcm_permit_state_t& state) { return TCM_PERMIT_STATE_VOID == state; }
bool is_active(const tcm_permit_state_t& state) { return TCM_PERMIT_STATE_ACTIVE == state; }
bool is_inactive(const tcm_permit_state_t& state) { return TCM_PERMIT_STATE_INACTIVE == state; }
bool is_idle(const tcm_permit_state_t& state) { return TCM_PERMIT_STATE_IDLE == state; }
bool is_pending(const tcm_permit_state_t& state) { return TCM_PERMIT_STATE_PENDING == state; }

bool is_owning_resources(const tcm_permit_state_t& state) {
    return is_active(state) || is_idle(state);
}

bool is_negotiable(const tcm_permit_state_t& state, const tcm_permit_flags_t& flags) {
    if (is_owning_resources(state) && flags.rigid_concurrency)
        return false;
    return true;
}

bool is_activating_from_inactive(const tcm_permit_state_t& curr_state,
                                 const tcm_permit_state_t& new_state)
{
    return is_inactive(curr_state) && is_active(new_state);
}

bool is_deactivating(const tcm_permit_state_t& curr_state, const tcm_permit_state_t& new_state) {
    return is_owning_resources(curr_state) && is_inactive(new_state);
}

/**
 *******************************************************************************
 * End of permit state helpers
 *******************************************************************************
 */


// Computes the currently used amount of resources by specified permit data
uint32_t get_permit_grant(const tcm_permit_data_t& pd) {
    uint32_t permit_grant = 0;
    for (unsigned i = 0; i < pd.size; ++i)
        permit_grant += pd.concurrency[i].load(std::memory_order_relaxed);
    return permit_grant;
}

// Computes the currently used amount of resources by specified permit handle
uint32_t get_permit_grant(tcm_permit_handle_t ph) { return get_permit_grant(ph->data); }


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

float tcm_oversubscription_factor();

uint32_t hardware_concurrency() { return (uint32_t)std::thread::hardware_concurrency(); }

//! Returns available platform resources, taking into account possible degree
//! of oversubscription.
uint32_t platform_resources(unsigned int process_concurrency) {
  static uint32_t concurrency = uint32_t(tcm_oversubscription_factor() * process_concurrency);
  // TODO: Consider returning not less than one resource
  return concurrency;
}

struct mask_deleter {
    void operator()(tcm_cpu_mask_t* mask_ptr) const { hwloc_bitmap_free(*mask_ptr); }
};

int get_mask_concurrency(const tcm_cpu_mask_t& mask) {
    __TCM_ASSERT(mask, "Existing mask is expected.");
    int mc = hwloc_bitmap_weight(mask);
    return mc;
}

int get_oversubscribed_mask_concurrency(const tcm_cpu_mask_t &mask,
                                        float oversubscription_factor = tcm_oversubscription_factor())
{
    return int(get_mask_concurrency(mask) * oversubscription_factor);
}

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

int32_t infer_constraint_max_concurrency(int32_t max_concurrency_value,
                                         uint32_t fallback_concurrency, const tcm_cpu_mask_t& mask)
{
    if (tcm_automatic != max_concurrency_value) {
      __TCM_ASSERT(max_concurrency_value > 0,
                   "Found incorrect value for constraint.max_concurrency.");
      return max_concurrency_value;
    }

    if (mask) {
      max_concurrency_value = get_mask_concurrency(mask);
      if (max_concurrency_value < 0) {
        // Fail to get the concurrency of the provided mask or the mask is inifinitely set, use
        // fallback value.
        max_concurrency_value = fallback_concurrency;
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
    tcm_permit_handle_t ph = nullptr;       // permit handle to update
    tcm_permit_state_t new_state = 0;       // state to transition into
    std::vector<uint32_t> new_concurrencies; // concurrencies to set
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

void invoke_callbacks(const update_callbacks_t& callbacks) {
    for (const auto& n : callbacks) {
        const tcm_callback_t& callback = n.first;
        const callback_args_t& args = n.second;

        __TCM_ASSERT(callback, "Incorrect invariant: missing callback is in the invocation list.");
        auto result = callback(args.ph, args.callback_arg, args.reason);

        __TCM_ASSERT_EX(result == TCM_RESULT_SUCCESS, "Unsuccessful callback invocation.");
    }
}


// Permit changing helpers
uint32_t release_resources_moving_to_new_state(tcm_permit_handle_t ph,
                                               tcm_permit_state_t new_state)
{
    __TCM_ASSERT(new_state == TCM_PERMIT_STATE_VOID ||
                 new_state == TCM_PERMIT_STATE_PENDING ||
                 new_state == TCM_PERMIT_STATE_INACTIVE, "Inconsistent request.");

    auto& pd = ph->data;
    uint32_t current_grant = 0;

    prepare_permit_modification(ph);
    {
        for (uint32_t i = 0; i < pd.size; ++i) {
            current_grant += pd.concurrency[i].exchange(0, std::memory_order_relaxed);
        }
        pd.state = new_state;
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
      process_concurrency = hardware_concurrency();
    }
    available_concurrency = platform_resources(process_concurrency);
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

  //! The CPU mask of the process
  tcm_cpu_mask_t process_mask = nullptr;

  //! The existing permits
  std::vector<tcm_permit_handle_t> permits{};

  //! The map of callbacks per each client. Callbacks are used during
  //! renegotiation of permits.
  std::unordered_map<tcm_client_id_t, tcm_callback_t> client_to_callback_map{};

  //! The map of callback arguments associated with permits. Used during
  //! callback invocation.
  std::unordered_map<tcm_permit_handle_t, void*> permit_to_callback_arg_map{};

  //! The multimap of permits associated with the given client.
  std::unordered_multimap<tcm_client_id_t, tcm_permit_handle_t> client_to_permit_mmap{};

  // TODO: Consider alternative data structure for maintaining
  // registering/unregistering of threads by answering:
  // - Do we need to identify threads that register or unregister themselves?
  //   - Will atomic counting suffice?
  // - To avoid search in a container, move atomic counter into permit?
  //   - Can we cast array of chars to an atomic memory?

  //! The mutex for the map of registering threads.
  std::mutex threads_map_mutex{};

  //! The map of threads that register to be a part of a permit.
  std::unordered_map<std::thread::id, tcm_permit_handle_t> thread_to_permit_map{};
};


/**
 * Applies changes to the permits according to the given updates. Returns the list of callbacks and
 * their arguments to be invoked.
 *
 * The callback is added into the returned list only if there was an actual change to the permit.
 */
update_callbacks_t apply(ThreadComposabilityManagerData& data, const tcm_permit_handle_t initiator,
                         const std::vector<permit_change_t>& updates)
{
    update_callbacks_t update_callbacks;

    int32_t concurrency_delta = 0;
    for (const auto& change : updates) {
        callback_args_t args{change.ph, data.permit_to_callback_arg_map[change.ph], {}};

        tcm_permit_data_t& original_data = change.ph->data;
        prepare_permit_modification(change.ph);
        if (original_data.state.load(std::memory_order_relaxed) != change.new_state) {
            original_data.state.store(change.new_state, std::memory_order_relaxed);
            args.reason.new_state = true;
        }

        std::atomic<uint32_t>* original_concurrencies = original_data.concurrency;
        for (std::size_t i = 0; i < change.new_concurrencies.size(); ++i) {
            uint32_t old_concurrency = original_concurrencies[i].load(std::memory_order_relaxed);
            const uint32_t new_concurrency = change.new_concurrencies[i];
            if (old_concurrency != new_concurrency) {
                original_concurrencies[i].store(new_concurrency, std::memory_order_relaxed);
                args.reason.new_concurrency = true;
                concurrency_delta += old_concurrency - new_concurrency;
            }
        }
        commit_permit_modification(change.ph);

        if (change.ph == initiator)    // Skip callback invocation for the change requestor
            continue;

        if (args.reason.new_concurrency || args.reason.new_state) { // if there was a change
            tcm_callback_t callback = data.client_to_callback_map[original_data.client_id];
            if (callback) {
                update_callbacks.insert( {callback, args} );
            }
        }
    }

    data.available_concurrency += concurrency_delta;

    return update_callbacks;
}


// Describes a stakeholder of specific resources subset with cached negotiable value,
// which equals to permitted concurrency value on the subconstraint minus minimum
// requested concurrency on that subconstraint.
struct stakeholder_t {
    tcm_permit_handle_t m_ph = nullptr;
    int32_t m_constraint_index = tcm_automatic; // tcm_automatic if the whole permit is
                                                 // considered rather than its particular
                                                 // subconstraint
    uint32_t m_negotiable = 0;
};

struct greater_negotiable_stakeholder_t {
    bool operator()(const stakeholder_t& lhs, const stakeholder_t& rhs) const {
        return lhs.m_negotiable > rhs.m_negotiable;
    }
};

typedef std::priority_queue<stakeholder_t, std::vector<stakeholder_t>,
                            greater_negotiable_stakeholder_t> renegotiable_resources_queue_t;

// The container of negotiable stakeholders. Filled in while trying to satisfy a request.
struct negotiable_snapshot_t {
    static const int32_t among_all_constraints = tcm_automatic;

    bool is_negotiable() const { return m_negotiable == 0; }
    uint32_t num_negotiable() const { return m_negotiable; }

    uint32_t num_immediately_available() const { return m_immediately_available; }
    void set_immediately_available(uint32_t value) { m_immediately_available = value; }

    uint32_t num_available() const { return m_immediately_available + m_negotiable; }

    void set_adjusted_concurrencies(uint32_t min_concurrency, uint32_t max_concurrency) {
        __TCM_ASSERT(min_concurrency <= max_concurrency,
                     "Minimum concurrency must be less or equal to maximum concurrency.");
        m_adjusted_min_concurrency = min_concurrency;
        m_adjusted_max_concurrency = max_concurrency;
    }

    uint32_t adjusted_min_concurrency() const { return m_adjusted_min_concurrency; }
    uint32_t adjusted_max_concurrency() const { return m_adjusted_max_concurrency; }

    renegotiable_resources_queue_t get_contributing_permits() const { return m_permits; }

    void add(const tcm_permit_handle_t& ph, uint32_t negotiable, int32_t constraint_index) {
      /* negotiable == grant - minimum sw threads, if constraint_index equals to
         among_all_constraints. Otherwise, negotiable == grant - minimum concurrency
         within specific subconstraint, determined by the constraint_index in the array of
         permit's constraints. */
        add( stakeholder_t{ph, constraint_index, negotiable} );
    }

    void add(const stakeholder_t& stakeholder) {
      /* negotiable == grant - minimum sw threads, if constraint_index equals to
         among_all_constraints. Otherwise, negotiable == grant - minimum concurrency
         within specific subconstraint, determined by the constraint_index in the array of
         permit's constraints. */

        __TCM_ASSERT(is_owning_resources(stakeholder.m_ph->data.state.load(std::memory_order_relaxed)),
                     "Only permits with owning resources can be added into negotiable snapshot." );

        const auto search_item = std::make_pair(stakeholder.m_ph, stakeholder.m_constraint_index);
        if (m_included_permits.find(search_item) != m_included_permits.end())
            // There is such an item in the queue already. Do not duplicate it.
            return;

        m_negotiable += stakeholder.m_negotiable;
        m_permits.push(stakeholder);
    }

private:
    // The amount of resources that can grant without negotiations.
    uint32_t m_immediately_available = 0;

    // The amount of resources that can be negotiated, that is the sum of (grant
    // concurrency minus minimum required) expressions (possibly, per constraint) across
    // all permits in the snapshot.
    uint32_t m_negotiable = 0;

    // Adjusted concurrencies for specific constraint (or full permit if no constraints)
    uint32_t m_adjusted_min_concurrency = 0, m_adjusted_max_concurrency = 0;

    // Permits containing negotiable resources ordered from the most to the least
    // resources amount available for renegotiation
    renegotiable_resources_queue_t m_permits =
        renegotiable_resources_queue_t(greater_negotiable_stakeholder_t{});

    // The set of permits that have been added to the queue
    std::set< std::pair<tcm_permit_handle_t, /*constraint_index*/int32_t>>
    m_included_permits;
};


class ThreadComposabilityManagerBase : public ThreadComposabilityManagerData {
public:
  virtual ~ThreadComposabilityManagerBase() {}

  tcm_client_id_t register_client(tcm_callback_t r) {
      tracer t("ThreadComposabilityBase::register_client");
      const std::lock_guard<std::mutex> l(data_mutex);
      tcm_client_id_t clid = client_id++;
      client_to_callback_map[clid] = r;
      return clid;
  }

  void unregister_client(tcm_client_id_t clid) {
      tracer t("ThreadComposabilityBase::unregister_client");
      const std::lock_guard<std::mutex> l(data_mutex);
      __TCM_ASSERT(client_to_permit_mmap.count(clid) == 0, "Deactivating the client with associated permits.");
      __TCM_ASSERT(client_to_callback_map.count(clid) == 1, "The client_id was not registered.");
      client_to_callback_map.erase(clid);
  }

    /**
     * Allocates and copies constraints for the specified permit request.
     *
     * Assumes actual pointer to constraints is written to the passed constraints argument.
     */
  void allocate_constraints_by_copy(tcm_permit_request_t& req) {
      __TCM_ASSERT(req.cpu_constraints, "Nothing to copy from.");
      tcm_cpu_constraints_t* client_constraints = req.cpu_constraints;
      req.cpu_constraints = new tcm_cpu_constraints_t[req.constraints_size];
      for (uint32_t i = 0; i < req.constraints_size; ++i) {
          req.cpu_constraints[i] = client_constraints[i];
          if (client_constraints[i].mask)
              req.cpu_constraints[i].mask = hwloc_bitmap_dup(client_constraints[i].mask);
      }
  }

  void deallocate_constraints(tcm_permit_request_t& req) {
      __TCM_ASSERT(req.cpu_constraints, "Nothing to deallocate.");
      for (uint32_t i = 0; i < req.constraints_size; ++i)
          hwloc_bitmap_free(req.cpu_constraints[i].mask);
      delete [] req.cpu_constraints;
  }

  void copy_request_without_masks(tcm_permit_request_t& to, const tcm_permit_request_t& from) {
    tcm_cpu_constraints_t* internal_cpu_constraints = to.cpu_constraints;
    to = from;
    to.cpu_constraints = internal_cpu_constraints; // Restore the pointer to TCM's memory

    for (uint32_t i = 0; i < to.constraints_size; ++i) {
        tcm_cpu_mask_t internal_mask = to.cpu_constraints[i].mask;

        __TCM_ASSERT(
          internal_mask == nullptr ||
          0 == hwloc_bitmap_compare(internal_mask, from.cpu_constraints[i].mask),
          "Changing of the mask when re-requesting resources for existing permit is not supported."
        );

        to.cpu_constraints[i] = from.cpu_constraints[i];
        to.cpu_constraints[i].mask = internal_mask;
    }
  }

  void deduce_request_arguments(tcm_permit_request_t& request) {
    if (tcm_automatic == request.min_sw_threads) {
      request.min_sw_threads = 0;
    }

    if (tcm_automatic == request.max_sw_threads) {
      request.max_sw_threads = process_concurrency;
    }

    if (!request.cpu_constraints) {
      return;
    }

    for (uint32_t i = 0; i < request.constraints_size; ++i) {
      tcm_cpu_constraints_t& c = request.cpu_constraints[i];

      c.min_concurrency = infer_constraint_min_concurrency(c.min_concurrency);
      c.max_concurrency = infer_constraint_max_concurrency(c.max_concurrency, process_concurrency,
                                                           c.mask);
    }
  }

  bool request_permit(tcm_client_id_t clid, tcm_permit_request_t& req,
                      void* callback_arg, tcm_permit_handle_t* permit_handle,
                      tcm_permit_t* permit)
  {
    tracer t("ThreadComposabilityBase::request_permit");

    // Check the ability to satisfy minimum requested concurrency
    if (req.min_sw_threads > int32_t(initially_available_concurrency)) {
      if (permit)
        permit->state = TCM_PERMIT_STATE_VOID;
      return false;
    }

    bool additional_concurrency_available = false;

    tcm_permit_handle_t ph = *permit_handle;
    const bool is_requesting_new_permit = bool(!ph);
    if (is_requesting_new_permit) {
      ph = make_new_permit(clid, req);
      // New permits should have PENDING state until its required/minimum parameters are satisfied.
      __TCM_ASSERT(
          TCM_PERMIT_STATE_PENDING == ph->data.state.load(std::memory_order_relaxed),
          "Non-pending state for new permits contributes to their premature negotiations."
      );
      *permit_handle = ph;
    }

    update_callbacks_t callbacks;
    {
      const std::lock_guard<std::mutex> l(data_mutex);

      uint32_t initially_available = available_concurrency;

      // TODO: Consider adding the permit to containers after the concurrency level
      // calculation to avoid early renegotiation
      if (is_requesting_new_permit) {
          permits.push_back(ph);
          client_to_permit_mmap.emplace(ph->data.client_id, ph);
      } else {
          __TCM_ASSERT(is_valid(ph), "Permit request structure must exist.");
          copy_request_without_masks(ph->request, req);

          // Request is being updated for existing permit. To avoid in-the-middle
          // negotiations for that permit change its state to PENDING until its new
          // required/minimum parameters are further satisfied.
          const uint32_t released = move_to_pending(ph);
          available_concurrency += released;
      }

      deduce_request_arguments(ph->request);

      permit_to_callback_arg_map[ph] = callback_arg;

      std::vector<permit_change_t> updates = adjust_existing_permit(ph->request, ph);

      callbacks = apply(*this, /*initiator*/ph, updates);

      // client might re-request for less number of resources
      additional_concurrency_available = available_concurrency > initially_available;
    }

    invoke_callbacks(callbacks); // Invocation of the callbacks is happenning outside of the lock

    if (additional_concurrency_available)
        renegotiate_permits(/*initiator*/ph);

    if (permit) {
      bool reading_succeeded = false;
      while (!reading_succeeded) {
        reading_succeeded = copy_permit(ph, permit);
      }
    }

    return true;
  }

  tcm_result_t get_permit(tcm_permit_handle_t ph, tcm_permit_t* permit) {
    tracer t("ThreadComposabilityBase::get_permit");
    __TCM_ASSERT(ph && permit, nullptr);

    // NOTE: expected that ph is valid - but direct call is_valid() can lead to races

    permit->flags.stale = false;
    if (!copy_permit(ph, permit)) {
      permit->flags.stale = true;
    }

    return TCM_RESULT_SUCCESS;
  }

  tcm_result_t idle_permit(tcm_permit_handle_t ph) {
    tracer t("ThreadComposabilityBase::idle_permit");
    __TCM_ASSERT(ph, nullptr);

    tcm_permit_data_t& pd = ph->data;

    tcm_permit_state_t curr_state;
    {
      const std::lock_guard<std::mutex> l(data_mutex);
      curr_state = pd.state.load(std::memory_order_relaxed);
    }

    if (!is_active(curr_state))
      return TCM_RESULT_ERROR_UNKNOWN;

    return update_permit(ph, curr_state, /*new_state*/TCM_PERMIT_STATE_IDLE);
  }

  tcm_result_t activate_permit(tcm_permit_handle_t ph) {
    tracer t("ThreadComposabilityBase::activate_permit");
    __TCM_ASSERT(ph, nullptr);

    tcm_permit_data_t& pd = ph->data;
    tcm_permit_state_t curr_state;
    {
      const std::lock_guard<std::mutex> l(data_mutex);
      curr_state = pd.state.load(std::memory_order_relaxed);
    }

    if (!is_inactive(curr_state) && !is_idle(curr_state))
      return TCM_RESULT_ERROR_UNKNOWN;

    return update_permit(ph, curr_state, /*new_state*/TCM_PERMIT_STATE_ACTIVE);
  }

  tcm_result_t deactivate_permit(tcm_permit_handle_t ph) {
    tracer t("ThreadComposabilityBase::activate_permit");
    __TCM_ASSERT(ph, nullptr);

    tcm_permit_data_t& pd = ph->data;
    tcm_permit_state_t curr_state;
    {
      const std::lock_guard<std::mutex> l(data_mutex);
      curr_state = pd.state.load(std::memory_order_relaxed);
    }

    _tcm_result_t status = TCM_RESULT_ERROR_UNKNOWN;
    if (is_owning_resources(curr_state) || is_pending(curr_state)) {
      status = update_permit(ph, curr_state, /*new_state*/TCM_PERMIT_STATE_INACTIVE);
    }

    return status;
  }

  tcm_result_t release_permit(tcm_permit_handle_t ph) {
    tracer t("ThreadComposabilityBase::release_permit");
    __TCM_ASSERT(ph, nullptr);

    tcm_permit_data_t& pd = ph->data;
    tcm_permit_state_t curr_state;
    {
      const std::lock_guard<std::mutex> l(data_mutex);
      curr_state = pd.state.load(std::memory_order_relaxed);
    }

    return update_permit(ph, curr_state, /*new_state*/TCM_PERMIT_STATE_VOID);
  }

  tcm_result_t register_thread(tcm_permit_handle_t ph) {
    tracer t("ThreadComposabilityBase::register_thread");
    __TCM_ASSERT(ph, nullptr);

    const std::lock_guard<std::mutex> l(threads_map_mutex);
    thread_to_permit_map[std::this_thread::get_id()] = ph;
    return TCM_RESULT_SUCCESS;
  }

  tcm_result_t unregister_thread() {
    tracer t("ThreadComposabilityBase::unregister_thread");
    const std::lock_guard<std::mutex> l(threads_map_mutex);
    thread_to_permit_map[std::this_thread::get_id()] = nullptr;
    return TCM_RESULT_SUCCESS;
  }

protected:
  tcm_permit_epoch_t prepare_permit_copying(tcm_permit_handle_t ph) const {
    return ph->epoch.load(std::memory_order_acquire);
  }

  bool has_copying_succeeded(tcm_permit_handle_t permit_handle, tcm_permit_epoch_t c) const {
    return c == permit_handle->epoch.load(std::memory_order_relaxed);
  }

  bool is_safe_to_copy(const tcm_permit_epoch_t& e) const { return e % 2 == 0; }

  bool copy_permit(tcm_permit_handle_t ph, tcm_permit_t* permit) const {
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

    permit->size = pd.size;
    permit->state = pd.state.load(std::memory_order_relaxed);
    permit->flags = pd.flags;

    return has_copying_succeeded(ph, e);
  }

  // Helper to determine whether the permit is not released yet. Must be
  // called under data_mutex.
  bool is_valid(tcm_permit_handle_t ph) const {
      return permits.cend() != std::find(permits.cbegin(), permits.cend(), ph);
  }

  bool skip_permit_negotiation(tcm_permit_handle_t ph, tcm_permit_handle_t initiator) const
  {
    if (ph == initiator)    // renegotiate for one that asked
      return false;

    const tcm_permit_data_t& pd = ph->data;
    const tcm_permit_state_t state = pd.state.load(std::memory_order_relaxed);

    if (!is_negotiable(state, pd.flags)) {
      return true;
    }

    if (is_owning_resources(state))
      return false;

    if (is_pending(state))
      return false;

    return true;
  }

  bool skip_callback_invocation(tcm_permit_handle_t ph, tcm_permit_handle_t initiator,
                                uint32_t previous_grant) const
  {
    if (ph == initiator)        // skip callback invocation for one that asked ...
      return true;

    // ... or if resources grant is not changed
    const uint32_t new_grant = get_permit_grant(ph);
    return previous_grant == new_grant;
  }

    // TODO: rename "try_satisfy" method into something like "calculate mask occupancy"

    // Tries to satisfy requested concurrency on the specific mask
    negotiable_snapshot_t try_satisfy(tcm_permit_handle_t ph,
                                      const tcm_cpu_constraints_t& constraint,
                                      const uint32_t current_concurrency, tcm_cpu_mask_t mask)
    {
        const uint32_t constraint_min = constraint.min_concurrency;
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
        std::unique_ptr<tcm_cpu_mask_t, mask_deleter> unique_result_mask(&per_constraint_union_mask);
        std::unique_ptr<tcm_cpu_mask_t, mask_deleter> unique_common_mask(&common_mask);

        hwloc_bitmap_or(common_mask, common_mask, mask);
        uint32_t common_concurrency = 0;

        // min_required is the maximum among the amount of unavailable resources needed
        // to satisfy the required concurrency (i.e. constraint.min_concurrency).
        // max_desired is the maximum among the amount of resources needed to satisfy the
        // desired concurrency (i.e. constraint.max_concurrency). It is set only when
        // min_required is unset (i.e. required can be satisfied).
        uint32_t min_required = 0, max_desired = 0; // TODO: these are calculated but not
                                                    // used.

        __TCM_ASSERT(constraint_max > 0, "Cannot satisfy indefinite constraint.");
        int available_min = constraint_max;  // holds the number of resources available immediately,
                                             // not requiring any negotiations

        // The following is not an exhaustive search, but a reasonable trade-off between
        // correct resource tracking and algorithm complexity.
        // TODO: Consider implementing other approaches if needed.

        // Going through masks of existing permits one by one, compute mask subscription
        // and try squeezing more resources out of it.

        // Masks that do not intersect when separately applied
        std::queue<stakeholder_t> separate_masks;

        for (tcm_permit_handle_t& ph_i : permits) {
            tcm_permit_request_t& req = ph_i->request;
            tcm_permit_data_t& pd_i = ph_i->data;

            if ( !pd_i.cpu_mask )     // Subscription is tracked separately on the platform level
                continue;

            tcm_permit_state_t ph_i_state = pd_i.state.load(std::memory_order_relaxed);

            // TODO: Looping over permits that own resources and can be negotiated is
            // frequent. Consider maintaining separate structure to avoid filtering them
            // out each time.
            if ( !is_owning_resources(ph_i_state) )
                // Skipping permits that do not own any resources. This also includes the permit for
                // which the resources are being searched.
                continue;

            const bool can_negotiate = is_negotiable(ph_i_state, pd_i.flags);

            __TCM_ASSERT_EX(ph_i != ph, "A being satisfied permit request is considered "
                            "as the one whose resources can be negotiated.");

            for (unsigned constr_idx = 0; constr_idx < pd_i.size; ++constr_idx) {
                __TCM_ASSERT(pd_i.cpu_mask[constr_idx], "Mask must be present for each subconstraint.");

                const uint32_t granted = pd_i.concurrency[constr_idx].load(std::memory_order_relaxed);
                __TCM_ASSERT(int32_t(granted) >= req.cpu_constraints[constr_idx].min_concurrency,
                             "An invalid grant was found.");
                const uint32_t negotiable =
                    can_negotiate ? (granted - req.cpu_constraints[constr_idx].min_concurrency) : 0;


                stakeholder_t stakeholder{ph_i, int32_t(constr_idx), negotiable};
                bool add = false; // Determines whether the stakeholder actually contributes to the
                                  // subscription of the interested part of the platform, and hence
                                  // should be added to the stakeholders list.

                // Try fitting for an individual mask
                if (hwloc_bitmap_intersects(mask, pd_i.cpu_mask[constr_idx])) {
                    hwloc_bitmap_or(per_constraint_union_mask, mask, pd_i.cpu_mask[constr_idx]);
                    const int mc = get_oversubscribed_mask_concurrency(per_constraint_union_mask);
                    __TCM_ASSERT(uint32_t(mc) >= granted, "Incorrectly granted permit is detected.");
                    const int available = mc - granted;
                    available_min = std::min(available_min, available);
                    const auto fitting_result =
                        try_fit_concurrency(constraint_min, constraint_max, available);
                    if (!fitting_result.is_required_satisfied) {
                        min_required = std::max(min_required, fitting_result.needed_concurrency);
                    } else if (min_required == 0) { // not oversubscribed yet
                        max_desired = std::max(max_desired, fitting_result.needed_concurrency);
                    }
                    add = true;
                }

                // Try fitting into compound masks
                if (hwloc_bitmap_intersects(common_mask, pd_i.cpu_mask[constr_idx])) {
                    // TODO: extract common with the above part
                    hwloc_bitmap_or(common_mask, common_mask, pd_i.cpu_mask[constr_idx]);
                    const int mc = get_oversubscribed_mask_concurrency(common_mask);
                    common_concurrency += granted;
                    __TCM_ASSERT(uint32_t(mc) >= common_concurrency, "Incorrectly granted permit is detected.");
                    const int available = mc - common_concurrency;
                    available_min = std::min(available_min, available);
                    const auto fitting_result =
                        try_fit_concurrency(constraint_min, constraint_max, available);
                    if (!fitting_result.is_required_satisfied) {
                        min_required = std::max(min_required, fitting_result.needed_concurrency);
                    } else if (min_required == 0) { // not oversubscribed yet
                        max_desired = std::max(max_desired, fitting_result.needed_concurrency);
                    }
                    add = true;
                } else {
                    separate_masks.push(stakeholder);
                }

                if (add)
                    stakeholders.add(stakeholder);
            } // for each mask in a permit
        } // for each existing permit

        // try applying the masks that did not intersect previously, but might
        // started intersecting after the loop above completes
        std::size_t num_processed_masks = separate_masks.size();
        bool has_union_applied = false;
        while (!separate_masks.empty()) {
            auto& stakeholder = separate_masks.front();
            const tcm_permit_data_t &pd_i = stakeholder.m_ph->data;
            auto m = pd_i.cpu_mask[stakeholder.m_constraint_index];
            if (hwloc_bitmap_intersects(common_mask, m)) {
                // TODO: extract common with the above part
                hwloc_bitmap_or(common_mask, common_mask, m);
                const int mc = get_oversubscribed_mask_concurrency(common_mask);
                const auto c =
                    pd_i.concurrency[stakeholder.m_constraint_index].load(std::memory_order_relaxed);
                common_concurrency += c;
                __TCM_ASSERT(uint32_t(mc) >= common_concurrency, "Incorrectly granted permit is detected.");
                const int available = mc - common_concurrency;
                available_min = std::min(available_min, available);
                auto fitting_result = try_fit_concurrency(constraint_min, constraint_max, available);
                if (!fitting_result.is_required_satisfied)
                    min_required = std::max(min_required, fitting_result.needed_concurrency);
                else if (min_required == 0) { // not oversubscribed yet
                    max_desired = std::max(max_desired, fitting_result.needed_concurrency);
                }
                stakeholders.add(stakeholder);
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

        if (constraint.numa_id == tcm_any) {
            bool is_desired_satisfied = false;
            tcm_cpu_mask_t result_mask = hwloc_bitmap_alloc();
            std::unique_ptr<tcm_cpu_mask_t, mask_deleter> result_mask_guard(&result_mask);

            for (int i = 0; i < num_numa_nodes; ++i) {
                // TODO: separate mask filling and attempts of its satisfaction.
                topology.fill_constraints_affinity_mask(pd_mask, numa_indices[i],
                                                        constraint.core_type_id,
                                                        constraint.threads_per_core);

                // TODO: Subtract the masks from other subconstraints here so that two subconstrains
                // do not occupy the same mask

                if ( hwloc_bitmap_iszero(pd_mask) )
                    continue;   // The result mask is empty, continue with the next numa node

                uint32_t constraint_max = infer_constraint_max_concurrency(
                    constraint.max_concurrency, /*fallback_concurrency*/process_concurrency, pd_mask
                );

                __TCM_ASSERT(constraint_min <= constraint_max, "Broken concurrency in constraint");

                negotiable_snapshot_t stakeholders = try_satisfy(ph, constraint.min_concurrency,
                                                                 constraint_max,
                                                                 current_concurrency, pd_mask);

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
                        if (stakeholders.num_available() < result_snapshot.num_available()) {
                            // Found the mask with smaller availability
                            result_snapshot = stakeholders;
                            hwloc_bitmap_copy(result_mask, pd_mask);
                        }
                    } else if (result_snapshot.num_immediately_available()
                               < stakeholders.num_immediately_available())
                    {
                        result_snapshot = stakeholders; // Found the mask with lesser negotiations
                        hwloc_bitmap_copy(result_mask, pd_mask);
                    }
                } else if (result_snapshot.num_available() < stakeholders.num_available()) {
                    is_desired_satisfied = constraint_max <= stakeholders.num_available();
                    result_snapshot = stakeholders;
                    hwloc_bitmap_copy(result_mask, pd_mask);
                }
            } // for each numa index
            hwloc_bitmap_copy(pd_mask, result_mask);
        } else {
            // NUMA node ID specified explicitly or left default
            topology.fill_constraints_affinity_mask(pd_mask, constraint.numa_id,
                                                    constraint.core_type_id,
                                                    constraint.threads_per_core);
            result_snapshot = try_satisfy(ph, constraint, current_concurrency, pd_mask);
        }

        return result_snapshot;
    }

    struct stakeholder_cache {
        stakeholder_cache(std::size_t stakeholders_size)
                : total_immediately_available(0), total_negotiable(0)
                , stakeholders{stakeholders_size} {}

        uint32_t total_immediately_available = 0;
        uint32_t total_negotiable = 0;
        std::vector<negotiable_snapshot_t> stakeholders; // Per constraint negotiable snapshot.
    };
    // Loops over all the constraints in the given array and populate the list of negotiable
    // snapshots.
    // TODO: rename to "calculate constraint snapshots" or something
    void try_satisfy_constraints(stakeholder_cache& sc, const tcm_permit_request_t& req,
                                 tcm_permit_handle_t ph)
    {
        for (uint32_t constr_idx = 0; constr_idx < req.constraints_size; ++constr_idx) {
            const tcm_cpu_constraints_t& constraint = req.cpu_constraints[constr_idx];
            __TCM_ASSERT(
                constraint.max_concurrency == tcm_automatic ||
                constraint.min_concurrency <= constraint.max_concurrency,
                "Invalid constraint."
            );

            negotiable_snapshot_t& snapshot = sc.stakeholders[constr_idx];
            tcm_permit_data_t& pd = ph->data;
            tcm_cpu_mask_t& pd_mask = pd.cpu_mask[constr_idx];
            const uint32_t current_concurrency = pd.concurrency[constr_idx]
                .load(std::memory_order_relaxed);
            if (constraint.mask) {
                if (hwloc_bitmap_iszero(pd_mask)) {
                    // Assigning the mask for the first time, it should not change
                    // afterwards
                    hwloc_bitmap_and(pd_mask, constraint.mask, process_mask);
                }
                snapshot = try_satisfy(ph, constraint, current_concurrency, pd_mask);
            } else {
                // assume hwloc_bitmap_copy works in case the same mask is passed as its
                // src and dst arguments
                snapshot = try_satisfy_high_level_constraints(ph, constraint, current_concurrency,
                                                              pd_mask);
            }

            sc.total_negotiable += snapshot.num_negotiable();
            sc.total_immediately_available += snapshot.num_immediately_available();
        }
    }


    struct fulfillment_decision_t { // per constraint decision
        uint32_t to_assign = 0; // concurrency to set
        uint32_t to_negotiate = 0; // concurrency that needs to be negotiated out from the permits.
                                   // Its value is included into the concurrency field.
        int32_t need = 0;          // Negative value means missed resource num to satisfy required
                                   // concurrency, positive - desired concurrency.
        renegotiable_resources_queue_t permits; // contributing permits
    };

    struct fulfillment_t {
        fulfillment_t(std::size_t size = 0)
            : num_satisfiable(0), num_negotiable(0), pending_constraints_indices(0),
              decisions(size) {}

        uint32_t num_satisfiable;      // the maximum number of resources that can be satisfied. If
                                       // less than min_sw_threads, the permit cannot be activated,
                                       // hence must be left in the PENDING state.

        uint32_t num_negotiable;       // the amount of resources required to negotiate before
                                       // activating the permit. (included into num_satisfiable)

        std::vector<int> pending_constraints_indices; // indices of the constraints array whose
                                                      // required concurrency cannot be satisfied.
        std::vector<fulfillment_decision_t> decisions; // per constraint prescription
    };

    // Distributes as max as possible of the desired concurrency
    fulfillment_t calculate_updates(const tcm_permit_request_t& req, tcm_permit_handle_t ph,
                                   const stakeholder_cache& cache)
    {
        __TCM_ASSERT(0 <= req.max_sw_threads, "Maximum requested threads must be known.");

        const std::vector<negotiable_snapshot_t>& sh = cache.stakeholders;
        fulfillment_t fulfillment(/*decisions array size*/sh.size());
        std::vector<fulfillment_decision_t>& decision = fulfillment.decisions;

        // To satisfy a constrained request means allocating a number of resources from the
        // [min_sw_threads, max_sw_threads] interval taken from the parts of the platform via
        // specified constraints array.
        uint32_t left_to_find = req.max_sw_threads;

        // Distributing required concurrency first
        for (std::size_t i = 0; i < sh.size() && left_to_find; ++i) {
            const negotiable_snapshot_t& cns = sh[i]; // negotiable snapshot for single constraint
            uint32_t to_find_i = std::min(left_to_find, cns.adjusted_min_concurrency());
            uint32_t new_concurrency = std::min(cns.num_immediately_available(), to_find_i);

            uint32_t to_negotiate = to_find_i - new_concurrency;
            to_negotiate = std::min(cns.num_negotiable(), to_negotiate);

            new_concurrency += to_negotiate; // Concurrency that was found for the constraint

            decision[i].to_negotiate = to_negotiate;
            fulfillment.num_negotiable += to_negotiate;

            if (new_concurrency < cns.adjusted_min_concurrency()) {
                // Cannot negotiate necessary amount of resources for constraint. The permit is
                // going to be left in PENDING state.
                fulfillment.pending_constraints_indices.push_back(static_cast<int>(i));
                // Value < 0 means cannot satisfy required concurrency.
                decision[i].need = new_concurrency - cns.adjusted_min_concurrency();
            } else {
                __TCM_ASSERT(cns.adjusted_min_concurrency() == new_concurrency,
                             "Incorrect invariant.");
            }
            decision[i].to_assign = new_concurrency;
            fulfillment.num_satisfiable += new_concurrency;

            __TCM_ASSERT(new_concurrency <= left_to_find,
                         (std::string("Incorrect calculation of concurrency to assign for the ") +
                          std::to_string(i) + std::string("-th constraint of permit handle ") +
                          std::to_string(std::uintptr_t(ph))).c_str());
            suppress_unused_warning(ph);
            left_to_find -= new_concurrency;

            decision[i].permits = cns.get_contributing_permits();
        }

        if (!fulfillment.pending_constraints_indices.empty()) {
            // Cannot satisfy required concurrency on at least single constraint
            return fulfillment;
        }

        // TODO: Rewrite the algorithm to fix the following:
        // 1) Distribute the resources which do not require negotiation first (i.e.
        //    cache.total_immediately_available())
        // 2) Balance the left to be negotiated to avoid end up having to distribute the amount
        //    that is less than any subconstraint.min_concurrency

        for (std::size_t i = 0; i < sh.size() && left_to_find; ++i) {

            if (decision[i].to_negotiate)
                // Even satisfying minimum requires negotiation. Do not grab more than required in
                // this constraint then.
                continue;

            // Satisfying minimum for this constraint does not require negotiation. Try satisfying
            // more to reach desired concurrency, still without negotiating.

            const negotiable_snapshot_t& cns = sh[i];
            uint32_t to_find_i = cns.adjusted_max_concurrency() - cns.adjusted_min_concurrency();
            to_find_i = std::min(left_to_find, to_find_i);

            const auto available_i = cns.num_immediately_available() - decision[i].to_assign;
            const auto assign_further = std::min(available_i, to_find_i);

            uint32_t to_negotiate = to_find_i - assign_further;
            to_negotiate = std::min(cns.num_negotiable(), to_negotiate);

            const uint32_t able_to_satisfy =
                decision[i].to_assign + assign_further + to_negotiate;

            // Value > 0 means cannot satisfy desired concurrency.
            decision[i].need = cns.adjusted_max_concurrency() - able_to_satisfy;
            __TCM_ASSERT(decision[i].need >= 0, "Incorrect invariant.");

            __TCM_ASSERT(assign_further + to_negotiate <= left_to_find,
                         (std::string("Trying to satisfy more than needed for the ") +
                          std::to_string(i) + std::string("-th constraint of permit handle ") +
                          std::to_string(std::uintptr_t(ph))).c_str());

            // Do not negotiate if minimum has been satisfied
            decision[i].to_assign += assign_further;
            fulfillment.num_satisfiable += assign_further;
            left_to_find -= assign_further;
        }

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
                                           tcm_permit_handle_t& ph)
    {
        permit_change_t requested_permit{ph, TCM_PERMIT_STATE_ACTIVE, {}};
        std::vector<uint32_t>& requested_permit_concurrencies = requested_permit.new_concurrencies;
        std::vector<permit_change_t> result;
        std::unordered_multimap<tcm_permit_handle_t, permit_change_t> new_grants;
        std::unordered_set<tcm_permit_handle_t> handles;

        for (fulfillment_decision_t& fd : f.decisions) {
            requested_permit_concurrencies.push_back(fd.to_assign);

            // Minimizing the number of negotiations
            while (fd.to_negotiate) { // There is something to negotiate for current constraint from
                                      // contributing permits
                __TCM_ASSERT(!fd.permits.empty(),
                             "The queue must not be empty if there is something to negotiate.");

                stakeholder_t st = fd.permits.top();
                fd.permits.pop();

                uint32_t current_negotiation = std::min(fd.to_negotiate, st.m_negotiable);

                tcm_permit_data_t& data = st.m_ph->data;
                std::vector<uint32_t> new_concurrencies{data.size};
                // TODO: introduce "dump_concurrencies" helper
                for (std::size_t i = 0; i < new_concurrencies.size(); ++i) {
                    new_concurrencies[i] = data.concurrency[i].load(std::memory_order_relaxed);
                }
                if (st.m_constraint_index == negotiable_snapshot_t::among_all_constraints)
                    st.m_constraint_index = 0;
                new_concurrencies[st.m_constraint_index] -= current_negotiation;
                __TCM_ASSERT(
                    !st.m_ph->request.cpu_constraints ||
                    st.m_ph->request.cpu_constraints[st.m_constraint_index].min_concurrency <=
                    int32_t(new_concurrencies[st.m_constraint_index]),
                    "Wrongly computed negotiation found."
                );

                permit_change_t pc{st.m_ph, data.state.load(std::memory_order_relaxed),
                                   new_concurrencies};
                new_grants.insert( std::make_pair(st.m_ph, pc) );

                handles.insert(st.m_ph);

                fd.to_negotiate -= current_negotiation;
            } // while there is something to negotiate
        } // for each constraint decision
        result.push_back(std::move(requested_permit));

        // Merging constraints negotiations for each permit handle
        for (tcm_permit_handle_t curr_ph : handles) {
            auto range = new_grants.equal_range(curr_ph);
            permit_change_t pc = range.first->second;
            std::vector<uint32_t>& concurrencies = pc.new_concurrencies;
            for (auto it = ++range.first; it != range.second; ++it) {
                auto& current_concurrencies = it->second.new_concurrencies;
                for (std::size_t i = 0; i < current_concurrencies.size(); ++i) {
                    // "merge" means to use negotiated value (which is less than the current)
                    if (current_concurrencies[i] < concurrencies[i])
                        concurrencies[i] = current_concurrencies[i];
                }
            }
            result.push_back( std::move(pc) );
        }
        return result;
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

        __TCM_ASSERT(req.max_sw_threads >= 0, "Cannot satisfy indefinite request.");

        stakeholder_cache sc{/*constraints array length*/ph->data.size};
        if (req.cpu_constraints && process_mask) {
            __TCM_ASSERT(req.constraints_size > 0, "Size of constraints array is not specified.");
            try_satisfy_constraints(sc, req, ph);
        } else {
            // TODO: Unify the approach by acting as if it is the permit with single constraint that
            // has process mask set as its cpu_mask. This allows reusing of the
            // try_satisfy_constraints() method

            const tcm_permit_data_t& pd = ph->data;
            const uint32_t current_concurrency = pd.concurrency[0].load(std::memory_order_relaxed);

            __TCM_ASSERT(1 == pd.size, "Act as if it is the permit with single constraint");

            negotiable_snapshot_t& snapshot = sc.stakeholders[0];
            snapshot.set_immediately_available(current_concurrency + available_concurrency_snapshot);
            snapshot.set_adjusted_concurrencies(req.min_sw_threads, req.max_sw_threads);

            // Considering that all the permits affect the current one
            for (tcm_permit_handle_t& ph_i : permits) {
                const tcm_permit_request_t& ph_req = ph_i->request;
                const tcm_permit_state_t ph_state = ph_i->data.state.load(std::memory_order_relaxed);

                if (ph_i == ph) {
                    // __TCM_ASSERT(ph_i != ph, "The being satisfied permit should be skipped.");

                    // This might be the permit for which the amount of allotted resources is
                    // reconsidered. Skip it in calculation of contributing stakeholders.
                    continue;
                }

                if (!is_owning_resources(ph_state))
                    continue;

                auto permit_grant = get_permit_grant(ph_i);
                __TCM_ASSERT(0 <= ph_req.min_sw_threads, "Exact number for required threads was not given.");

                uint32_t negotiable = 0;
                if (is_negotiable(ph_state, ph_i->data.flags))
                    negotiable = permit_grant - ph_req.min_sw_threads;

                stakeholder_t stakeholder{
                    ph_i, negotiable_snapshot_t::among_all_constraints, negotiable
                };
                snapshot.add(stakeholder);
            }

            sc.total_negotiable = snapshot.num_negotiable();
            sc.total_immediately_available = snapshot.num_immediately_available();
        }
        fulfillment_t fulfillment = calculate_updates(req, ph, sc);

        return fulfillment;
    }

    virtual void renegotiate_permits(tcm_permit_handle_t initiator) = 0;

    virtual std::vector<permit_change_t> adjust_existing_permit(const tcm_permit_request_t& req,
                                                                tcm_permit_handle_t permit) = 0;

    tcm_permit_handle_t
    make_new_permit(const tcm_client_id_t clid, const tcm_permit_request_t& req) {
        tracer t("ThreadComposabilityManagerBase::make_new_permit");

        tcm_permit_handle_t ph = new tcm_permit_rep_t;

        ph->epoch.store(0, std::memory_order_relaxed);
        tcm_permit_data_t& pd = ph->data;

        pd.client_id = clid;
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
            allocate_constraints_by_copy(pr);

            pd.size = req.constraints_size;
            pd.cpu_mask = new tcm_cpu_mask_t[pd.size];
            for (uint32_t i = 0; i < pd.size; ++i) {
                pd.cpu_mask[i] = hwloc_bitmap_alloc();
                __TCM_ASSERT(hwloc_bitmap_iszero(pd.cpu_mask[i]), "Not empty mask");
            }
        }

        pd.concurrency = new std::atomic<uint32_t>[pd.size]{0};
        pd.state.store(TCM_PERMIT_STATE_PENDING, std::memory_order_relaxed);
        pd.flags = req.flags;

        return ph;
    }

    void release_permit_impl(tcm_permit_handle_t ph) {
        tracer t("ThreadComposabilityBase::release_permit_impl");
        __TCM_ASSERT(ph, nullptr);

        bool additional_concurrency_available = false;
        tcm_permit_data_t& pd = ph->data;
        {
            const std::lock_guard<std::mutex> l(data_mutex);
            __TCM_ASSERT(is_valid(ph), "Invalid permit is being released.");

            tcm_permit_request_t& req = ph->request;
            if (req.cpu_constraints)
              deallocate_constraints(req);
            permits.erase(std::remove(permits.begin(), permits.end(), ph), permits.end());

            permit_to_callback_arg_map.erase(ph);

            auto client_phs = client_to_permit_mmap.equal_range(pd.client_id);
            for (auto it = client_phs.first; it != client_phs.second; ++it) {
                if (it->second == ph) {
                    client_to_permit_mmap.erase(it);
                    break;
                }
            }

            available_concurrency += get_permit_grant(pd);
            if (available_concurrency > 0)
                additional_concurrency_available = true;
        }

        if (pd.cpu_mask) {
            for (uint32_t i = 0 ; i < pd.size; ++i) {
                hwloc_bitmap_free(pd.cpu_mask[i]);
            }
            delete [] pd.cpu_mask;
        }
        delete [] pd.concurrency;
        delete ph;

        if (additional_concurrency_available) {
            t.log("ThreadComposabilityBase:: going to renegotiate permits");
            renegotiate_permits(/*initiator*/nullptr);
        }
    }

    tcm_result_t update_permit(tcm_permit_handle_t ph, const tcm_permit_state_t curr_state,
                              const tcm_permit_state_t new_state)
    {
        // TODO: refactor this function to have uniform approach where applicable,
        // and split responsibilities otherwise.
        tracer t("ThreadComposabilityBase::update_flags");
        __TCM_ASSERT(!is_void(curr_state), "Updating void permit.");
        __TCM_ASSERT(curr_state != new_state,
                     "Setting permit state to be identical to current.");
        tcm_permit_data_t& pd = ph->data;
        if (is_void(new_state)) {
            release_permit_impl(ph);
        } else if (is_activating_from_inactive(curr_state, new_state)) {
            update_callbacks_t callbacks;
            {
                const std::lock_guard<std::mutex> l(data_mutex);
                __TCM_ASSERT(is_valid(ph), "Updating non-existing permit.");
                std::vector<permit_change_t> updates = adjust_existing_permit(ph->request, ph);
                callbacks = apply(*this, ph, updates);
            }
            invoke_callbacks(callbacks);
        } else if (is_deactivating(curr_state, new_state)) {
            // The transition to inactive state threads requires relinquishing of resources.
            // TODO: consider using adjust_existing_permit
            uint32_t released_concurrency = 0;
            {
                // TODO: investigate whether taking the resources back is a necessary step here. To
                // check whether policies automatically consider resoruces from inactive permits as
                // available for redistribution?
                const std::lock_guard<std::mutex> l(data_mutex);
                released_concurrency = move_to_inactive(ph);
                available_concurrency += released_concurrency;
            }
            if (released_concurrency > 0)
                renegotiate_permits(/*initiator*/nullptr); // specify nullptr as the initiator value
                                                           // in order to skip negotiation for this
                                                           // deactivated permit
        } else {
            const std::lock_guard<std::mutex> l(data_mutex);
            __TCM_ASSERT(is_valid(ph), "Updating non-existing permit.");
            prepare_permit_modification(ph);
            pd.state.store(new_state, std::memory_order_relaxed);
            commit_permit_modification(ph);
        }
        return TCM_RESULT_SUCCESS;
    }
}; // class ThreadComposabilityBase

class ThreadComposabilityFCFSCImpl : public ThreadComposabilityManagerBase {
public:
    void renegotiate_permits(tcm_permit_handle_t initiator) override {
        tracer t("ThreadComposabilityFCFSCImpl::renegotiate_permits");
        int32_t available_concurrency_snapshot;
        {
            const std::lock_guard<std::mutex> l(data_mutex);
            available_concurrency_snapshot = available_concurrency;
        }

        // TODO: consider comparing performance with other containers such as
        // std::deque.
        std::vector<tcm_permit_handle_t> skipped_permits;

        // we contact enough clients that could absorb
        // available_concurrency_snapshot
        // Not sure this is the best approach
        while (available_concurrency_snapshot > 0) {
            tcm_permit_handle_t current_ph;
            uint32_t current_grant = 0;
            int32_t delta = 0;
            update_callbacks_t callbacks;
            {
                const std::lock_guard<std::mutex> l(data_mutex);
                if (renegotiation_deque.empty() || !available_concurrency)
                    break;
                current_ph = renegotiation_deque.front();
                renegotiation_deque.pop_front();

                if (!is_valid(current_ph))
                    // permit is not valid anymore, skip it.
                    continue;

                tcm_permit_data_t& pd = current_ph->data;
                if (skip_permit_negotiation(current_ph, initiator)) {
                    skipped_permits.push_back(current_ph);
                    continue;
                }

                const tcm_permit_request_t& req = current_ph->request;

                __TCM_ASSERT(
                    1 == client_to_callback_map.count(pd.client_id),
                    "The group has no corresponding callback."
                );
                suppress_unused_warning(pd);

                current_grant = get_permit_grant(current_ph);
                delta = (int32_t)req.max_sw_threads - current_grant;
                if (delta <= 0)
                    // There was a renegotiation for this permit for higher concurrency,
                    // which has been already partially or fully satisfied. Therefore, the
                    // previous request is not valid anymore.
                    continue;

                __TCM_ASSERT(
                    1 == permit_to_callback_arg_map.count(current_ph),
                    "The permit has no argument for the client callback."
                );

                fulfillment_t ff = try_satisfy_request(req, current_ph, available_concurrency);
                __TCM_ASSERT(ff.num_negotiable <= ff.num_satisfiable,
                             "Number of negotiated must be included into total number of found resources.");
                __TCM_ASSERT(req.min_sw_threads <= int32_t(ff.num_satisfiable) &&
                                                   int32_t(ff.num_satisfiable) <= req.max_sw_threads,
                             "Found resources should be within the requested limits.");

                const int32_t immediately_satisfiable = ff.num_satisfiable - ff.num_negotiable;
                if (immediately_satisfiable < req.min_sw_threads ||
                    !ff.pending_constraints_indices.empty())
                {
                    // Cannot satisfy minimum requested concurrency without negotiation or failed to satisfy
                    // the minimum concurrency for at least one constraint. The permit should be left in the
                    // PENDING state.

                    // Push the request back to the renegotiation queue if it's still not
                    // fully satisfied or there is no concurrency available.
                    // TODO: Also, if we have an unsatisfied pending permit, we can return it
                    // to the beginning of the queue until it be satisfied.
                    // So, this approach may increase the amount of active permits in some cases.
                    // It needs to be investigated.

                    // Reuse the skipped permits container to avoid infinite looping.
                    skipped_permits.push_back(current_ph);
                } else {
                    if (immediately_satisfiable < req.max_sw_threads) {
                        // Cannot satisfy maximum requested (desired) concurrency. Put the permit
                        // into renegotiation deque, so that the amount of resources given to it is
                        // reconsidered whenever resource distribution state changes.

                        // Reuse the skipped permits container to avoid infinite looping.
                        skipped_permits.push_back(current_ph);
                    }
                    // Since TCM is able to satisfy required concurrency without negotiating resources with
                    // other permits and this is FCFS policy, nullify negotiation part.
                    nullify_negotiations(ff);
                    auto updates = negotiate(ff, req, current_ph);
                    // Pass nullptr as the initiator value because the change in resource
                    // distribution is not initiated by current permit. This helps to force callback
                    // invocation for this permit, hence notifying it about the changes.
                    callbacks = apply(*this, /*initiator*/nullptr, updates);
                }
                available_concurrency_snapshot = available_concurrency;
            } // end of mutex lock scope

            invoke_callbacks(callbacks);

        } // end of while resources exist

        // Return non-renegotiable permits back
        const std::lock_guard<std::mutex> l(data_mutex);
        renegotiation_deque.insert(
            renegotiation_deque.end(),
            skipped_permits.begin(), skipped_permits.end()
        );
    }

    std::vector<permit_change_t> adjust_existing_permit(const tcm_permit_request_t &req,
                                                        tcm_permit_handle_t ph) override
    {
        // The data_mutex lock must be taken
        tracer t("ThreadComposabilityFCFSCImpl::adjust_existing_permit");
        __TCM_ASSERT(is_valid(ph), "Invalid permit.");

        fulfillment_t ff = try_satisfy_request(req, ph, available_concurrency);

        const int32_t immediately_satisfiable = ff.num_satisfiable - ff.num_negotiable;
        if (immediately_satisfiable < req.min_sw_threads ||
            !ff.pending_constraints_indices.empty())
        {
            // Cannot satisfy minimum requested (required) concurrency without negotiation or failed
            // to satisfy the minimum concurrency for at least one constraint. The permit should be
            // left in the PENDING state.
            t.log("ThreadComposabilityFCFSCImpl::NOTE p is an unsatisfied permit");
            renegotiation_deque.push_back(ph);
            return {};
        } else if (immediately_satisfiable < req.max_sw_threads) {
            // Cannot satisfy maximum requested (desired) concurrency. Put the permit into
            // renegotiation deque, so that the amount of resources given to it is reconsidered
            // whenever resource distribution state changes.
            renegotiation_deque.push_back(ph);
        }

        __TCM_ASSERT(ff.num_negotiable <= ff.num_satisfiable,
                     "Number of negotiated must be included into total number of found resources.");
        __TCM_ASSERT(req.min_sw_threads <= int32_t(ff.num_satisfiable) &&
                     int32_t(ff.num_satisfiable) <= req.max_sw_threads,
                     "Found resources should be within the requested limits.");

        nullify_negotiations(ff);
        return negotiate(ff, req, ph);
    }
private:
    std::deque<tcm_permit_handle_t> renegotiation_deque;

    void nullify_negotiations(fulfillment_t& ff) {
        ff.num_negotiable = 0;
        for (auto& fd : ff.decisions) {
            fd.to_assign -= fd.to_negotiate;
            fd.to_negotiate = 0;
            fd.permits = renegotiable_resources_queue_t{}; // drop contributing permits
        }
    }

};

class ThreadComposabilityFairBalance : public ThreadComposabilityManagerBase {
  // Stores the keys in the map of existing permits.
  std::deque<tcm_permit_handle_t> renegotiation_deque;

protected:
    //! Calculate concurrency for ACTIVE/IDLE permits in accordance with the FAIR approach.
    //! If necessary, callback should be called separately.
    permit_change_t renegotiate_active_idle_permit(uint32_t distributed_concurrency,
                                                   uint32_t total_demand,
                                                   const tcm_permit_request_t& pr,
                                                   tcm_permit_handle_t ph,
                                                   uint32_t& carry)
    {
        __TCM_ASSERT(pr.min_sw_threads >= 0, "Actual value of required concurrency is expected.");
        permit_change_t update {ph,
            /*required concurrency should have been satisfied by this time*/TCM_PERMIT_STATE_ACTIVE,
            /*new_concurrencies[0] = */{unsigned(pr.min_sw_threads)}
        };

        // TODO: take account of the mask when computing allotment
        tcm_permit_data_t& pd = ph->data;
        if (pr.cpu_constraints) {
            for (uint32_t i = 0; i < pd.size; ++i) {
                update.new_concurrencies[i] = pr.cpu_constraints[i].min_concurrency;
            }
        }

        distributed_concurrency = std::min(distributed_concurrency, total_demand);
        const uint32_t demand = pr.max_sw_threads - pr.min_sw_threads;
        uint32_t allotted = 0;
        if (distributed_concurrency > 0 && total_demand > 0) {
            const uint32_t v = distributed_concurrency * demand + carry;
            allotted = v / total_demand;
            carry = v % total_demand;
            __TCM_ASSERT(allotted <= distributed_concurrency, "Distributing more than available.");
        }

        for (uint32_t i = 0; i < pd.size && allotted; ++i) {
            unsigned span = demand;
            if (pr.cpu_constraints) {
                // TODO: Consider distribution of allotted resources evenly between elements of
                // constraints array
                span = pr.cpu_constraints[i].max_concurrency - pr.cpu_constraints[i].min_concurrency;
            }
            const uint32_t additionally_assigned = std::min(allotted, span);
            update.new_concurrencies[i] += additionally_assigned;
            allotted -= additionally_assigned;
        }

        return update;
    }

    void renegotiate_permits(tcm_permit_handle_t initiator) override {
        tracer t("ThreadComposabilityFairBalance::renegotiate_permits");
        uint32_t total_granted = 0;
        uint32_t active_demand = 0;
        update_callbacks_t callbacks;

        // distribute whole platform concurrency
        uint32_t available_concurrency_snapshot = initially_available_concurrency;

        std::map<uint32_t, tcm_permit_handle_t> renegotiation_pending;
        std::vector<std::pair<int32_t, tcm_permit_handle_t>> renegotiation_active_idle;
        std::vector<permit_change_t> updates;
        {
            const std::lock_guard<std::mutex> lock(data_mutex);
            const int active_permits_upper_bound_estimate = 128;
            renegotiation_active_idle.reserve(active_permits_upper_bound_estimate);

            // Searching of ACTIVE without rigid_concurrency flag/IDLE/PENDING permits
            for (tcm_permit_handle_t& ph: permits) {
                tcm_permit_data_t& pd = ph->data;
                if (skip_permit_negotiation(ph, initiator)) {
                    const uint32_t permit_grant = get_permit_grant(ph);
                    __TCM_ASSERT(0 == permit_grant ||
                                 !is_negotiable(pd.state.load(std::memory_order_relaxed), pd.flags),
                                 "Must skip only permits that cannot negotiate.");
                    __TCM_ASSERT(available_concurrency_snapshot >= permit_grant,
                                 "Underflow detected");
                    available_concurrency_snapshot -= permit_grant;
                    continue;
                }

                const tcm_permit_request_t& pr = ph->request;

                if (is_pending(pd.state.load(std::memory_order_relaxed)))
                    renegotiation_pending.insert(std::make_pair(pr.min_sw_threads, ph));
                else {
                    active_demand += pr.max_sw_threads - pr.min_sw_threads;
                    uint32_t prev_granted = get_permit_grant(pd);
                    total_granted += pr.min_sw_threads;

                    renegotiation_active_idle.push_back(
                        std::make_pair(int32_t(prev_granted), ph)
                    );
                }
            }

            // Deduct the minimally granted amount of resources
            __TCM_ASSERT(available_concurrency_snapshot >= total_granted, "Underflow detected");
            available_concurrency_snapshot -= total_granted;

            // Process of PENDING permits first in order to activate as many permits as possible

            // Satisfied, but not yet applied, concurrency from pending requests
            uint32_t overall_pending_satisfied = 0;
            for (auto& elem : renegotiation_pending) {
                tcm_permit_handle_t ph = elem.second;
                const tcm_permit_request_t& pr = ph->request;

                fulfillment_t ff = try_satisfy_request(pr, ph, available_concurrency_snapshot);
                uint32_t required_concurrency = infer_constraint_min_concurrency(pr.min_sw_threads);

                // To this point only required concurrency was subtracted from available
                // resources, therefore, permits cannot negotiate, hence we do not take into
                // account negotiable part.
                const uint32_t satisfiable = ff.num_satisfiable - ff.num_negotiable;

                const bool is_required_fits = required_concurrency <= satisfiable &&
                    ff.pending_constraints_indices.empty();

                if (!is_required_fits)
                    continue; // there might be other (constrained) pending permits that fit

                // TODO: take account of the mask of the permits that are activating from
                // pending state
                const bool is_overall_fits =
                    overall_pending_satisfied + required_concurrency <= available_concurrency_snapshot;
                if (!is_overall_fits)
                    // No need to consider other permits since they are ordered from the lowest
                    // to highest required concurrency
                    break;

                // Accumulate with satisfied amount from the previous pending permits that were
                // activated
                overall_pending_satisfied += required_concurrency;

                renegotiation_active_idle.push_back(
                    std::make_pair(/*previously granted concurrency for pending permit*/0, ph)
                );
                __TCM_ASSERT(pr.max_sw_threads > 0, "Permit should not be in pending state.");
                active_demand += pr.max_sw_threads - required_concurrency;
            }

            available_concurrency_snapshot -= overall_pending_satisfied;

            // Processing of active and idle permits
            uint32_t carry = 0;
            for (auto& elem: renegotiation_active_idle) {
                tcm_permit_handle_t ph = elem.second;
                const tcm_permit_request_t& pr = ph->request;

                permit_change_t permit_update = renegotiate_active_idle_permit(
                    available_concurrency_snapshot, active_demand, pr, ph, carry
                );
                updates.push_back(permit_update);
            }
            callbacks = apply(*this, initiator, updates);
        }
        invoke_callbacks(callbacks);
    }

    //! TODO: Why do we have to pass req here? Can use the request from permit_to_request_map inside?
    std::vector<permit_change_t> adjust_existing_permit(const tcm_permit_request_t &req,
                                                        tcm_permit_handle_t ph) override
    {
        // The data_mutex lock must be taken
        tracer t("ThreadComposabilityFairBalance::adjust_existing_permit");

        __TCM_ASSERT(is_valid(ph), "Invalid permit.");

        // Trying to squeeze resources out of the platform, returning permits that share
        // resources needed by that ph
        fulfillment_t ff = try_satisfy_request(req, ph, available_concurrency);

        if (int32_t(ff.num_satisfiable) < req.min_sw_threads) {
            return {}; // Also works if min_sw_threads == tcm_automatic
        }

        if ( !ff.pending_constraints_indices.empty() ) {
            // Failed to satisfy the minimum concurrency for at least one constraint. The permit
            // should be left in the PENDING state.
            return {};
        }
        __TCM_ASSERT(ff.num_negotiable <= ff.num_satisfiable,
                     "Number of negotiated must be included into total number of found resources.");
        __TCM_ASSERT(req.min_sw_threads <= int32_t(ff.num_satisfiable) &&
                                           int32_t(ff.num_satisfiable) <= req.max_sw_threads,
                     "Found resources should be within the requested limits.");

        t.log("ThreadComposabilityFairBalance::NOTE satisfying the permit requires renegotiation.");
        std::vector<permit_change_t> updates = negotiate(ff, req, ph);
        return updates;
    }
}; // ThreadComposabilityFairBalance
} // namespace internal

class ThreadComposabilityManager {
  std::unique_ptr<internal::ThreadComposabilityManagerBase> impl_;
public:
  explicit ThreadComposabilityManager(const internal::environment& tcm_env) {
    std::string tcm_strategy = tcm_env.tcm_resource_distribution_strategy;

    if (tcm_strategy == "FCFS") {
      impl_.reset(new internal::ThreadComposabilityFCFSCImpl);
    } else if (tcm_strategy == "FAIR") {
      impl_.reset(new internal::ThreadComposabilityFairBalance);
    } else {
      __TCM_ASSERT(false, "Incorrect value of TCM_RESOURCE_DISTRIBUTION_STRATEGY environment variable.");
    }
  }

  tcm_client_id_t register_client(tcm_callback_t r) {
    internal::tracer t("ThreadComposability::register_client");
    return impl_->register_client(r);
  }

  void unregister_client(tcm_client_id_t clid) {
    internal::tracer t("ThreadComposability::unregister_client");
    impl_->unregister_client(clid);
  }

  bool request_permit(tcm_client_id_t clid, tcm_permit_request_t& req,
                      void* callback_arg, tcm_permit_handle_t* permit_handle,
                      tcm_permit_t* permit) {
    internal::tracer t("ThreadComposability::request_permit");
    return impl_->request_permit(clid, req, callback_arg, permit_handle, permit);
  }

  tcm_result_t get_permit(tcm_permit_handle_t ph, tcm_permit_t* p) {
    internal::tracer t("ThreadComposability::get_permit");
    return impl_->get_permit(ph, p);
  }

  tcm_result_t idle_permit(tcm_permit_handle_t p) {
    internal::tracer t("ThreadComposability::idle_permit");
    return impl_->idle_permit(p);
  }

  tcm_result_t activate_permit(tcm_permit_handle_t p) {
    internal::tracer t("ThreadComposability::activate_permit");
    return impl_->activate_permit(p);
  }

  tcm_result_t deactivate_permit(tcm_permit_handle_t p) {
    internal::tracer t("ThreadComposability::deactivate_permit");
    return impl_->deactivate_permit(p);
  }

  tcm_result_t release_permit(tcm_permit_handle_t p) {
    internal::tracer t("ThreadComposability::release_permit");
    return impl_->release_permit(p);
  }

  tcm_result_t register_thread(tcm_permit_handle_t p) {
    internal::tracer t("ThreadComposability::register_thread");
    return impl_->register_thread(p);
  }

  tcm_result_t unregister_thread() {
    internal::tracer t("ThreadComposability::register_thread");
    return impl_->unregister_thread();
  }

};

class theTCM {
  static ThreadComposabilityManager* tcm_ptr;
  static std::size_t reference_count;
  static std::mutex tcm_mutex;
  static internal::environment tcm_env;

public:
  static bool is_enabled() { 
    return tcm_env.tcm_disable == 0;
  }

  friend float internal::tcm_oversubscription_factor();

  static void increase_ref_count() {
    std::lock_guard<std::mutex> l(tcm_mutex);
    if (reference_count++)
      return;
    tcm_ptr = new ThreadComposabilityManager(tcm_env);
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
};


ThreadComposabilityManager* theTCM::tcm_ptr{nullptr};
std::size_t theTCM::reference_count{0};
std::mutex theTCM::tcm_mutex{};
internal::environment theTCM::tcm_env{};

float internal::tcm_oversubscription_factor() {
  static const float oversb_factor = theTCM::tcm_env.tcm_oversubscription_factor;
  __TCM_ASSERT(oversb_factor > std::numeric_limits<float>::epsilon(),
                "Incorrect value of TCM_OVERSUBSCRIPTION_FACTOR environment variable.");
  return oversb_factor;
}

} // namespace tcm

extern "C" {

///////////////////////////////////////////////////////////////////////////////
/// @brief Initialize the connection to the thread composability manager
///
/// @details
///     - The client must call this function before calling any other
///       resource management function.
///
/// @returns
///     - ::TCM_RESULT_SUCCESS
///     - ::TCM_RESULT_ERROR_UNKNOWN
tcm_result_t tcmConnect(tcm_callback_t callback, tcm_client_id_t *client_id)
{
  using tcm::theTCM;
  tcm::internal::tracer t("tcmConnect");

  if (theTCM::is_enabled() && client_id) {
    theTCM::increase_ref_count();
    auto& mgr = theTCM::instance();
    *client_id = mgr.register_client(callback);
    if (*client_id)
      return TCM_RESULT_SUCCESS;
  }
  return TCM_RESULT_ERROR_UNKNOWN;
}

/// @brief Terminate the connection with the thread composability manager
///
/// @details
///     - Must be called whenever the client, which is seen as a set of permits associated
///       with the given client_id, finishes its work with the thread composability manager
///       and no other calls, possibly except for tcmConnect are expected to be made
///       from that client.
///
/// @returns
///     - ::TCM_RESULT_SUCCESS
///     - ::TCM_RESULT_ERROR_UNKNOWN
tcm_result_t tcmDisconnect(tcm_client_id_t client_id)
{
  using tcm::theTCM;
  tcm::internal::tracer t("tcmDisconnect");

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
  tcm::internal::tracer t("tcmRequestPermit");

  int32_t sum_min = 0, sum_max = 0;
  if (request.cpu_constraints) {
    if (request.constraints_size <= 0)
      return TCM_RESULT_ERROR_INVALID_ARGUMENT;

    for (uint32_t i = 0; i < request.constraints_size; ++i) {
      const tcm_cpu_constraints_t& c = request.cpu_constraints[i];
      int32_t adjusted_min = 0;
      if (c.min_concurrency != tcm_automatic) {
        if (c.min_concurrency < 0) {
          return TCM_RESULT_ERROR_INVALID_ARGUMENT;
        }
        adjusted_min = c.min_concurrency;
      }
      sum_min += adjusted_min;

      int32_t adjusted_max = 1024; // TODO: use process concurrency as the default
      if (c.max_concurrency != tcm_automatic) {
        if (c.max_concurrency < 0) {
          return TCM_RESULT_ERROR_INVALID_ARGUMENT;
        }
        adjusted_max = c.max_concurrency;
      } else if (c.mask) {
        // TODO: handle infinitely set masks
        adjusted_max = tcm::internal::get_mask_concurrency(c.mask);
      }
      sum_max += adjusted_max;

      if (adjusted_max < adjusted_min)
        return TCM_RESULT_ERROR_INVALID_ARGUMENT;
    }
  }

  if (request.min_sw_threads != tcm_automatic && sum_min > request.min_sw_threads) {
    return TCM_RESULT_ERROR_INVALID_ARGUMENT;
  } else if (request.max_sw_threads != tcm_automatic &&
             (request.max_sw_threads < 0 || (sum_max && sum_max < request.max_sw_threads))) {
    return TCM_RESULT_ERROR_INVALID_ARGUMENT;
  } else if (request.max_sw_threads < request.min_sw_threads) {
    return TCM_RESULT_ERROR_INVALID_ARGUMENT;
  } else if (!permit_handle) {
    return TCM_RESULT_ERROR_INVALID_ARGUMENT;
  }

  // Even for default-initialized request parameters, the meaningful values can be inferred.

  // request.min_sw_threads == automatic means zero if no constraints specified. Otherwise, the sum
  // of minimum concurrencies in the constraints.

  auto& mgr = theTCM::instance();
  bool result = mgr.request_permit(client_id, request, callback_arg, permit_handle, permit);
  return result? TCM_RESULT_SUCCESS : TCM_RESULT_ERROR_INVALID_ARGUMENT;
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
  tcm::internal::tracer t("tcmGetPermitData");

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
  tcm::internal::tracer t("tcmIdlePermit");

  auto& mgr = theTCM::instance();
  if (p) {
    return mgr.idle_permit(p);
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
  tcm::internal::tracer t("zeReactivatePermit");

  auto& mgr = theTCM::instance();
  if (p) {
    return mgr.activate_permit(p);
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
  tcm::internal::tracer t("zeReactivatePermit");

  auto& mgr = theTCM::instance();
  if (p) {
    return mgr.deactivate_permit(p);
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
tcm_result_t tcmReleasePermit(tcm_permit_handle_t p) {
  using tcm::theTCM;
  tcm::internal::tracer t("tcmReleasePermit");

  auto& mgr = theTCM::instance();
  if (p) {
    return mgr.release_permit(p);
  }
  return TCM_RESULT_ERROR_UNKNOWN;
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

  auto& mgr = theTCM::instance();
  if (p) {
    return mgr.register_thread(p);
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

  auto& mgr = theTCM::instance();
  return mgr.unregister_thread();
}

}
