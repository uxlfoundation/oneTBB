/*
 *
 * Copyright (C) 2021-2022 Intel Corporation
 *
 */

#pragma once

#include "tcm/detail/_tcm_assert.h"
#include "tcm.h"
#include "hwloc.h"
#include <stdlib.h>
#include <vector>
#include <set>
#include <iostream>
#include <algorithm>
#include <thread>
#include <string>
#include <limits>

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

std::string bitmap_to_string(const zerm_cpu_mask_t mask) {
  int size = hwloc_bitmap_weight(mask);
  if (size <= 0) size = 256;
  std::string result(size, ' ');
  hwloc_bitmap_snprintf(result.data(), result.size(), mask);
  return result;
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
  }  else {
    std::cout << "SUCCESS: " << msg << std::endl;
  }
  return b;
}

//! Returns available platform resources, taking into account the possible degree
//! of the oversubscription (oversb_factor must be greater than zero).
uint32_t platform_resources () {
  const char* oversb_factor_env_value = std::getenv("RM_OVERSUBSCRIPTION_FACTOR");
  float oversb_factor = 1.0f;
  if (oversb_factor_env_value) {
    // TODO: Consider alternative options for std::stof
    oversb_factor = std::stof(oversb_factor_env_value);
    check(oversb_factor > std::numeric_limits<float>::epsilon(),
          "Incorrect value of RM_OVERSUBSCRIPTION_FACTOR environment variable.");
  }

  return uint32_t(oversb_factor * std::thread::hardware_concurrency());
}

// TODO: rename total_number_of_threads to num_total_threads
const int32_t total_number_of_threads = platform_resources();

bool check_permit_size(const zerm_permit_t& expected, const zerm_permit_t& actual,
                       const bool report = true)
{
  const auto& a = actual.size;
  const auto& e = expected.size;
  const bool result = (a == e);
  std::string report_str = "Check concurrency, expected " + std::to_string(e) + " equals to actual " + std::to_string(a);

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
      " equals to actual " + std::to_string(a) + " ,index " + std::to_string(i);

    if (!result)
      break;
  }

  return report ? check(result, report_str) : result;
}

bool check_permit_mask(const zerm_permit_t& expected, const zerm_permit_t& actual,
                       const bool report = true)
{
  // check the cpu_masks pointers
  if (expected.cpu_masks == nullptr && actual.cpu_masks == nullptr) {
    return true;
  } else if (expected.cpu_masks == nullptr || actual.cpu_masks == nullptr) {
    return check(false, "Check CPU masks");
  }

  bool result = false;
  std::string report_str{};
  for (unsigned i = 0; i < expected.size; ++i) {
    // check the cpu_masks values
    const auto& e = expected.cpu_masks[i]; const auto& a = actual.cpu_masks[i];

    if (e && a) {
      result = (hwloc_bitmap_compare(e,a) == 0);
      // TODO: print not only the wrong element, but the whole range
      report_str = "Check CPU mask, expected " +  bitmap_to_string(e) +
        " equals to actual " + bitmap_to_string(a) + " ,index " + std::to_string(i);
    } else {
      result = (a == nullptr);
      report_str = "Check CPU mask is not nullptr: actual " +  bitmap_to_string(a) +
        " ,index " + std::to_string(i);
    }

    if (!result) break;
  }

  return report ? check(result, report_str) : result;
}

bool check_permit_state(const zerm_permit_t& expected, const zerm_permit_t& actual,
                        const bool report = true)
{
  const auto& e = expected.state; const auto& a = actual.state;
  const bool result = (e == a);
  std::string report_str = "Check state, expected " + std::to_string(e) +
    " equals to actual " + std::to_string(a);

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

  std::string report_str("Check state, flags " + to_string(e) + " equals to actual " + to_string(a));

  return report ? check(result, report_str) : result;
}

struct skip_checks_t {
  bool size = false;
  bool concurrency = false;
  bool state = false;
  bool flags = false;
  bool mask = false;
};

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

//! Expects successful reading of a permit, thus cannot be used concurrently
//! with the modification of the permit. Throws exception if reading of the
//! permit was not successful.
bool check_permit(const zerm_permit_t& expected, zerm_permit_handle_t ph,
                  const skip_checks_t skip = {}, const bool report = true)
{
  if (!check(ph, "check permit handle is not nullptr."))
    return false;

  uint32_t actual_concurrency;
  zerm_permit_t actual{&actual_concurrency, nullptr};
  ze_result_t reading_result = zermGetPermitData(ph, &actual);
  if (!check(reading_result == ZE_RESULT_SUCCESS,
             "Reading data from permit " + std::to_string(uintptr_t(ph))))
    throw std::string("Error: failed reading permit with status " + std::to_string(reading_result));

  return check_permit(expected, actual, skip, report);
}

typedef std::vector< std::pair<zerm_permit_handle_t*, zerm_permit_t*> > permits_data_t;

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

zerm_permit_request_t make_request(int min_sw_threads, int max_sw_threads,
                                 zerm_permit_flags_t flags = {}) {
  zerm_permit_request_t result = ZERM_PERMIT_REQUEST_INITIALIZER;
  result.min_sw_threads = min_sw_threads;
  result.max_sw_threads = max_sw_threads;
  result.flags = flags;
  return result;
}
