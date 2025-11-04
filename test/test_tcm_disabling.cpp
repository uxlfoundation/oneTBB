/*
    Copyright (C) 2023-2025 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#include "basic_test_utils.h"

#include "tcm.h"

#include <limits>

static const bool is_tcm_enabled = []() {
  const char* tcm_enable_env_name = "TCM_ENABLE";

  const char* tcm_enable_env = std::getenv(tcm_enable_env_name);
  if (!tcm_enable_env)
      return false;

  const int tcm_enable_env_value = std::atoi(tcm_enable_env);
  if (tcm_enable_env_value == 0)
      return false;

  return true;
}();

TEST("test_tcm_connection") {
  tcm_client_id_t clid = std::numeric_limits<tcm_client_id_t>::max();
  tcm_client_id_t clid_bak = clid;

  bool is_connection_successful = succeeded(tcmConnect(nullptr, &clid));

  if (is_tcm_enabled) {
    check(is_connection_successful, "tcmConnect accepts connection");
    check(clid_bak != clid, "tcmConnect assigns client id");
    check_success(tcmDisconnect(clid), "tcmDisconnect returns successfully");
  } else {
    check(!is_connection_successful, "tcmConnect refuses connection");
    check(clid_bak == clid, "tcmConnect does not change client id");
  }
}

TEST("test_tcm_suggesting") {
  if (is_tcm_enabled) {
    test_log("Test is skipped because TCM is ENABLED");
    return;
  }
  bool connection_failed = true;
  tcm_client_id_t clid1;
  tcm_client_id_t clid2;
  connection_failed &= !succeeded(tcmConnect(nullptr, &clid1));
  connection_failed &= !succeeded(tcmConnect(nullptr, &clid2));
  check(connection_failed, "tcmConnect is supposed to fail when it wasn't enabled");
}
