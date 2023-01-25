/*
 *
 * Copyright (C) 2021-2022 Intel Corporation
 *
 */

#pragma once

#include "hwloc_test_utils.h"
#include "tcm/detail/_tcm_assert.h"
#include "tcm.h"
// MSVC Warning: unreferenced formal parameter
__TCM_SUPPRESS_WARNING_WITH_PUSH(4100)
#include "hwloc.h"
__TCM_SUPPRESS_WARNING_POP
#include <stdlib.h>
#include <vector>
#include <set>
#include <iostream>
#include <algorithm>
#include <thread>
#include <string>
#include <limits>

 // MSVC Warning: Args can be incorrect: this does not match function name specification
__TCM_SUPPRESS_WARNING_WITH_PUSH(6387)
// TODO: Function SetEnv() is borrowed from oneTBB project. Check the license.
inline int SetEnv( const char *envname, const char *envval ) {
  __TCM_ASSERT( (envname && envval), "SetEnv requires two valid C strings" );
#if !(_MSC_VER || __MINGW32__ || __MINGW64__)
  // On POSIX systems use setenv
  return setenv(envname, envval, /*overwrite=*/1);
#elif __STDC_SECURE_LIB__>=200411
  // this macro is set in VC & MinGW if secure API functions are present
  return _putenv_s(envname, envval);
#else
  // If no secure API on Windows, use _putenv
  size_t namelen = strlen(envname), valuelen = strlen(envval);
  char* buf = new char[namelen+valuelen+2];
  strncpy(buf, envname, namelen);
  buf[namelen] = '=';
  strncpy(buf+namelen+1, envval, valuelen);
  buf[namelen+1+valuelen] = char(0);
  int status = _putenv(buf);
  delete[] buf;
  return status;
#endif
}

char* GetEnv(const char* envname) {
    __TCM_ASSERT(envname, "GetEnv requires valid C string");
    return std::getenv(envname);
}
__TCM_SUPPRESS_WARNING_POP


struct masks_guard_t {
  masks_guard_t(uint32_t size) : m_size(size) {}
  void operator()(zerm_cpu_mask_t* cpu_masks) const {
    for (uint32_t i = 0; i < m_size; ++i) {
      hwloc_bitmap_free(cpu_masks[i]);
    }
    delete [] cpu_masks;
  }
private:
  const uint32_t m_size;
};


zerm_permit_request_t make_request(int min_sw_threads, int max_sw_threads,
                                   zerm_cpu_constraints_t* constraints = nullptr, uint32_t size = 0,
                                   zerm_request_priority_t priority = ZERM_REQUEST_PRIORITY_NORMAL,
                                   zerm_permit_flags_t flags = {})
{
    __TCM_ASSERT(0 <= min_sw_threads && min_sw_threads <= max_sw_threads, "Incorrect concurrency requested");
    __TCM_ASSERT((constraints && size) || (!constraints && !size), "Inconsistent request.");
    return zerm_permit_request_t{
        min_sw_threads, max_sw_threads, constraints, size, priority, flags, /*reserved*/{0}
    };
}

// TODO: rename to make_permit

zerm_permit_t make_permit(uint32_t* concurrencies, zerm_cpu_mask_t* cpu_masks = nullptr,
                          uint32_t size = 1, zerm_permit_state_t state = ZERM_PERMIT_STATE_VOID,
                          zerm_permit_flags_t flags = {})
{
  __TCM_ASSERT(concurrencies, "Array of concurrencies cannnot be nullptr.");
  return zerm_permit_t{concurrencies, cpu_masks, size, state, flags};
}

zerm_permit_t make_void_permit(uint32_t* concurrencies, zerm_cpu_mask_t* cpu_masks = nullptr,
                               uint32_t size = 1, zerm_permit_flags_t flags = {})
{
    return make_permit(concurrencies, cpu_masks, size, ZERM_PERMIT_STATE_VOID, flags);
}

zerm_permit_t make_active_permit(uint32_t* concurrencies, zerm_cpu_mask_t* cpu_masks = nullptr,
                                 uint32_t size = 1, zerm_permit_flags_t flags = {})
{
  return make_permit(concurrencies, cpu_masks, size, ZERM_PERMIT_STATE_ACTIVE, flags);
}

zerm_permit_t make_pending_permit(uint32_t* concurrencies, zerm_cpu_mask_t* cpu_masks = nullptr,
                                  uint32_t size = 1, zerm_permit_flags_t flags = {})
{
  return make_permit(concurrencies, cpu_masks, size, ZERM_PERMIT_STATE_PENDING, flags);
}

std::string bitmap_to_string(const zerm_cpu_mask_t mask) {
  const unsigned max_size = 1024;
  char buf[max_size] = {0};
  hwloc_bitmap_snprintf(buf, max_size, (hwloc_const_bitmap_t)mask);
  return std::string(buf);
}

std::string to_string(const zerm_permit_flags_t flags) {
  return std::string("flags: stale=" + std::to_string(flags.stale) +
                     ", rigid_concurrecy=" + std::to_string(flags.rigid_concurrency) +
                     ", excluisive=" + std::to_string(flags.exclusive));
}

bool check(bool b, const std::string& msg, const std::string& report_msg = "") {
  if (!b) {
    std::cout << "*****************" << msg << std::endl;
    std::cout << "      ERROR      " << msg << std::endl;
    std::cout << "*****************" << msg << std::endl;
    std::cout << report_msg;
  }  else if (!msg.empty()){
    std::cout << "SUCCESS: " << msg << std::endl;
  }
  return b;
}

inline void test_prolog(const std::string& msg) {
  std::cout << "\n\nSUCCESS: begin " << msg << std::endl;
}

inline bool test_stop(bool b, const std::string& msg) {
  return check(b, "end " + msg);
}

inline bool test_fail(const std::string& msg) {
  return test_stop(false, msg);
}

inline bool test_epilog(const std::string& msg) {
  return test_stop(true, msg);
}

inline bool succeeded(ze_result_t res) {
  return (ZE_RESULT_SUCCESS == res);
}

inline bool check_success(ze_result_t res, const std::string& msg = "",
                          const std::string& report_msg = "")
{
  return check(succeeded(res), msg, report_msg);
}

const float tcm_oversubscription_factor = [] {
  const char* oversb_factor_env_value = GetEnv("TCM_OVERSUBSCRIPTION_FACTOR");
  float oversb_factor = 1.0f;
  if (oversb_factor_env_value) {
    // TODO: Consider alternative options for std::stof
    oversb_factor = std::stof(oversb_factor_env_value);
    __TCM_ASSERT(oversb_factor > std::numeric_limits<float>::epsilon(),
                 "Incorrect value of TCM_OVERSUBSCRIPTION_FACTOR environment variable.");
  }
  return oversb_factor;
}();

//! Returns available platform resources, taking into account the possible
//! degree of the oversubscription (oversb_factor must be greater than zero).
uint32_t platform_resources () {
  return uint32_t(tcm_oversubscription_factor * std::thread::hardware_concurrency());
}

//! Returns available platform resources, taking into account the possible
//! degree of the oversubscription (oversb_factor must be greater than zero) and
//! process mask.
uint32_t platform_resources(tcm_test::system_topology& tp) {
  auto process_mask = tp.allocate_process_affinity_mask();
  uint32_t result = uint32_t(tcm_oversubscription_factor * hwloc_bitmap_weight(process_mask));
  tp.free_affinity_mask(process_mask);
  return result;
}

// TODO: rename total_number_of_threads to num_total_threads
const int32_t total_number_of_threads = platform_resources();

bool check_permit_size(const zerm_permit_t& expected, const zerm_permit_t& actual,
                       const bool report = true)
{
  const auto& a = actual.size;
  const auto& e = expected.size;
  const bool result = (a == e);

  std::string report_str = "Check size of arrays inside permit, expected " + std::to_string(e) +
      " equals to actual " + std::to_string(a);

  return report ? check(result, report_str) : result;
}


bool check_permit_concurrency(const zerm_permit_t& expected, const zerm_permit_t& actual,
                              const bool report = true)
{
  bool result = false;
  std::string report_str{};

  for (unsigned i = 0; i < expected.size; ++i) {
    const auto& e = expected.concurrencies[i]; const auto& a = actual.concurrencies[i];
    result = (e == a);
    // TODO: print not only the wrong element, but the whole range
    report_str = "Check concurrency, expected " + std::to_string(e) +
      " equals to actual " + std::to_string(a) + ", concurrency array index of "
        + std::to_string(i);

    if (!result)
      break;
  }

  return report ? check(result, report_str) : result;
}

bool check_permit_mask(const zerm_permit_t& expected, const zerm_permit_t& actual,
                       const bool report = true)
{
  std::string report_str{};
  // check the cpu_masks pointers
  if (expected.cpu_masks == nullptr && actual.cpu_masks == nullptr) {
    return true;
  } else if (expected.cpu_masks == nullptr || actual.cpu_masks == nullptr) {
    report_str = "Check CPU mask, expected.masks '" +
                 std::to_string(uintptr_t(expected.cpu_masks)) +
                 "' equals to actual '" + std::to_string(uintptr_t(actual.cpu_masks)) + "'";
    return report ? check(false, report_str) : false;
  }

  bool result = false;
  for (unsigned i = 0; i < expected.size; ++i) {
    // check the cpu_masks values
    const auto& e = expected.cpu_masks[i]; const auto& a = actual.cpu_masks[i];

    if (e && a) {
      result = (hwloc_bitmap_compare(e, a) == 0);
      // TODO: print not only the wrong element, but the whole range
      report_str = "Check CPU mask, expected '" + bitmap_to_string(e) +
        "' equals to actual '" + bitmap_to_string(a) + "', mask index " + std::to_string(i);
    } else {
      result = (a == nullptr);
      report_str = "Check CPU mask is not nullptr: actual '" +  bitmap_to_string(a) +
        "', index " + std::to_string(i);
    }

    if (!result) break;
  }

  return report ? check(result, report_str) : result;
}

static const char* states[] = {
  "VOID", "INACTIVE", "PENDING", "IDLE", "ACTIVE"
};

bool check_permit_state(const zerm_permit_t& expected, const zerm_permit_t& actual,
                        const bool report = true)
{
  const auto& e = expected.state; const auto& a = actual.state;
  const bool result = (e == a);
  std::string report_str = "Check state, expected " + std::to_string(e) +
    " (" + std::string(states[e]) + ") equals to actual " + std::to_string(a) +
    " (" + std::string(states[a]) + ")";

  return report ? check(result, report_str) : result;
}

bool check_permit_flags(const zerm_permit_t& expected, const zerm_permit_t& actual,
                        const bool report = true)
{
  const auto& e = expected.flags; const auto& a = actual.flags;
  bool result = true;
  result &= e.stale == a.stale;
  result &= e.rigid_concurrency == a.rigid_concurrency;
  result &= e.exclusive == a.exclusive;

  std::string report_str("Check expectation of " + to_string(e) + " equals to actual " + to_string(a));

  return report ? check(result, report_str) : result;
}

struct skip_checks_t {
  bool size = false;
  bool concurrency = false;
  bool state = false;
  bool flags = false;
  bool mask = false;
};

// Compares two permits' data. Returns true if the data is equal, false -
// otherwise. Function allows skipping check of specific permit data fields.
bool check_permit(const zerm_permit_t& expected, const zerm_permit_t& actual,
                  const skip_checks_t skip = {}, const bool report = true)
{
  bool result = true;
  result &= skip.size         || check_permit_size(expected, actual, report);
  result &= skip.concurrency  || check_permit_concurrency(expected, actual, report);
  result &= skip.mask         || check_permit_mask(expected, actual, report);
  result &= skip.state        || check_permit_state(expected, actual, report);
  result &= skip.flags        || check_permit_flags(expected, actual, report);
  return result;
}

//! Checks the expected permit data with the data obtained by reading passed
//! permit handle. Returns true if the data is equal, and false - otherwise.
bool check_permit(const zerm_permit_t& expected, zerm_permit_handle_t ph,
                  const skip_checks_t skip = {}, const bool report = true)
{
  if (!check(ph, "check permit handle is not nullptr."))
    return false;

  __TCM_ASSERT(expected.size > 0, "Permit size cannot be zero.");
  std::vector<uint32_t> concurrencies(expected.size, 0);
  std::unique_ptr<zerm_cpu_mask_t[], masks_guard_t> cpu_masks(nullptr, masks_guard_t(expected.size));
  if (expected.cpu_masks) {
    cpu_masks.reset(new zerm_cpu_mask_t[expected.size]);
    for (uint32_t i = 0; i < expected.size; ++i) {
      cpu_masks[i] = hwloc_bitmap_alloc();
    }
  }

  zerm_permit_t actual = make_void_permit(concurrencies.data(), cpu_masks.get(), expected.size);
  ze_result_t reading_result = zermGetPermitData(ph, &actual);
  if (!check_success(reading_result)) {
      return check(false, "Reading data from permit " + std::to_string(uintptr_t(ph)),
                   "zermGetPermitData() returns status " + std::to_string(reading_result));
  }

  bool result = check_permit(expected, actual, skip, report);
  return result;
}

typedef std::vector< std::pair<zerm_permit_handle_t*, zerm_permit_t*> > permits_data_t;

/**
 * \brief Returns a set of permit handles, whose data is equal to corresponding
 * (expected) permit data passed as a sequence of pairs.
 *
 * Thus, if existing permits are passed as a parameter, the function returns
 * permit handles, for which the renegotiation should not have taken place.
 */
std::set<zerm_permit_handle_t*> list_unchanged_permits(const permits_data_t& pds) {
  std::set<zerm_permit_handle_t*> result;

  for (auto& pd : pds) {
    const zerm_permit_handle_t ph = *pd.first;
    const zerm_permit_t& expected = *pd.second;

    if (check_permit(expected, ph, skip_checks_t{}, /*report*/false))
      result.insert(pd.first);
  }

  return result;
}
