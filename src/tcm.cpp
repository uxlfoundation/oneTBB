/*
 *
 * Copyright (C) 2021-2022 Intel Corporation
 *
 */

#include "tcm.h"
#include "hwloc.h"
#include "detail/_tcm_assert.h"

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
#include <vector>

#if __RM_ENABLE_TRACER
#include <iostream>
#endif

struct zerm_permit_data_t {
  zerm_client_id_t client_id;
  std::atomic<uint32_t> concurrency;
  zerm_cpu_mask_t cpu_mask;
  std::atomic<zerm_permit_state_t> state;
  zerm_permit_flags_t flags;
};

extern "C" {

typedef uint64_t zerm_permit_epoch_t;

struct zerm_permit_rep_t {
  std::atomic<zerm_permit_epoch_t> epoch;
  zerm_permit_data_t data;
};

} // extern "C"

namespace tcm {
namespace internal {

#if __RM_ENABLE_TRACER
struct tracer {
  const std::string s_;
  tracer(const std::string &s) : s_(s) {
    std::cout << "Entering " << s_ << std::endl;
  }
  ~tracer() {
    std::cout << "Leaving " << s_ << std::endl;
  }
};
#else
struct tracer {
  tracer(const std::string &) {}
  ~tracer() {}
};
#endif

bool operator==(const zerm_permit_flags_t& lhs, const zerm_permit_flags_t& rhs) {
  return
    lhs.stale == rhs.stale &&
    lhs.rigid_concurrency == rhs.rigid_concurrency &&
    lhs.exclusive == rhs.exclusive;
}

bool operator==(const zerm_permit_request_t& lhs, const zerm_permit_request_t& rhs) {
  return
    lhs.min_sw_threads == rhs.min_sw_threads &&
    lhs.max_sw_threads == rhs.max_sw_threads &&
    lhs.cpu_constraints == rhs.cpu_constraints &&
    lhs.flags == rhs.flags;
}

unsigned int hardware_concurrency() {
  return std::thread::hardware_concurrency();
}

//! Returns available platform resources, taking into account the possible degree
//! of the oversubscription (oversb_factor must be greater than zero).
uint32_t platform_resources () {
  static uint32_t concurrency = [] {
    const char* oversb_factor_env_value = std::getenv("RM_OVERSUBSCRIPTION_FACTOR");
    float oversb_factor = 1.0f;
    if (oversb_factor_env_value) {
      // TODO: Consider alternative options for std::stof
      oversb_factor = std::stof(oversb_factor_env_value);
      __TCM_ASSERT(oversb_factor > std::numeric_limits<float>::epsilon(),
                  "Incorrect value of RM_OVERSUBSCRIPTION_FACTOR environment variable.");
    }

    return uint32_t(oversb_factor * hardware_concurrency());
  }();

  return concurrency;
}

struct ThreadComposabilityManagerData {
  std::mutex data_mutex;

  zerm_client_id_t client_id = 1;

  //! The count of available resources.
  uint32_t available_concurrency = platform_resources();

  //! The map of existing permits and corresponding requests.
  std::unordered_map<zerm_permit_handle_t, zerm_permit_request_t> permit_to_request_map;

  //! The map of callbacks per each client. Callbacks are used during
  //! renegotiation of permits.
  std::unordered_map<zerm_client_id_t, zerm_callback_t> client_to_callback_map;

  //! The map of callback arguments associated with permits. Used during
  //! callback invocation.
  std::unordered_map<zerm_permit_handle_t, void*> permit_to_callback_arg_map;

  //! The multimap of permits associated with the given client.
  std::unordered_multimap<zerm_client_id_t, zerm_permit_handle_t> client_to_permit_mmap;

  // TODO: Consider alternative data structure for maintaining
  // registering/unregistering of threads by answering:
  // - Do we need to identify threads that register or unregister themselves?
  //   - Will atomic counting suffice?
  // - To avoid search in a container, move atomic counter into permit?
  //   - Can we cast array of chars to an atomic memory?

  //! The mutex for the map of registering threads.
  std::mutex threads_map_mutex;

  //! The map of threads that register to be a part of a permit.
  std::unordered_map<std::thread::id, zerm_permit_handle_t> thread_to_permit_map;
};

class ThreadComposabilityManagerBase : public ThreadComposabilityManagerData {
public:
  virtual ~ThreadComposabilityManagerBase() {}

  zerm_client_id_t register_client(zerm_callback_t r) {
    tracer t("ThreadComposabilityBase::register_client");
    const std::lock_guard<std::mutex> l(data_mutex);
    zerm_client_id_t clid = client_id++;
    client_to_callback_map[clid] = r;
    return clid;
  }

  void unregister_client(zerm_client_id_t clid) {
    tracer t("ThreadComposabilityBase::unregister_client");
    const std::lock_guard<std::mutex> l(data_mutex);
    __TCM_ASSERT(client_to_permit_mmap.count(clid) == 0, "Deactivating the client with associated permits.");
    __TCM_ASSERT(client_to_callback_map.count(clid) == 1, "The client_id was not registered.");
    client_to_callback_map.erase(clid);
  }

  zerm_permit_handle_t request_permit(zerm_client_id_t clid, zerm_permit_request_t& req,
                                      void* callback_arg, zerm_permit_handle_t ph,
                                      zerm_permit_t* permit)
  {
    tracer t("ThreadComposabilityBase::request_permit");

    // Check the ability to satisfy minimum requested concurrency
    if (req.min_sw_threads > platform_resources()) {
      if (permit)
        permit->state = ZERM_PERMIT_STATE_VOID;
      return nullptr;
    }

    const bool is_requesting_new_permit = bool(!ph);
    if (is_requesting_new_permit) {
      ph = make_new_permit(clid, req);
    }

    {
      const std::lock_guard<std::mutex> l(data_mutex);
      // TODO: Consider adding the permit to containers after the concurrency level
      // calculation to avoid early renegotiation
      if (is_requesting_new_permit) {
        client_to_permit_mmap.emplace(ph->data.client_id, ph);
      }
      permit_to_request_map[ph] = req;
      permit_to_callback_arg_map[ph] = callback_arg;
    }

    adjust_existing_permit(req, ph);

    if (permit) {
      bool reading_succeeded = false;
      while (!reading_succeeded) {
        reading_succeeded = copy_permit(ph, permit);
      }
    }

    return ph;
  }

  ze_result_t get_permit(zerm_permit_handle_t ph, zerm_permit_t* permit) {
    tracer t("ThreadComposabilityBase::get_permit");
    __TCM_ASSERT(ph && permit, nullptr);

    if (!is_valid(ph))
      return ZE_RESULT_ERROR_UNKNOWN;

    permit->flags.stale = false;
    if (!copy_permit(ph, permit)) {
      permit->flags.stale = true;
    }

    return ZE_RESULT_SUCCESS;
  }

  ze_result_t idle_permit(zerm_permit_handle_t ph) {
    tracer t("ThreadComposabilityBase::idle_permit");
    __TCM_ASSERT(ph, nullptr);

    zerm_permit_data_t& pd = ph->data;

    zerm_permit_state_t curr_state;
    {
      const std::lock_guard<std::mutex> l(data_mutex);
      curr_state = pd.state.load(std::memory_order_relaxed);
    }

    if (!is_active(curr_state))
      return ZE_RESULT_ERROR_UNKNOWN;

    return update_permit(ph, curr_state, /*new_state*/ZERM_PERMIT_STATE_IDLE);
  }

  ze_result_t activate_permit(zerm_permit_handle_t ph) {
    tracer t("ThreadComposabilityBase::activate_permit");
    __TCM_ASSERT(ph, nullptr);

    zerm_permit_data_t& pd = ph->data;
    zerm_permit_state_t curr_state;
    {
      const std::lock_guard<std::mutex> l(data_mutex);
      curr_state = pd.state.load(std::memory_order_relaxed);
    }

    if (!is_inactive(curr_state) && !is_idle(curr_state))
      return ZE_RESULT_ERROR_UNKNOWN;

    return update_permit(ph, curr_state, /*new_state*/ZERM_PERMIT_STATE_ACTIVE);
  }

  ze_result_t deactivate_permit(zerm_permit_handle_t ph) {
    tracer t("ThreadComposabilityBase::activate_permit");
    __TCM_ASSERT(ph, nullptr);

    zerm_permit_data_t& pd = ph->data;
    zerm_permit_state_t curr_state;
    {
      const std::lock_guard<std::mutex> l(data_mutex);
      curr_state = pd.state.load(std::memory_order_relaxed);
    }

    if (!is_owning_resources(curr_state))
      return ZE_RESULT_ERROR_UNKNOWN;

    return update_permit(ph, curr_state, /*new_state*/ZERM_PERMIT_STATE_INACTIVE);
  }

  ze_result_t release_permit(zerm_permit_handle_t ph) {
    tracer t("ThreadComposabilityBase::release_permit");
    __TCM_ASSERT(ph, nullptr);

    zerm_permit_data_t& pd = ph->data;
    zerm_permit_state_t curr_state;
    {
      const std::lock_guard<std::mutex> l(data_mutex);
      curr_state = pd.state.load(std::memory_order_relaxed);
    }

    return update_permit(ph, curr_state, /*new_state*/ZERM_PERMIT_STATE_VOID);
  }

  ze_result_t register_thread(zerm_permit_handle_t ph) {
    tracer t("ThreadComposabilityBase::register_thread");
    __TCM_ASSERT(ph, nullptr);

    const std::lock_guard<std::mutex> l(threads_map_mutex);
    thread_to_permit_map[std::this_thread::get_id()] = ph;
    return ZE_RESULT_SUCCESS;
  }

  ze_result_t unregister_thread() {
    tracer t("ThreadComposabilityBase::unregister_thread");
    const std::lock_guard<std::mutex> l(threads_map_mutex);
    thread_to_permit_map[std::this_thread::get_id()] = nullptr;
    return ZE_RESULT_SUCCESS;
  }

protected:
  void prepare_permit_modification(zerm_permit_handle_t ph) {
    uint64_t prev_epoch = ph->epoch.fetch_add(1, std::memory_order_relaxed);
    __TCM_ASSERT(prev_epoch % 2 == 0, "Previous epoch value must be even.");
  }

  void commit_permit_modification(zerm_permit_handle_t ph) {
    uint64_t prev_epoch = ph->epoch.fetch_add(1, std::memory_order_release);
    __TCM_ASSERT(prev_epoch % 2 != 0, "Previous epoch value must be odd.");
  }

  zerm_permit_epoch_t prepare_permit_copying(zerm_permit_handle_t ph) const {
    return ph->epoch.load(std::memory_order_acquire);
  }

  bool has_copying_succeeded(zerm_permit_handle_t permit_handle, zerm_permit_epoch_t c) const {
    return c == permit_handle->epoch.load(std::memory_order_relaxed);
  }

  bool is_safe_to_copy(const zerm_permit_epoch_t& e) const { return e % 2 == 0; }

  bool copy_permit(zerm_permit_handle_t ph, zerm_permit_t* permit) const {
    zerm_permit_epoch_t e = prepare_permit_copying(ph);

    if (!is_safe_to_copy(e))
      return false; // someone else is modifying this permit

    zerm_permit_data_t& pd = ph->data;

    __TCM_ASSERT(permit->concurrencies, "Permit concurrencies field contains null pointer.");
    permit->concurrencies[0] = pd.concurrency.load(std::memory_order_relaxed);
    if (pd.cpu_mask && permit->cpu_masks[0]) {
      hwloc_bitmap_copy(permit->cpu_masks[0], pd.cpu_mask);
    }
    permit->size = 1;
    permit->state = pd.state.load(std::memory_order_relaxed);
    permit->flags = pd.flags;

    return has_copying_succeeded(ph, e);
  }

  // Helper to determine whether the permit is not released yet. Must be
  // called under data_mutex.
  bool is_valid(zerm_permit_handle_t ph) const {
    return 1 == permit_to_request_map.count(ph);
  }

  bool is_activating_from_inactive(const zerm_permit_state_t& curr_state,
                                   const zerm_permit_state_t& new_state) const
  {
    return is_inactive(curr_state) && is_active(new_state);
  }

  bool is_deactivating(const zerm_permit_state_t& curr_state,
                       const zerm_permit_state_t& new_state) const
  {
    return is_owning_resources(curr_state) && is_inactive(new_state);
  }

  bool is_idling_static_permit(const zerm_permit_flags_t& flags,
                               const zerm_permit_state_t& curr_state,
                               const zerm_permit_state_t& new_state) const
  {
    const bool is_idling = is_active(curr_state) && is_idle(new_state);
    return is_idling && flags.rigid_concurrency;
  }

  bool is_void(const zerm_permit_state_t& state) const {
    return ZERM_PERMIT_STATE_VOID == state;
  }

  bool is_active(const zerm_permit_state_t& state) const {
    return ZERM_PERMIT_STATE_ACTIVE == state;
  }

  bool is_inactive(const zerm_permit_state_t& state) const {
    return ZERM_PERMIT_STATE_INACTIVE == state;
  }

  bool is_idle(const zerm_permit_state_t& state) const {
    return ZERM_PERMIT_STATE_IDLE == state;
  }

  bool is_pending(const zerm_permit_state_t& state) const {
    return ZERM_PERMIT_STATE_PENDING == state;
  }

  bool is_owning_resources(const zerm_permit_state_t& state) const {
    return is_active(state) || is_idle(state);
  }

  bool is_renegotiable(const zerm_permit_state_t& state,
                       const zerm_permit_flags_t& flags) const
  {
    return !(is_active(state) && flags.rigid_concurrency);
  }

  bool skip_permit_renegotiation(zerm_permit_handle_t ph, zerm_permit_handle_t initiator,
                                 uint32_t& available_concurrency) const
  {
    if (ph == initiator)    // renegotiate for one that asked
      return false;

    zerm_permit_data_t& pd = ph->data;

    const std::memory_order relaxed = std::memory_order_relaxed;
    const zerm_permit_state_t state = pd.state.load(relaxed);

    if (!is_renegotiable(state, pd.flags)) {
      // TODO: avoid side-effects in this function
      const uint32_t permit_concurrency = pd.concurrency.load(relaxed);
      __TCM_ASSERT(available_concurrency >= permit_concurrency, "Underflow detected");
      available_concurrency -= permit_concurrency;
      return true;
    }

    if (is_owning_resources(state))
      return false;

    if (is_pending(state))
      return false;

    return true;
  }

  bool skip_callback_invocation(zerm_permit_handle_t ph, zerm_permit_handle_t initiator,
                                uint32_t previous_grant) const
  {
    if (ph == initiator)        // skip callback invocation for one that asked ...
      return true;

    // ... or if resources grant is not changed
    const auto new_grant = ph->data.concurrency.load(std::memory_order_relaxed);
    return previous_grant == new_grant;
  }

  //! Tries to meet the requested concurrency. Must be called under the data_mutex.
  //! Returns @true if requested concurrency was satisfied.
  //! TODO: Split this function into "suggestion" and "permit modification"
  bool try_satisfy_request(const zerm_permit_request_t& req, zerm_permit_handle_t ph) {
    __TCM_ASSERT(req.max_sw_threads > 0, "Incorrect concurrency requested"); 

    zerm_permit_data_t& pd = ph->data;
    std::memory_order relaxed = std::memory_order_relaxed;

    const uint32_t current_concurrency = pd.concurrency.load(relaxed);
    const int32_t delta = std::min((int32_t)available_concurrency,
                                   int32_t(req.max_sw_threads - current_concurrency));

    zerm_permit_state_t current_state = pd.state.load(relaxed);
    uint32_t new_concurrency = 0;
    zerm_permit_state_t new_state = ZERM_PERMIT_STATE_PENDING;

    const bool is_meeting_required_concurrency = current_concurrency + delta >= req.min_sw_threads;
    if (is_meeting_required_concurrency) {
      new_concurrency = current_concurrency + delta;
      available_concurrency -= delta;
      new_state = is_idle(current_state) ? ZERM_PERMIT_STATE_IDLE : ZERM_PERMIT_STATE_ACTIVE;
    }

    prepare_permit_modification(ph);

    if (current_state != new_state)
      pd.state.store(new_state, relaxed);
    if (current_concurrency != new_concurrency)
      pd.concurrency.store(new_concurrency, relaxed);
    pd.flags = req.flags;

    commit_permit_modification(ph);

    const bool is_satisfied = (int32_t)new_concurrency == req.max_sw_threads;
    return is_satisfied;
  }

  virtual void renegotiate_permits(zerm_permit_handle_t initiator) = 0;
  virtual void adjust_existing_permit(const zerm_permit_request_t& req, zerm_permit_handle_t permit) = 0;

  zerm_permit_handle_t make_new_permit(const zerm_client_id_t clid,
                                       const zerm_permit_request_t& req)
  {
    tracer t("ThreadComposabilityBase::make_new_permit_impl");

    zerm_permit_handle_t ph = (zerm_permit_rep_t*)malloc(sizeof(zerm_permit_rep_t));
    __TCM_ASSERT(ph, "Permit was not allocated.");

    ph->epoch = 0;
    zerm_permit_data_t* pd = &ph->data;

    pd->client_id = clid;
    pd->cpu_mask = nullptr;
    pd->concurrency.store(0, std::memory_order_relaxed);
    pd->state.store(ZERM_PERMIT_STATE_ACTIVE, std::memory_order_relaxed);
    pd->flags = req.flags;

    return ph;
  }

  void release_permit_impl(zerm_permit_handle_t ph) {
    tracer t("ThreadComposabilityBase::release_permit_impl");
    __TCM_ASSERT(ph, nullptr);

    bool additional_concurrency_available = false;

    zerm_permit_data_t& pd = ph->data;

    {
      const std::lock_guard<std::mutex> l(data_mutex);
      __TCM_ASSERT(is_valid(ph), "Invalid permit is being released.");
      permit_to_request_map.erase(ph);
      permit_to_callback_arg_map.erase(ph);

      auto client_phs = client_to_permit_mmap.equal_range(pd.client_id);
      for (auto it = client_phs.first; it != client_phs.second; ++it) {
        if (it->second == ph) {
          client_to_permit_mmap.erase(it);
          break;
        }
      }

      available_concurrency += pd.concurrency.load(std::memory_order_relaxed);
      if (available_concurrency > 0)
        additional_concurrency_available = true;
    }

    hwloc_bitmap_free(pd.cpu_mask);
    free(ph);

    if (additional_concurrency_available > 0) {
      tracer t("ThreadComposabilityBase:: going to renegotiate permits");
      renegotiate_permits(/*initiator*/nullptr);
    }
  }

  ze_result_t update_permit(zerm_permit_handle_t ph,
                            const zerm_permit_state_t curr_state,
                            const zerm_permit_state_t new_state)
  {
    // TODO: refactor this function to have uniform approach where applicable,
    // and split responsibilities otherwise.
    tracer t("ThreadComposabilityBase::update_flags");
    __TCM_ASSERT(is_valid(ph), "Updating non-existing permit.");
    __TCM_ASSERT(!is_void(curr_state), "Updating void permit.");
    __TCM_ASSERT(curr_state != new_state,
                "Setting permit state to be identical to current.");
    zerm_permit_data_t& pd = ph->data;
    if (is_void(new_state)) {
      release_permit_impl(ph);
    } else if (is_activating_from_inactive(curr_state, new_state)) {
      // Transition from inactive to active state requires resources reclaim
      zerm_permit_request_t request;
      {
        const std::lock_guard<std::mutex> l(data_mutex);
        request = permit_to_request_map[ph];

        prepare_permit_modification(ph);
        // Imitate new request
        pd.concurrency.store(0, std::memory_order_relaxed);
        commit_permit_modification(ph);
      }
      adjust_existing_permit(request, ph);
    } else if (is_deactivating(curr_state, new_state) ||
               is_idling_static_permit(pd.flags, curr_state, new_state))
    {
      // The transition to inactive state threads requires relinquishing of resources.
      // TODO: consider using adjust_existing_permit
      bool additional_concurrency_available = false;
      {
        const std::lock_guard<std::mutex> l(data_mutex);
        {
          prepare_permit_modification(ph);

          const std::memory_order relaxed = std::memory_order_relaxed;
          pd.state.store(new_state, relaxed);
          if (is_idling_static_permit(pd.flags, curr_state, new_state)) {
            // TODO: only allow renegotiation for static permits as their idling
            // does not necessarily mean relinguishing of its resources.

            // TODO: rename permit_to_request_map to something that represents
            // the semantics of the container more precisely.
            auto current_concurrency = pd.concurrency.load(relaxed);
            auto min_sw_threads = permit_to_request_map[ph].min_sw_threads;
            available_concurrency += current_concurrency - min_sw_threads;
            pd.concurrency.store(min_sw_threads, relaxed);
          } else {
            available_concurrency += pd.concurrency.load(relaxed);
          }

          commit_permit_modification(ph);
        }

        // TODO: consider setting p->max_concurrency to zero whenever resources
        // are given back to the Thread Composability Manager.

        // Do not nullify p->max_concurrency
        if (available_concurrency > 0)
          additional_concurrency_available = true;
      }
      if (additional_concurrency_available)
        renegotiate_permits(ph);
    } else {
      const std::lock_guard<std::mutex> l(data_mutex);
      prepare_permit_modification(ph);
      pd.state.store(new_state, std::memory_order_relaxed);
      commit_permit_modification(ph);
    }
    return ZE_RESULT_SUCCESS;
  }
}; // class ThreadComposabilityBase

class ThreadComposabilityFCFSCImpl : public ThreadComposabilityManagerBase {
public:
  void renegotiate_permits(zerm_permit_handle_t initiator) override {
    tracer t("ThreadComposabilityFCFSCImpl::renegotiate_permits");
    const std::memory_order relaxed = std::memory_order_relaxed;
    int32_t available_concurrency_snapshot;
    {
      const std::lock_guard<std::mutex> l(data_mutex);
      available_concurrency_snapshot = available_concurrency;
    }

    // TODO: consider comparing performance with other containers such as
    // std::deque.
    std::vector<zerm_permit_handle_t> nonrenegotiable_permits;

    // we contact enough clients that could absorb
    // available_concurrency_snapshot
    // Not sure this is the best approach
    while (available_concurrency_snapshot > 0) {
      zerm_permit_handle_t current_ph;
      zerm_permit_request_t req;
      zerm_callback_t callback;
      void* callback_arg = nullptr;
      uint32_t current_grant = 0;
      int32_t delta = 0;
      bool skip_callback = false;
      bool satisfied = false;
      {
        const std::lock_guard<std::mutex> l(data_mutex);
        if (renegotiation_deque.empty())
          break;
        current_ph = renegotiation_deque.front();
        renegotiation_deque.pop_front();

        if (!is_valid(current_ph))
          // permit is not valid anymore, skip it.
          continue;

        uint32_t dummy_concurrency_arg = platform_resources();
        if (skip_permit_renegotiation(current_ph, initiator, dummy_concurrency_arg)) {
          nonrenegotiable_permits.push_back(current_ph);
          continue;
        }

        req = permit_to_request_map[current_ph];
        zerm_permit_data_t& pd = current_ph->data;

        __TCM_ASSERT(
          1 == client_to_callback_map.count(pd.client_id),
          "The group has no corresponding callback."
        );

        current_grant = pd.concurrency.load(relaxed);
        delta = (int32_t)req.max_sw_threads - current_grant;
        if (delta <= 0)
          // There was a renegotiation for this permit for higher concurrency,
          // which has been already partially or fully satisfied. Therefore, the
          // previous request is not valid anymore.
          continue;


        callback = client_to_callback_map[pd.client_id];
        if (!callback)
          continue;

        __TCM_ASSERT(
          1 == permit_to_callback_arg_map.count(current_ph),
          "The permit has no argument for the client callback."
        );
        callback_arg = permit_to_callback_arg_map[current_ph];

        // Borrow additional concurrency beforehand so when renegotiation
        // callback finishes successfully the Thread Composability Manager still
        // can assign it to current permit. New permit requests that were
        // unsatisfied during the invocation of the callback function are placed
        // into renegotiation deque and will be negotiated later if there is
        // available concurrency for them.
        satisfied = try_satisfy_request(req, current_ph);

        if (!satisfied) {
          // Push the request back to the renegotiation queue if it's still not
          // fully satisfied or there is no concurrency available.
          // TODO: Also, if we have an unsatisfied pending permit, we can return it
          // to the beginning of the queue until it be satisfied.
          // So, this approach may increase the amount of active permits in some cases.
          // It needs to be investigated.
          renegotiation_deque.push_back(current_ph);
        }

        delta = pd.concurrency.load(relaxed) - current_grant;
        // TODO: consider asserting delta is strictly more than zero.
        __TCM_ASSERT(delta >= 0, "Invalid invariants.");

        skip_callback = skip_callback_invocation(current_ph, initiator, current_grant);
        available_concurrency_snapshot = available_concurrency;
      }

      if (skip_callback)
        continue;

      // TODO: align the logic upon callback response with the other RM
      // strategies
      zerm_callback_flags_t callback_reason{};
      callback_reason.new_concurrency = true;
      const ze_result_t cbr = callback(current_ph, callback_arg, callback_reason);
      if (cbr != ZE_RESULT_SUCCESS) {
        const std::lock_guard<std::mutex> l(data_mutex);
        if (is_valid(current_ph)) {
          prepare_permit_modification(current_ph);
          current_ph->data.concurrency.store(current_grant, relaxed);
          commit_permit_modification(current_ph);
        }

        // For some reason the client did not consume additional
        // concurrency. Return the borrowed concurrency back.
        available_concurrency += delta;
        available_concurrency_snapshot = available_concurrency;
      }
    } // end of while resources exist

    // Return non-renegotiable permits back
    const std::lock_guard<std::mutex> l(data_mutex);
    renegotiation_deque.insert(
      renegotiation_deque.end(),
      nonrenegotiable_permits.begin(), nonrenegotiable_permits.end()
    );
  }

  void adjust_existing_permit(const zerm_permit_request_t &req, zerm_permit_handle_t ph) override {
    tracer t("ThreadComposabilityFCFSCImpl::adjust_existing_permit");
    bool additional_concurrency_available = false;
    bool satisfied_client = true;

    {
      const std::lock_guard<std::mutex> l(data_mutex);
      __TCM_ASSERT(is_valid(ph), "Invalid permit.");
      __TCM_ASSERT(req == permit_to_request_map[ph], "Inconsistent request state.");

      satisfied_client = try_satisfy_request(req, ph);

      if (available_concurrency > 0)
        additional_concurrency_available = true;
    }

    if (!satisfied_client) {
      tracer t("ThreadComposabilityFCFSCImpl::NOTE p is an unsatisfied permit");
      const std::lock_guard<std::mutex> l(data_mutex);
      renegotiation_deque.push_back(ph);
    }

    if (additional_concurrency_available) {
      tracer t("ThreadComposabilityFCFSCImpl::NOTE going to renegotiate permits");
      renegotiate_permits(ph);
    }
  }
private:
  std::deque<zerm_permit_handle_t> renegotiation_deque;
};

class ThreadComposabilityFairBalance : public ThreadComposabilityManagerBase {
  // TODO: Consider more efficient data structure
  // std::vector<zerm_permit_request_t> my_active_requests;

  // Stores the keys in the map of existing permits.
  std::deque<zerm_permit_handle_t> renegotiation_deque;

protected:
  //! Tries to satisfy minimum concurrency level to switch permit to the ACTIVE state.
  bool try_satisfy_required_demand(const zerm_permit_request_t& pr, zerm_permit_handle_t ph) {
    tracer t("ThreadComposabilityFairBalance::try_satisfy_required_demand");

    zerm_permit_data_t& pd = ph->data;

    const uint32_t demand = std::min((int32_t)available_concurrency, pr.min_sw_threads);
    const bool is_activation = demand == pr.min_sw_threads;

    if (is_activation) {
      prepare_permit_modification(ph);

      pd.concurrency.store(demand, std::memory_order_relaxed);
      pd.state.store(ZERM_PERMIT_STATE_ACTIVE, std::memory_order_relaxed);

      commit_permit_modification(ph);

      available_concurrency -= demand;
    }

    return is_activation;
  }

  //! Calculate concurrency for ACTIVE/IDLE permits in accordance with the FAIR approach.
  //! If necessary, callback should be called separately.
  void renegotiate_active_idle_permit(uint32_t distributed_concurrency, uint32_t total_demand,
                                      const zerm_permit_request_t& pr, zerm_permit_handle_t ph) {
    zerm_permit_data_t& pd = ph->data;

    uint32_t allotted = 0;
    const uint32_t demand = pr.max_sw_threads - pr.min_sw_threads;
    distributed_concurrency = std::min(distributed_concurrency, total_demand);

    if (available_concurrency > 0 && total_demand > 0) {
      const uint32_t numerator = distributed_concurrency * demand;
      allotted = numerator / total_demand;
      const uint32_t remainder = numerator % total_demand;
      if (remainder >= total_demand - remainder)
        allotted += 1;
      if (allotted > available_concurrency)
        allotted = available_concurrency;
    }

    available_concurrency -= allotted;
    const uint32_t new_granted = pr.min_sw_threads + allotted;

    prepare_permit_modification(ph);
    pd.concurrency.store(new_granted, std::memory_order_relaxed);
    commit_permit_modification(ph);
  }

  void renegotiate_permits(zerm_permit_handle_t initiator) override {
    tracer t("ThreadComposabilityFairBalance::renegotiate_permits");
    {
      uint32_t total_granted = 0;
      uint32_t active_demand = 0;

      static std::map<uint32_t, zerm_permit_handle_t> renegotiation_pending;
      static std::vector<std::pair<int32_t, zerm_permit_handle_t>> renegotiation_active_idle;
      renegotiation_active_idle.reserve(1024);

      {
        const std::lock_guard<std::mutex> l(data_mutex);

        // distribute whole platform concurrency
        available_concurrency = platform_resources();

        // Searching of ACTIVE without STATIC flag/IDLE/PENDING permits
        for (auto& elem: permit_to_request_map) {
          zerm_permit_handle_t ph = elem.first;
          if (skip_permit_renegotiation(ph, initiator, available_concurrency))
            continue;

          zerm_permit_data_t& pd = ph->data;
          zerm_permit_request_t& pr = elem.second;

          if (is_pending(pd.state.load(std::memory_order_relaxed)))
            renegotiation_pending.insert(std::make_pair(pr.min_sw_threads, ph));
          else {
            active_demand += pr.max_sw_threads - pr.min_sw_threads;
            total_granted += pr.min_sw_threads;
            renegotiation_active_idle.push_back(
              std::make_pair((int32_t)pd.concurrency.load(std::memory_order_relaxed), ph));
          }
        }

        __TCM_ASSERT(available_concurrency >= total_granted, "Underflow detected");
        available_concurrency -= total_granted;
        // Resource redistribution is performed if we have available resources.
        if (available_concurrency != 0) {
          // Processing of the PENDING permits
          for (auto& elem : renegotiation_pending) {
            zerm_permit_request_t pr  = permit_to_request_map[elem.second];

            bool is_activation = try_satisfy_required_demand(pr, elem.second);
            if (!is_activation)
              // No need processing other pending permits left in the map
              // since it is sorted from lowest to highest requests' requirements.
              break;

            renegotiation_active_idle.push_back(std::make_pair(0, elem.second));
            uint32_t new_demand = pr.max_sw_threads - pr.min_sw_threads;
            if (new_demand > 0)
              active_demand += new_demand;
          }

          // Processing of active and idle permits
          uint32_t available_concurrency_snapshot = available_concurrency;
          for (auto& elem: renegotiation_active_idle) {
            int32_t prev_granted = elem.first;
            zerm_permit_handle_t ph = elem.second;
            zerm_permit_request_t& pr = permit_to_request_map[ph];

            renegotiate_active_idle_permit(available_concurrency_snapshot, active_demand, pr, ph);

            if (skip_callback_invocation(ph, initiator, prev_granted))
              continue;

            zerm_callback_t callback = client_to_callback_map[ph->data.client_id];
            __TCM_ASSERT(callback, "No callback registered for the group.");
            void* arg = permit_to_callback_arg_map[ph];

            zerm_callback_flags_t callback_reason{};
            callback_reason.new_concurrency = true;
            const ze_result_t cbr = callback(ph, arg, callback_reason);
            __TCM_ASSERT(cbr == ZE_RESULT_SUCCESS, "Callback failed.");
          }
        }
      }

      renegotiation_pending.clear();
      renegotiation_active_idle.clear();
    }
  }

  //! TODO: Why do we have to pass req here? Can use the request from permit_to_request_map inside?
  void adjust_existing_permit(const zerm_permit_request_t &req, zerm_permit_handle_t ph) override {
    tracer t("ThreadComposabilityFairBalance::adjust_existing_permit");
    bool satisfied_client = true;

    {
      const std::lock_guard<std::mutex> l(data_mutex);
      __TCM_ASSERT(is_valid(ph), "Invalid permit.");
      __TCM_ASSERT(req == permit_to_request_map[ph], "Inconsistent request state.");

      satisfied_client = try_satisfy_request(req, ph);
    }

    if (!satisfied_client) {
      tracer t("ThreadComposabilityFairBalance::NOTE p is an unsatisfied permit, renegotiating.");
      renegotiate_permits(ph);
    }
  }
}; // ThreadComposabilityFairBalance
} // namespace internal

class ThreadComposabilityManager {
  std::unique_ptr<internal::ThreadComposabilityManagerBase> impl_;
public:
  ThreadComposabilityManager() {
    std::string tcm_strategy = "FAIR"; // default

    char* rm_strategy_env_value = std::getenv("RM_STRATEGY");
    if (rm_strategy_env_value)
      tcm_strategy = rm_strategy_env_value;

    if (tcm_strategy == "FCFS") {
      impl_.reset(new internal::ThreadComposabilityFCFSCImpl);
    } else if (tcm_strategy == "FAIR") {
      impl_.reset(new internal::ThreadComposabilityFairBalance);
    } else {
      __TCM_ASSERT(false, "Incorrect value of RM_STRATEGY environment variable.");
    }
  }

  zerm_client_id_t register_client(zerm_callback_t r) {
    internal::tracer t("ThreadComposability::register_client");
    return impl_->register_client(r);
  }

  void unregister_client(zerm_client_id_t clid) {
    internal::tracer t("ThreadComposability::unregister_client");
    impl_->unregister_client(clid);
  }

  zerm_permit_handle_t request_permit(zerm_client_id_t clid, zerm_permit_request_t& req,
                                    void* callback_arg, zerm_permit_handle_t permit_handle,
                                    zerm_permit_t* permit) {
    internal::tracer t("ThreadComposability::request_permit");
    return impl_->request_permit(clid, req, callback_arg, permit_handle, permit);
  }

  ze_result_t get_permit(zerm_permit_handle_t ph, zerm_permit_t* p) {
    internal::tracer t("ThreadComposability::get_permit");
    return impl_->get_permit(ph, p);
  }

  ze_result_t idle_permit(zerm_permit_handle_t p) {
    internal::tracer t("ThreadComposability::idle_permit");
    return impl_->idle_permit(p);
  }

  ze_result_t activate_permit(zerm_permit_handle_t p) {
    internal::tracer t("ThreadComposability::activate_permit");
    return impl_->activate_permit(p);
  }

  ze_result_t deactivate_permit(zerm_permit_handle_t p) {
    internal::tracer t("ThreadComposability::deactivate_permit");
    return impl_->deactivate_permit(p);
  }

  ze_result_t release_permit(zerm_permit_handle_t p) {
    internal::tracer t("ThreadComposability::release_permit");
    return impl_->release_permit(p);
  }

  ze_result_t register_thread(zerm_permit_handle_t p) {
    internal::tracer t("ThreadComposability::register_thread");
    return impl_->register_thread(p);
  }

  ze_result_t unregister_thread() {
    internal::tracer t("ThreadComposability::register_thread");
    return impl_->unregister_thread();
  }

};

class theTCM {
  static ThreadComposabilityManager* tcm_ptr;
  static std::size_t reference_count;
  static std::mutex tcm_mutex;
public:
  static void increase_ref_count() {
    std::lock_guard<std::mutex> l(tcm_mutex);
    if (reference_count++)
      return;
    tcm_ptr = new ThreadComposabilityManager;
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
    }
    delete rm_instance_to_delete;
  }
};

ThreadComposabilityManager* theTCM::tcm_ptr{nullptr};
std::size_t theTCM::reference_count{0};
std::mutex theTCM::tcm_mutex{};

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
///     - ::ZE_RESULT_SUCCESS
///     - ::ZE_RESULT_ERROR_UNKNOWN
ze_result_t zermConnect(zerm_callback_t callback, zerm_client_id_t *client_id)
{
  using tcm::theTCM;
  tcm::internal::tracer t("zermConnect");

  if (client_id) {
    theTCM::increase_ref_count();
    auto& mgr = theTCM::instance();
      if ((*client_id = mgr.register_client(callback)))
        return ZE_RESULT_SUCCESS;
  }
  return ZE_RESULT_ERROR_UNKNOWN;
}

/// @brief Terminate the connection with the thread composability manager
///
/// @details
///     - Must be called whenever the client, which is seen as a set of permits associated
///       with the given client_id, finishes its work with the thread composability manager
///       and no other calls, possibly except for zermConnect are expected to be made
///       from that client.
///
/// @returns
///     - ::ZE_RESULT_SUCCESS
///     - ::ZE_RESULT_ERROR_UNKNOWN
ze_result_t zermDisconnect(zerm_client_id_t client_id)
{
  using tcm::theTCM;
  tcm::internal::tracer t("zermDisconnect");

  auto& mgr = theTCM::instance();
  mgr.unregister_client(client_id);
  theTCM::decrease_ref_count();

  return ZE_RESULT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Request a new permit
///
/// @details
///     - The client must call this function to request the permit.
///
/// @returns
///     - ::ZE_RESULT_SUCCESS
///     - ::ZE_RESULT_ERROR_UNKNOWN
ze_result_t zermRequestPermit(zerm_client_id_t client_id,
                              zerm_permit_request_t request,
                              void* callback_arg,
                              zerm_permit_handle_t *permit_handle,
                              zerm_permit_t* permit)
{
  using tcm::theTCM;
  tcm::internal::tracer t("zermRequestPermit");

  if (!permit_handle)
    return ZE_RESULT_ERROR_UNKNOWN;

  if (request.min_sw_threads < 0 || request.max_sw_threads < request.min_sw_threads)
    return ZE_RESULT_ERROR_UNKNOWN;

  auto& mgr = theTCM::instance();
  zerm_permit_handle_t new_ph = mgr.request_permit(client_id, request, callback_arg,
                                                 *permit_handle, permit); 

  if (!new_ph)
    return ZE_RESULT_ERROR_INVALID_ARGUMENT;

  *permit_handle = new_ph;

  return ZE_RESULT_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Writes the current permit data into passed argument.
///
/// @details
///     - The client calls this function whenever it wants to read the permit.
///       In paricular, after zermIdlePermit and zermActivatePermit calls, and
///       during invocation of the client's callback.
///
/// @returns
///     - ::ZE_RESULT_SUCCESS
///     - ::ZE_RESULT_ERROR_UNKNOWN
ze_result_t zermGetPermitData(zerm_permit_handle_t permit_handle, zerm_permit_t* permit) {
  using tcm::theTCM;
  tcm::internal::tracer t("zermGetPermitData");

  if (!permit_handle || !permit)
    return ZE_RESULT_ERROR_UNKNOWN;

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
///     - ::ZE_RESULT_SUCCESS
///     - ::ZE_RESULT_ERROR_UNKNOWN
ze_result_t zermIdlePermit(zerm_permit_handle_t p) {
  using tcm::theTCM;
  tcm::internal::tracer t("zermIdlePermit");

  auto& mgr = theTCM::instance();
  if (p) {
    return mgr.idle_permit(p);
  }
  return ZE_RESULT_ERROR_UNKNOWN;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Activates a permit
///
/// @details
///     - The client must call this function to activate the permit.
///
/// @returns
///     - ::ZE_RESULT_SUCCESS
///     - ::ZE_RESULT_ERROR_UNKNOWN
ze_result_t zermActivatePermit(zerm_permit_handle_t p) {
  using tcm::theTCM;
  tcm::internal::tracer t("zeReactivatePermit");

  auto& mgr = theTCM::instance();
  if (p) {
    return mgr.activate_permit(p);
  }
  return ZE_RESULT_ERROR_UNKNOWN;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Deactivates a permit
///
/// @details
///     - The client must call this function to deactivate the permit.
///
/// @returns
///     - ::ZE_RESULT_SUCCESS
///     - ::ZE_RESULT_ERROR_UNKNOWN
ze_result_t zermDeactivatePermit(zerm_permit_handle_t p) {
  using tcm::theTCM;
  tcm::internal::tracer t("zeReactivatePermit");

  auto& mgr = theTCM::instance();
  if (p) {
    return mgr.deactivate_permit(p);
  }
  return ZE_RESULT_ERROR_UNKNOWN;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Releases a permit
///
/// @details
///     - The client must call this function to release the permit.
///
/// @returns
///     - ::ZE_RESULT_SUCCESS
///     - ::ZE_RESULT_ERROR_UNKNOWN
ze_result_t zermReleasePermit(zerm_permit_handle_t p) {
  using tcm::theTCM;
  tcm::internal::tracer t("zermReleasePermit");

  auto& mgr = theTCM::instance();
  if (p) {
    return mgr.release_permit(p);
  }
  return ZE_RESULT_ERROR_UNKNOWN;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Registers a thread
///
/// @details
///     - The client must call this function when the thread allocated as part of the permit starts the work.
///
/// @returns
///     - ::ZE_RESULT_SUCCESS
///     - ::ZE_RESULT_ERROR_UNKNOWN
ze_result_t zermRegisterThread(zerm_permit_handle_t p) {
  using tcm::theTCM;

  auto& mgr = theTCM::instance();
  if (p) {
    return mgr.register_thread(p);
  }
  return ZE_RESULT_ERROR_UNKNOWN;
}


///////////////////////////////////////////////////////////////////////////////
/// @brief Unregisters a thread
///
/// @details
///     - The client must call this function when the thread allocated as part of the permit ends the work.
///
/// @returns
///     - ::ZE_RESULT_SUCCESS
///     - ::ZE_RESULT_ERROR_UNKNOWN
ze_result_t zermUnregisterThread() {
  using tcm::theTCM;

  auto& mgr = theTCM::instance();
  return mgr.unregister_thread();
}

}
