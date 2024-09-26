/*
    Copyright (C) 2023-2024 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#ifndef __TCM_TESTS_TEST_UTILS_HEADER
#define __TCM_TESTS_TEST_UTILS_HEADER

#include "basic_test_utils.h"

#include "test_exceptions.h"

#include "hwloc_test_utils.h"
#include "../src/utils.h"
#include "tcm/detail/_tcm_assert.h"
#include "tcm.h"

#include <cstdio>
#include <vector>
#include <set>
#include <string>
#include <initializer_list>
#include <utility>

/***************************************************************************************************
 * Helpers for work with CPU masks
 **************************************************************************************************/
using tcm_const_cpu_mask_t = hwloc_const_bitmap_t; // TODO: Consider introducing into TCM API

inline std::string to_string(const tcm_const_cpu_mask_t mask) {
  const unsigned max_size = 1024;
  char buf[max_size] = {0};
  __TCM_ASSERT(mask, "CPU mask should not be nullptr");
  hwloc_bitmap_snprintf(buf, max_size, mask);
  return std::string(buf);
}

inline tcm_cpu_mask_t allocate_cpu_mask() { return hwloc_bitmap_alloc(); }
inline void free_cpu_mask(tcm_cpu_mask_t mask) {
  __TCM_ASSERT(mask, "CPU mask should not be nullptr");
  hwloc_bitmap_free(mask);
}

struct mask_deleter {
  void operator()(tcm_cpu_mask_t* mask) const { free_cpu_mask(*mask); }
};

struct masks_guard_t {
  masks_guard_t(uint32_t size) : m_size(size) {}
  void operator()(tcm_cpu_mask_t* cpu_masks) const {
    __TCM_ASSERT(cpu_masks, "Array of CPU masks cannot be nullptr");
    for (uint32_t i = 0; i < m_size; ++i) {
      free_cpu_mask(cpu_masks[i]);
    }
    delete [] cpu_masks;
  }
private:
  const uint32_t m_size;
};

inline bool is_equal(tcm_const_cpu_mask_t mask_1, tcm_const_cpu_mask_t mask_2) {
  __TCM_ASSERT(mask_1, "CPU mask should not be nullptr");
  __TCM_ASSERT(mask_2, "CPU mask should not be nullptr");
  return hwloc_bitmap_compare(mask_1, mask_2) == 0;
}

inline bool is_intersect(tcm_const_cpu_mask_t mask_1, tcm_const_cpu_mask_t mask_2) {
  __TCM_ASSERT(mask_1, "CPU mask should not be nullptr");
  __TCM_ASSERT(mask_2, "CPU mask should not be nullptr");
  return hwloc_bitmap_intersects(mask_1, mask_2);
}

inline void copy(tcm_cpu_mask_t dst, tcm_const_cpu_mask_t src) {
  __TCM_ASSERT(dst, "Destination CPU mask should not be nullptr");
  __TCM_ASSERT(src, "Source CPU mask should not be nullptr");
  int result = hwloc_bitmap_copy(dst, src);
  __TCM_ASSERT_EX(0 == result, "Unable to copy the CPU mask");
}

inline int32_t hardware_concurrency(tcm_const_cpu_mask_t mask) {
  __TCM_ASSERT(mask, "CPU mask should not be nullptr");
  return int32_t(hwloc_bitmap_weight(mask));
}

constexpr float tcm_oversubscription_factor = 1.0f;

inline int32_t tcm_concurrency(tcm_const_cpu_mask_t mask) {
  __TCM_ASSERT(mask, "CPU mask should not be nullptr");
  return int32_t(tcm_oversubscription_factor * hardware_concurrency(mask));
}

inline tcm_const_cpu_mask_t process_affinity_mask = []() {
    tcm_test::system_topology::construct();
    auto& tp = tcm_test::system_topology::instance();
    return tp.process_affinity_mask();
}();

//! Returns available concurrency on the platform, taking into account the mask of the process.
inline int32_t platform_hardware_concurrency() {
  static int32_t c = hardware_concurrency(process_affinity_mask);
  return c;
}

//! Returns available concurrency on the platform, taking into account the degree of
//! oversubscription (oversubscription_factor must be greater than zero) and process mask.
inline int32_t platform_tcm_concurrency() {
  static int32_t c = int32_t(tcm_oversubscription_factor * platform_hardware_concurrency());
  return c;
}

inline void extract_first_n_bits_from_process_affinity_mask(tcm_cpu_mask_t result, int n_threads,
                                                            tcm_const_cpu_mask_t hint = nullptr)
{
  __TCM_ASSERT(result, "Result CPU mask should be pre-allocated on the caller side");
  __TCM_ASSERT(hwloc_bitmap_iszero(result), "Result CPU mask should be empty");

  int start_id = -1;
  if (hint)
    start_id = hwloc_bitmap_last(hint);
  start_id = hwloc_bitmap_next(process_affinity_mask, start_id);

  for (int idx = start_id; idx != -1 && n_threads-- > 0;
       idx = hwloc_bitmap_next(process_affinity_mask, idx))
  {
    hwloc_bitmap_set(result, idx);
  }
}


/***************************************************************************************************
 * Helpers for testing TCM permits properties
 **************************************************************************************************/

inline bool has_masks(tcm_permit_t const& p) {
    if (!p.size)
        return false;

    for (unsigned i = 0; i < p.size; ++i) {
        if (!p.cpu_masks[i])
            return false;

        auto c = hardware_concurrency(p.cpu_masks[i]);
        if (!c) {
            return false;
        }
    }
    return true;
}


inline bool check_permit_size(const tcm_permit_t& expected, const tcm_permit_t& actual,
                              const unsigned num_indents = 0, const bool report = true)
{
  const auto& a = actual.size;
  const auto& e = expected.size;
  const bool result = (a == e);

  std::string report_str = "Check size of arrays inside permit, expected " + std::to_string(e) +
      " equals to actual " + std::to_string(a);

  return report ? check(result, report_str, num_indents) : result;
}


inline bool check_permit_concurrency(const tcm_permit_t& expected, const tcm_permit_t& actual,
                                     const unsigned num_indents = 0, const bool report = true)
{
  bool result = false;
  std::string report_str{};

  auto const& expected_concurrencies = to_string(expected.concurrencies, expected.size);
  auto const& actual_concurrencies = to_string(actual.concurrencies, actual.size);

  report_str = "Check concurrencies, expected " + expected_concurrencies + " equals to actual "
               + actual_concurrencies;

  for (unsigned i = 0; i < expected.size; ++i) {
    const auto& e = expected.concurrencies[i]; const auto& a = actual.concurrencies[i];
    result = (e == a);
    if (!result)
      break;
  }

  return report ? check(result, report_str, num_indents) : result;
}

inline uint32_t get_permit_concurrency(const tcm_permit_t& permit) {
  uint32_t total_grant = 0;
  for (unsigned i = 0; i < permit.size; ++i) {
    const auto& e = permit.concurrencies[i];
    total_grant += e;
  }

  return total_grant;
}

inline bool check_permit_mask(const tcm_permit_t& expected, const tcm_permit_t& actual,
                              const unsigned num_indents = 0, const bool report = true)
{
  std::string report_str{};
  // check the cpu_masks pointers
  if (expected.cpu_masks == nullptr && actual.cpu_masks == nullptr) {
    return check(true, "Both masks are null pointers", num_indents);
  } else if (expected.cpu_masks == nullptr || actual.cpu_masks == nullptr) {
    report_str = "Check CPU mask, expected cpu_masks pointer '" +
        std::to_string(uintptr_t(expected.cpu_masks)) + "' equals to actual cpu_masks pointer '" +
        std::to_string(uintptr_t(actual.cpu_masks)) + "'";
    return report ? check(false, report_str, num_indents) : false;
  }

  bool result = true;
  for (unsigned i = 0; i < expected.size; ++i) {
    // check the cpu_masks values
    const auto& e = expected.cpu_masks[i]; const auto& a = actual.cpu_masks[i];

    if (e && a) {
      result &= is_equal(e, a);
      report_str = "Check CPU mask, expected '" + to_string(e) +
        "' equals to actual '" + to_string(a) + "', mask index " + std::to_string(i);
    } else {
      result &= (e == nullptr && a == nullptr);
      report_str = "Check CPU mask is not nullptr: actual '" +  to_string(a) +
        "', index " + std::to_string(i);
    }

    if (report) {
        result &= check(result, report_str, num_indents);
    }
  }

  return result;
}

inline bool check_permit_state(const tcm_permit_t& expected, const tcm_permit_t& actual,
                               const unsigned num_indents = 0, const bool report = true)
{
  const auto& e = expected.state; const auto& a = actual.state;
  const bool result = (e == a);
  std::string report_str = "Check state, expected " + to_string(e) + " (" + std::to_string(e) +
                           ") equals to actual " + to_string(a) + " (" + std::to_string(a) + ")";

  return report ? check(result, report_str, num_indents) : result;
}

inline bool check_permit_flags(const tcm_permit_t& expected, const tcm_permit_t& actual,
                               const unsigned num_indents = 0, const bool report = true)
{
  const auto& e = expected.flags; const auto& a = actual.flags;
  bool result = true;
  result &= e.stale == a.stale;
  result &= e.rigid_concurrency == a.rigid_concurrency;
  result &= e.exclusive == a.exclusive;
  result &= e.request_as_inactive == a.request_as_inactive;

  std::string report_str("Check flags, expected: " + to_string(e) +
                         " equals to actual: " + to_string(a));

  return report ? check(result, report_str, num_indents) : result;
}

struct skip_checks_t {
  bool size = false;
  bool concurrency = false;
  bool state = false;
  bool flags = false;
  bool mask = false;
};

inline skip_checks_t operator|(const skip_checks_t& lhs, const skip_checks_t& rhs) {
  return {
    lhs.size || rhs.size,
    lhs.concurrency || rhs.concurrency,
    lhs.state || rhs.state,
    lhs.flags || rhs.flags,
    lhs.mask || rhs.mask
  };
}

// Compares two permits' data. Returns true if the data is equal, false -
// otherwise. Function allows skipping check of specific permit data fields.
inline bool check_permit(const tcm_permit_t& expected, const tcm_permit_t& actual,
                         const skip_checks_t skip = {}, unsigned num_indents = 1,
                         const bool report = true)
{
  bool result = true;
  result &= skip.size         || check_permit_size(expected, actual, num_indents, report);
  result &= skip.concurrency  || check_permit_concurrency(expected, actual, num_indents, report);
  result &= skip.mask         || check_permit_mask(expected, actual, num_indents, report);
  result &= skip.state        || check_permit_state(expected, actual, num_indents, report);
  result &= skip.flags        || check_permit_flags(expected, actual, num_indents, report);
  return result;
}

//! Checks the expected permit data with the data obtained by reading passed
//! permit handle. Returns true if the data is equal, and false - otherwise.
inline bool check_permit(const tcm_permit_t& expected, tcm_permit_handle_t ph,
                         const skip_checks_t skip = {}, const unsigned num_indents = 1,
                         const bool report = true)
{
    constexpr unsigned str_size = 128; char msg[str_size]{'\0'};
    std::snprintf(msg, str_size, "check permit_handle=%p is not nullptr", (void*)ph);
    if (!check(ph, msg, num_indents))
      return false;

  __TCM_ASSERT(expected.size > 0, "Permit size cannot be zero.");
  std::vector<uint32_t> concurrencies(expected.size, 0);
  std::unique_ptr<tcm_cpu_mask_t[], masks_guard_t> cpu_masks(nullptr, masks_guard_t(expected.size));
  if (expected.cpu_masks) {
    cpu_masks.reset(new tcm_cpu_mask_t[expected.size]);
    for (uint32_t i = 0; i < expected.size; ++i) {
      cpu_masks[i] = allocate_cpu_mask();
    }
  }

  tcm_permit_t actual = make_void_permit(concurrencies.data(), cpu_masks.get(), expected.size);
  tcm_result_t reading_result = tcmGetPermitData(ph, &actual);
  if (!check_success(reading_result)) {
      return check(false, "Reading data from ph=" + to_string(ph), num_indents,
                   "tcmGetPermitData() returns status " + std::to_string(reading_result));
  }

  bool result = check_permit(expected, actual, skip, num_indents, report);
  return result;
}

typedef std::pair<const tcm_permit_t&, const tcm_permit_t&> tcm_permits_pair_t;

inline bool check_permits(std::initializer_list<tcm_permits_pair_t> expected_and_actual_permit_pairs,
                          const unsigned num_indents = 0, const skip_checks_t skip = {},
                          const bool report = true)
{
    bool result = true;
    for (const auto& pair : expected_and_actual_permit_pairs) {
        const auto& expected = pair.first; const auto& actual = pair.second;
        result &= check_permit(expected, actual, skip, num_indents, report);
    }
    return result;
}

typedef std::pair<const tcm_permit_t&, tcm_permit_handle_t&> tcm_permit_and_handle_pair_t;

inline bool check_permits(
  std::initializer_list<tcm_permit_and_handle_pair_t> expected_and_actual_permit_pairs,
  const unsigned num_indents = 1, const skip_checks_t skip = {}, const bool report = true)
{
    bool result = true;
    for (const auto& pair : expected_and_actual_permit_pairs) {
        const auto& expected = pair.first; auto& permit_handle = pair.second;
        result &= check_permit(expected, permit_handle, skip, num_indents, report);
    }
    return result;
}


typedef std::vector< std::pair<tcm_permit_handle_t, tcm_permit_t*> > permits_data_t;

/**
 * \brief Returns a set of permit handles, whose data is equal to corresponding
 * (expected) permit data passed as a sequence of pairs.
 *
 * Thus, if existing permits are passed as a parameter, the function returns
 * permit handles, for which the renegotiation should not have taken place.
 */
inline std::set<tcm_permit_handle_t> list_unchanged_permits(const permits_data_t& pds,
                                                            const unsigned num_indents = 0)
{
  std::set<tcm_permit_handle_t> result;

  for (auto& pd : pds) {
    const tcm_permit_handle_t ph = pd.first;
    const tcm_permit_t& expected = *pd.second;

    if (check_permit(expected, ph, skip_checks_t{}, num_indents, /*report*/false))
      result.insert(ph);
  }

  return result;
}


/*
 * Test helpers to simplify regular work with the TCM.
 */
// TODO: Make use of these helpers in all the tests, utilizing RAII for releasing permits, closing
// connections that remain after exception occurs.
inline tcm_client_id_t connect_new_client(tcm_callback_t callback = nullptr,
                                          const std::string& error_message = "",
                                          const std::string& log_message = "tcmConnect")
{
  tcm_client_id_t client_id;

  tcm_result_t r = tcmConnect(callback, &client_id);
  if (!check_success(r, log_message))
    throw tcm_connect_error(error_message.c_str());

  return client_id;
}

inline void disconnect_client(const tcm_client_id_t& client_id,
                              const std::string& error_message = "",
                              const std::string& log_message = "tcmDisconnect")
{
  tcm_result_t r = tcmDisconnect(client_id);
  if (!check_success(r, log_message))
    throw tcm_disconnect_error(error_message.c_str());
}

inline tcm_permit_handle_t
request_permit(tcm_client_id_t client, const tcm_permit_request_t& req, void* callback_arg = nullptr,
               tcm_permit_handle_t permit_handle = nullptr, const std::string& error_message = "",
               const std::string& log_message = "")
{
  auto r = tcmRequestPermit(client, req, callback_arg, &permit_handle, /*permit*/nullptr);

  std::string actual_log_message = log_message;
  if ("" == log_message) {
      const std::string num_resources_msg = "[" + std::to_string(req.min_sw_threads) + ", " +
                                            std::to_string(req.max_sw_threads) + "]";
      std::string ph_type_msg = "new";
      if (permit_handle) {
          ph_type_msg = "existing";
      }
      actual_log_message = std::string("tcmRequestPermit on ") + ph_type_msg + " permit_handle for "
                           + num_resources_msg + " resources";
  }

  if (!check_success(r, actual_log_message)) {
    throw tcm_request_permit_error(error_message);
  }

  return permit_handle;
}

inline void activate_permit(tcm_permit_handle_t permit_handle,
                            const std::string& error_message = "",
                            const std::string& log_message = "tcmActivatePermit")
{
  auto r = tcmActivatePermit(permit_handle);
  if (!check_success(r, log_message)) {
    throw tcm_activate_permit_error(error_message);
  }
}

inline void deactivate_permit(tcm_permit_handle_t permit_handle,
                              const std::string& error_message = "",
                              const std::string& log_message = "tcmDeactivatePermit")
{
  auto r = tcmDeactivatePermit(permit_handle);
  if (!check_success(r, log_message)) {
    throw tcm_deactivate_permit_error(error_message);
  }
}

inline void idle_permit(tcm_permit_handle_t permit_handle, const std::string& error_message = "")
{
  auto r = tcmIdlePermit(permit_handle);
  if (!check_success(r, "tcmIdlePermit")) {
    throw tcm_idle_permit_error(error_message);
  }
}

inline void release_permit(tcm_permit_handle_t ph, const std::string& error_message = "",
                           const std::string& log_message = "tcmReleasePermit")
{
  auto r = tcmReleasePermit(ph);

  if (!check_success(r, log_message)) {
    throw tcm_release_permit_error(error_message);
  }
}

inline void register_thread(tcm_permit_handle_t ph, const std::string& error_message = "",
                            const std::string& log_message = "tcmRegisterThread")
{
  auto r = tcmRegisterThread(ph);

  if (!check_success(r, log_message)) {
    throw tcm_register_thread_error(error_message);
  }
}

inline void unregister_thread(const std::string& error_message = "",
                              const std::string& log_message = "tcmUnregisterThread")
{
  auto r = tcmUnregisterThread();

  if (!check_success(r, log_message)) {
    throw tcm_unregister_thread_error(error_message);
  }
}

template <int size = 1>
class permit_t {
public:
    permit_t(bool allocate_mask = false)
        : concurrencies(new uint32_t[size]{0}),
        // TODO: Fix the bug with copying over all the array elements received bitmap from the
        // HWLOC, while the intention is to allocate separate mask for each element of cpu_masks
        // array
          cpu_masks(allocate_mask? new tcm_cpu_mask_t[size]{allocate_cpu_mask()} : nullptr,
                    masks_guard_t(size)),
          permit{
              concurrencies.get(), cpu_masks.get(), size, TCM_PERMIT_STATE_VOID,
              tcm_permit_flags_t{}
          }
    {
      static_assert(size == 1, "Unsupported code due to wrong initialization of cpu_masks array");
    }
    size_t concurrency() {
      return get_permit_concurrency(permit);
    }
    operator tcm_permit_t&() { return permit; }
private:
    std::unique_ptr<uint32_t[]> concurrencies;
    std::unique_ptr<tcm_cpu_mask_t[], masks_guard_t> cpu_masks;
    tcm_permit_t permit;
};

inline void get_permit_data(tcm_permit_handle_t ph, tcm_permit_t& permit,
                            const std::string& error_message = "",
                            const std::string& log_message = "tcmGetPermitData")
{
  auto r = tcmGetPermitData(ph, &permit);
  if (!check_success(r, log_message)) {
    throw tcm_get_permit_data_error(error_message.c_str());
  }
}

template <int size = 1>
permit_t<size> get_permit_data(tcm_permit_handle_t ph, const std::string& error_message = "")
{
  // TODO: propagate 'allocate_mask' flag to the permit_t class
  permit_t<size> permit_wrapper;
  tcm_permit_t& permit = permit_wrapper;

  auto r = tcmGetPermitData(ph, &permit);
  if (!check_success(r, "tcmGetPermitData for ph=" + to_string(ph))) {
    throw tcm_get_permit_data_error(error_message);
  }

  return permit_wrapper;
}

inline permit_t</*size*/1> make_active_permit(uint32_t expected_concurrency,
                                              tcm_cpu_mask_t* cpu_masks = nullptr,
                                              tcm_permit_flags_t flags = {})
{
  const bool allocate_mask = bool(cpu_masks);
  permit_t</*size*/1> permit_wrapper(allocate_mask);
  tcm_permit_t& permit = permit_wrapper;
  permit.concurrencies[0] = expected_concurrency;
  permit.state = TCM_PERMIT_STATE_ACTIVE;
  permit.flags = flags;
  return permit_wrapper;
}

inline permit_t</*size*/1> make_inactive_permit(tcm_cpu_mask_t* cpu_masks = nullptr,
                                                tcm_permit_flags_t flags = {})
{
  const bool allocate_mask = bool(cpu_masks);
  permit_t</*size*/1> permit_wrapper(allocate_mask);
  tcm_permit_t& permit = permit_wrapper;
  permit.state = TCM_PERMIT_STATE_INACTIVE;
  permit.flags = flags;
  return permit_wrapper;
}

#endif // __TCM_TESTS_TEST_UTILS_HEADER
