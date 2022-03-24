/*
 *
 * Copyright (C) 2022 Intel Corporation
 *
 */

#include "test_utils.h"

#include "tcm.h"
#include <limits>

const char* tcm_disable_env_name = "TCM_DISABLE";

bool is_tcm_enabled() {
  char* tcm_disable_env = std::getenv(tcm_disable_env_name);
  if (tcm_disable_env) {
    std::string tcm_disable_env_value(tcm_disable_env);
    if (tcm_disable_env_value != "0")
      return false;
  }
  return true;
}

bool test_tcm_connection(const bool is_success_expected) {
  check(true, "\n\nbegin test_tcm_disabling");

  zerm_client_id_t clid = std::numeric_limits<zerm_client_id_t>::max();
  zerm_client_id_t clid_bak = clid;

  ze_result_t r = zermConnect(nullptr, &clid);
  bool is_connection_successful = (r == ZE_RESULT_SUCCESS);

  bool test_result = true;
  if (is_success_expected) {
    test_result &= check(is_connection_successful, "zermConnect accepts connection");
    test_result &= check(clid_bak != clid, "zermConnect assigns client id");
  } else {
    test_result &= check(!is_connection_successful, "zermConnect refuses connection");
    test_result &= check(clid_bak == clid, "zermConnect does not change client id");
  }

  check(true, "end test_tcm_disabling");

  return test_result;
}

int main() {
  bool res = true;

  const bool is_successful_connection_expected = is_tcm_enabled();

  res &= test_tcm_connection(is_successful_connection_expected);

  return int(!res);
}
