// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#pragma once

#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"

namespace yb {

class Pg15UpgradeTestBase : public UpgradeTestBase {
 public:
  Pg15UpgradeTestBase() : UpgradeTestBase(kBuild_2_20_2_4) {}
  virtual ~Pg15UpgradeTestBase() override = default;

  void SetUp() override;

 protected:
  // UpgradeTestBase provides helper functions UpgradeClusterToCurrentVersion, FinalizeUpgrade,
  // and RollbackClusterToOldVersion. These restart all tservers to the current version. The below
  // helper functions only upgrade one tserver (id=kMixedModeTserverPg15) to the current version and
  // keep the other tservers in the old version, helping us perform validations while in mixed mode.

  const size_t kMixedModeTserverPg15 = 0;
  const size_t kMixedModeTserverPg11 = 1;

  // Restarts all masters in the current version, runs ysql major version upgrade, and restarts
  // tserver kMixedModeTserverPg15 in the current version. Other tservers are kept in the pg11
  // version.
  Status UpgradeClusterToMixedMode();

  // Restarts all other tservers in the current version, and finalizes the upgrade.
  Status FinalizeUpgradeFromMixedMode();

  // Restarts tserver kMixedModeTserverPg15 in the old version, rolls backs the ysql major version
  // upgrade, and restarts all masters in the old version.
  Status RollbackUpgradeFromMixedMode();

  // Connects to a random tserver and executes ysql statements.
  Status ExecuteStatements(const std::vector<std::string>& sql_statements);
  Status ExecuteStatement(const std::string& sql_statement);

  Result<pgwrapper::PGConn> CreateConnToTs(size_t ts_id);

  // Run a ysql statement via ysqlsh against a given tserver.
  Result<std::string> ExecuteViaYsqlshOnTs(const std::string& sql_statement, size_t ts_id);

  // Run a ysql statement via ysqlsh against a random tserver.
  Result<std::string> ExecuteViaYsqlsh(const std::string& sql_statement);
};

}  // namespace yb
