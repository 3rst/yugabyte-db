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

#include "yb/master/xcluster/master_xcluster_util.h"

#include "yb/common/common_types.pb.h"
#include "yb/common/xcluster_util.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"

namespace yb::master {

bool IsTableEligibleForXClusterReplication(const master::TableInfo& table) {
  if (table.GetTableType() != PGSQL_TABLE_TYPE || table.is_system()) {
    // DB Scoped replication Limited to ysql databases.
    // System tables are not replicated. DDLs statements will be replicated and executed on the
    // target universe to handle catalog changes.
    return false;
  }

  if (table.IsColocationParentTable()) {
    // The colocated parent table needs to be replicated.
    return true;
  }

  if (table.is_matview()) {
    // Materialized views need not be replicated, since they are not modified. Every time the view
    // is refreshed, new tablets are created. The same refresh can just run on the target universe.
    return false;
  }

  if (table.IsColocatedUserTable()) {
    // Only the colocated parent table needs to be replicated.
    return false;
  }

  if (table.IsSequencesSystemTable()) {
    // The sequences_data table is treated specially elsewhere.
    return false;
  }

  if (table.IsXClusterDDLReplicationReplicatedDDLsTable()) {
    // replicated_ddls is only used on the target, so we do not want to replicate it.
    return false;
  }

  return true;
}

std::string GetFullTableName(const TableInfo& table_info) {
  const auto& schema_name = table_info.pgschema_name();
  if (schema_name.empty()) {
    return table_info.name();
  }

  return Format("$0.$1", schema_name, table_info.name());
}

std::string TableDesignator::ToString() const {
  return strings::Substitute("$0.$1 [id=$2]", pgschema_name, name, id);
}

TableDesignator GetDesignatorFromTableInfo(const TableInfo& table_info) {
  TableDesignator designator;
  designator.id = table_info.id();
  designator.name = table_info.name();
  designator.pgschema_name = table_info.pgschema_name();
  return designator;
}

Result<std::vector<TableDesignator>> GetTablesEligibleForXClusterReplication(
    const CatalogManager& catalog_manager, const NamespaceId& namespace_id,
    bool include_sequences_data) {
  auto table_infos = VERIFY_RESULT(catalog_manager.GetTableInfosForNamespace(namespace_id));

  std::vector<TableDesignator> table_designators{};
  for (const auto& table_info : table_infos) {
    if (IsTableEligibleForXClusterReplication(*table_info)) {
      table_designators.push_back(GetDesignatorFromTableInfo(*table_info));
    }
  }

  if (include_sequences_data) {
    auto sequence_table_info = catalog_manager.GetTableInfo(kPgSequencesDataTableId);
    if (sequence_table_info) {
      TableDesignator designator = GetDesignatorFromTableInfo(*sequence_table_info);
      designator.id = xcluster::GetSequencesDataAliasForNamespace(namespace_id);
      table_designators.push_back(designator);
    }
  }
  return table_designators;
}

bool IsDbScoped(const SysUniverseReplicationEntryPB& replication_info) {
  return replication_info.has_db_scoped_info() &&
         replication_info.db_scoped_info().namespace_infos_size() > 0;
}

bool IsAutomaticDdlMode(const SysUniverseReplicationEntryPB& replication_info) {
  return replication_info.has_db_scoped_info() &&
         replication_info.db_scoped_info().automatic_ddl_mode();
}

}  // namespace yb::master
