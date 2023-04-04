/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#include "test_utils.h"

#include "tcm.h"
#include <limits>

const char* tcm_disable_env_name = "TCM_DISABLE";

bool is_tcm_enabled() {
  if (char* tcm_disable_env = std::getenv(tcm_disable_env_name)) {
    int tcm_disable_env_value = std::atoi(tcm_disable_env);
    if (tcm_disable_env_value != 0) {
      return false;
    }
  }
  return true;
}

bool test_tcm_connection(const bool is_success_expected) {
  const char* test_name = "test_tcm_disabling";
  test_prolog(test_name);

  tcm_client_id_t clid = std::numeric_limits<tcm_client_id_t>::max();
  tcm_client_id_t clid_bak = clid;

  bool is_connection_successful = succeeded(tcmConnect(nullptr, &clid));

  bool res = true;
  if (is_success_expected) {
    res &= check(is_connection_successful, "tcmConnect accepts connection");
    res &= check(clid_bak != clid, "tcmConnect assigns client id");
  } else {
    res &= check(!is_connection_successful, "tcmConnect refuses connection");
    res &= check(clid_bak == clid, "tcmConnect does not change client id");
  }

  return test_stop(res, test_name);
}

int main() {
  bool res = true;

  const bool is_successful_connection_expected = is_tcm_enabled();

  res &= test_tcm_connection(is_successful_connection_expected);

  return int(!res);
}
