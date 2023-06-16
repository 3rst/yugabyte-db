// Copyright (c) YugaByte, Inc.
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

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include "yb/master/master.h"
#include "yb/master/mini_master.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_Read);
METRIC_DECLARE_counter(pg_response_cache_queries);
METRIC_DECLARE_counter(pg_response_cache_hits);
METRIC_DECLARE_counter(pg_response_cache_renew_soft);
METRIC_DECLARE_counter(pg_response_cache_renew_hard);
METRIC_DECLARE_counter(pg_response_cache_gc_calls);
METRIC_DECLARE_counter(pg_response_cache_entries_removed_by_gc);

DECLARE_bool(ysql_enable_read_request_caching);
DECLARE_bool(ysql_minimal_catalog_caches_preload);
DECLARE_uint64(TEST_pg_response_cache_catalog_read_time_usec);
DECLARE_uint64(TEST_committed_history_cutoff_initial_value_usec);
DECLARE_uint32(pg_cache_response_renew_soft_lifetime_limit_ms);
DECLARE_uint64(pg_response_cache_size_bytes);
DECLARE_uint32(pg_response_cache_size_percentage);

namespace yb::pgwrapper {
namespace {

Status EnableCatCacheEventLogging(PGConn* conn) {
  return conn->Execute("SET yb_debug_log_catcache_events = ON");
}

class Configuration {
 public:
  constexpr explicit Configuration(bool minimal_catalog_caches_preload)
    : minimal_catalog_caches_preload_(minimal_catalog_caches_preload) {}

  constexpr Configuration(bool minimal_catalog_caches_preload, uint64_t response_cache_size_bytes)
    : minimal_catalog_caches_preload_(minimal_catalog_caches_preload),
      response_cache_size_bytes_(response_cache_size_bytes) {}

  bool minimal_catalog_caches_preload() const { return minimal_catalog_caches_preload_; }
  bool enable_read_request_caching() const { return response_cache_size_bytes_.has_value(); }
  std::optional<uint64_t> response_cache_size_bytes() const { return response_cache_size_bytes_; }

 private:
  bool minimal_catalog_caches_preload_;
  std::optional<uint64_t> response_cache_size_bytes_;
};

class PgCatalogPerfTestBase : public PgMiniTestBase {
 public:
  virtual ~PgCatalogPerfTestBase() = default;

 protected:
  void SetUp() override {
    auto config = GetConfig();
    FLAGS_ysql_enable_read_request_caching = config.enable_read_request_caching();
    FLAGS_ysql_minimal_catalog_caches_preload = config.minimal_catalog_caches_preload();
    FLAGS_pg_response_cache_size_percentage = 0;
    FLAGS_pg_response_cache_size_bytes = config.response_cache_size_bytes().value_or(0);
    PgMiniTestBase::SetUp();
    metrics_.emplace(
        *cluster_->mini_master()->master(), *cluster_->mini_tablet_server(0)->server());
  }

  size_t NumTabletServers() override {
    return 1;
  }

  Result<uint64_t> CacheRefreshRPCCount() {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(EnableCatCacheEventLogging(&conn));
    auto conn_aux = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(EnableCatCacheEventLogging(&conn_aux));
    RETURN_NOT_OK(conn_aux.Execute("CREATE TABLE t (k INT)"));
    RETURN_NOT_OK(conn_aux.Execute("ALTER TABLE t ADD COLUMN v INT"));
    // Catalog version was increased by the conn_aux but conn may not detect this immediately.
    // So run simplest possible query which doesn't produce RPC in a loop until number of
    // RPC will be greater than 0.
    for (;;) {
      const auto result = VERIFY_RESULT(metrics_->read_rpc_.Delta([&conn] {
        return conn.Execute("ROLLBACK");
      }));
      if (result) {
        return result;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    return STATUS(RuntimeError, "Unreachable statement");
  }

  using AfterCacheRefreshFunctor = std::function<Status(PGConn*)>;
  Result<uint64_t> RPCCountAfterCacheRefresh(const AfterCacheRefreshFunctor& functor) {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Execute("CREATE TABLE cache_refresh_trigger (k INT)"));
    // Force version increment. Next new connection will do cache refresh on start.
    RETURN_NOT_OK(conn.Execute("ALTER TABLE cache_refresh_trigger ADD COLUMN v INT"));
    auto aux_conn = VERIFY_RESULT(Connect());
    return metrics_->read_rpc_.Delta([&functor, &aux_conn] {
      return functor(&aux_conn);
    });
  }

  struct MetricCounters {
    size_t read_rpc = 0;
    size_t cache_queries = 0;
    size_t cache_hits = 0;
    size_t cache_renew_soft = 0;
    size_t cache_renew_hard = 0;
    size_t cache_gc_calls = 0;
    size_t cache_entries_removed_by_gc = 0;
  };

  Result<MetricCounters> MetricDeltas(MetricWatcher::DeltaFunctor functor) {
    MetricCounters counters;
    MetricDeltasCapturer capturer(std::move(functor));
    RETURN_NOT_OK(capturer
        .Capture(metrics_->cache_queries_,
                 [&counters](size_t delta) {counters.cache_queries = delta; })
        .Capture(metrics_->cache_hits_,
                 [&counters](size_t delta) {counters.cache_hits = delta; })
        .Capture(metrics_->cache_renew_soft_,
                 [&counters](size_t delta) {counters.cache_renew_soft = delta; })
        .Capture(metrics_->cache_renew_hard_,
                 [&counters](size_t delta) {counters.cache_renew_hard = delta; })
        .Capture(metrics_->cache_gc_calls_,
                 [&counters](size_t delta) {counters.cache_gc_calls = delta; })
        .Capture(metrics_->cache_entries_removed_by_gc_,
                 [&counters](size_t delta) {counters.cache_entries_removed_by_gc = delta; })
        .Capture(metrics_->read_rpc_,
                 [&counters](size_t delta) {counters.read_rpc = delta; })
        .Run());
    return counters;
  }

 private:
  virtual Configuration GetConfig() const = 0;

  class MetricDeltasCapturer {
   public:
    using Capturer = std::function<void(size_t)>;

    explicit MetricDeltasCapturer(MetricWatcher::DeltaFunctor&& functor)
        : functor_(functor) {}

    MetricDeltasCapturer& Capture(
        std::reference_wrapper<const MetricWatcher> watcher,
        Capturer&& capturer) {
      functor_ = [&w = watcher.get(), c = std::move(capturer), f = std::move(functor_)] {
        c(VERIFY_RESULT(w.Delta(f)));
        return static_cast<Status>(Status::OK());
      };
      return *this;
    }

    Status Run() {
      return functor_();
    }

   private:
    MetricWatcher::DeltaFunctor functor_;
  };

  struct Metrics {
    Metrics(const master::Master& master, const tserver::TabletServer& tserver)
        : read_rpc_(master, METRIC_handler_latency_yb_tserver_TabletServerService_Read),
          cache_queries_(tserver, METRIC_pg_response_cache_queries),
          cache_hits_(tserver, METRIC_pg_response_cache_hits),
          cache_renew_soft_(tserver, METRIC_pg_response_cache_renew_soft),
          cache_renew_hard_(tserver, METRIC_pg_response_cache_renew_hard),
          cache_gc_calls_(tserver, METRIC_pg_response_cache_gc_calls),
          cache_entries_removed_by_gc_(tserver, METRIC_pg_response_cache_entries_removed_by_gc) {}

    MetricWatcher read_rpc_;
    MetricWatcher cache_queries_;
    MetricWatcher cache_hits_;
    MetricWatcher cache_renew_soft_;
    MetricWatcher cache_renew_hard_;
    MetricWatcher cache_gc_calls_;
    MetricWatcher cache_entries_removed_by_gc_;
  };

  std::optional<Metrics> metrics_;
};

class PgCatalogPerfBasicTest : public PgCatalogPerfTestBase {
 protected:
  // Test checks number of RPC to a master caused by the first INSERT stmt into a table with primary
  // key after cache refresh.
  void TestAfterCacheRefreshRPCCountOnInsert(size_t expected_master_rpc_count) {
    auto aux_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(aux_conn.Execute("CREATE TABLE t (k INT PRIMARY KEY)"));
    auto master_rpc_count_for_insert = ASSERT_RESULT(RPCCountAfterCacheRefresh([](PGConn* conn) {
      return conn->Execute("INSERT INTO t VALUES(0)");
    }));
    ASSERT_EQ(master_rpc_count_for_insert, expected_master_rpc_count);
  }

  // Test checks number of RPC to a master caused by the first SELECT stmt from a table with primary
  // key after cache refresh.
  void TestAfterCacheRefreshRPCCountOnSelect(size_t expected_master_rpc_count) {
    auto aux_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(aux_conn.Execute("CREATE TABLE t (k INT PRIMARY KEY)"));
    auto master_rpc_count_for_select = ASSERT_RESULT(RPCCountAfterCacheRefresh([](PGConn* conn) {
      VERIFY_RESULT(conn->Fetch("SELECT * FROM t"));
      return static_cast<Status>(Status::OK());
    }));
    ASSERT_EQ(master_rpc_count_for_select, expected_master_rpc_count);
  }
};

constexpr Configuration kConfigDefault(
    /*minimal_catalog_caches_preload=*/false);

constexpr Configuration kConfigWithUnlimitedCache(
    /*minimal_catalog_caches_preload=*/false, /*response_cache_size_bytes=*/0);

constexpr Configuration kConfigMinPreload(
    /*minimal_catalog_caches_preload=*/true);

constexpr Configuration kConfigWithLimitedCache(
    /*minimal_catalog_caches_preload=*/false, /*response_cache_size_bytes=*/5 * 1024 * 1024);

template<class Base, const Configuration& Config>
class ConfigurableTest : public Base {
 private:
  Configuration GetConfig() const override {
    return Config;
  }
};

using PgCatalogPerfTest = ConfigurableTest<PgCatalogPerfBasicTest, kConfigDefault>;
using PgCatalogMinPreloadTest = ConfigurableTest<PgCatalogPerfBasicTest, kConfigMinPreload>;
using PgCatalogWithUnlimitedCachePerfTest =
    ConfigurableTest<PgCatalogPerfTestBase, kConfigWithUnlimitedCache>;
using PgCatalogWithLimitedCachePerfTest =
    ConfigurableTest<PgCatalogPerfTestBase, kConfigWithLimitedCache>;

class PgCatalogWithStaleResponseCacheTest : public PgCatalogWithUnlimitedCachePerfTest {
 protected:
  void SetUp() override {
    constexpr uint64_t kHistoryCutoffInitialValue = 10000000;
    FLAGS_TEST_committed_history_cutoff_initial_value_usec = kHistoryCutoffInitialValue;
    // Substitute catalog_read_time in cached responses with value lower than history cutoff to
    // get 'Snapshot too old' error on attempt to read at this read time.
    FLAGS_TEST_pg_response_cache_catalog_read_time_usec = kHistoryCutoffInitialValue - 1;
    FLAGS_pg_cache_response_renew_soft_lifetime_limit_ms = 1000;
    PgCatalogWithUnlimitedCachePerfTest::SetUp();
  }
};

} // namespace

// Test checks the number of RPC for very first and subsequent connection to same t-server.
// Very first connection prepares local cache file while subsequent connections doesn't do this.
// As a result number of RPCs has huge difference.
// Note: Also subsequent connections doesn't preload the cache. This maybe changed in future.
//       Number of RPCs in all the tests are not the constants and they can be changed in future.
TEST_F(PgCatalogPerfTest, StartupRPCCount) {
  const auto connector = [this] {
    RETURN_NOT_OK(Connect());
    return static_cast<Status>(Status::OK());
  };

  const auto first_connect_rpc_count = ASSERT_RESULT(MetricDeltas(connector)).read_rpc;
  ASSERT_EQ(first_connect_rpc_count, 5);
  const auto subsequent_connect_rpc_count = ASSERT_RESULT(MetricDeltas(connector)).read_rpc;
  ASSERT_EQ(subsequent_connect_rpc_count, 2);
}

// Test checks number of RPC in case of cache refresh without partitioned tables.
TEST_F(PgCatalogPerfTest, CacheRefreshRPCCountWithoutPartitionTables) {
  const auto cache_refresh_rpc_count = ASSERT_RESULT(CacheRefreshRPCCount());
  ASSERT_EQ(cache_refresh_rpc_count, 3);
}

// Test checks number of RPC in case of cache refresh with partitioned tables.
TEST_F(PgCatalogPerfTest, CacheRefreshRPCCountWithPartitionTables) {
  auto conn = ASSERT_RESULT(Connect());
  for (size_t ti = 0; ti < 3; ++ti) {
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE t$0 (r INT, v INT) PARTITION BY RANGE(r)", ti));
    for (size_t pi = 0; pi < 3; ++pi) {
      ASSERT_OK(conn.ExecuteFormat(
          "CREATE TABLE t$0_p$1 PARTITION OF t$0 FOR VALUES FROM ($2) TO ($3)",
          ti, pi, 100 * pi + 1, 100 * (pi + 1)));
    }
  }

  constexpr auto kTableWithCastInPartitioning = "t_with_cast";
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (d DATE, v INT) PARTITION BY RANGE(EXTRACT(month FROM d))",
      kTableWithCastInPartitioning));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0_p0 PARTITION OF $0 FOR VALUES FROM (1) TO (12)",
      kTableWithCastInPartitioning));

  const auto cache_refresh_rpc_count = ASSERT_RESULT(CacheRefreshRPCCount());
  ASSERT_EQ(cache_refresh_rpc_count, 6);
}

TEST_F(PgCatalogPerfTest, AfterCacheRefreshRPCCountOnInsert) {
  TestAfterCacheRefreshRPCCountOnInsert(/*expected_master_rpc_count=*/ 1);
}

TEST_F_EX(PgCatalogPerfTest,
          AfterCacheRefreshRPCCountOnInsertMinPreload,
          PgCatalogMinPreloadTest) {
  TestAfterCacheRefreshRPCCountOnInsert(/*expected_master_rpc_count=*/ 6);
}

TEST_F(PgCatalogPerfTest, AfterCacheRefreshRPCCountOnSelect) {
  TestAfterCacheRefreshRPCCountOnSelect(/*expected_master_rpc_count=*/ 3);
}

TEST_F_EX(PgCatalogPerfTest,
          AfterCacheRefreshRPCCountOnSelectMinPreload,
          PgCatalogMinPreloadTest) {
  TestAfterCacheRefreshRPCCountOnSelect(/*expected_master_rpc_count=*/ 11);
}

// The test checks number of hits in response cache in case of multiple connections and aggressive
// sys catalog changes. Which causes catalog cache refresh in each established connection.
TEST_F_EX(PgCatalogPerfTest, ResponseCacheEfficiency, PgCatalogWithUnlimitedCachePerfTest) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (r INT PRIMARY KEY)"));
  auto aux_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("ALTER TABLE t ADD COLUMN v INT"));
  std::vector<PGConn> conns;
  constexpr size_t kConnectionCount = 20;
  constexpr size_t kAlterTableCount = 10;
  const std::string select_all("SELECT * FROM t");
  for (size_t i = 0; i < kConnectionCount; ++i) {
    conns.push_back(ASSERT_RESULT(Connect()));
    ASSERT_RESULT(conns.back().Fetch(select_all));
  }
  ASSERT_RESULT(aux_conn.Fetch(select_all));
  const auto metrics = ASSERT_RESULT(MetricDeltas(
      [&conn, &conns] {
        for (size_t i = 0; i < kAlterTableCount; ++i) {
          RETURN_NOT_OK(conn.ExecuteFormat("ALTER TABLE t ADD COLUMN v_$0 INT", i));
          TestThreadHolder holder;
          size_t conn_idx = 0;
          for (auto& c : conns) {
            holder.AddThread([&c, idx = conn_idx, i] {
              ASSERT_OK(c.ExecuteFormat("INSERT INTO t VALUES($0)", idx * 100 + i));
            });
            ++conn_idx;
          }
        }
        return static_cast<Status>(Status::OK());
      }));
  constexpr auto kExpectedRows = kAlterTableCount * kConnectionCount;
  constexpr auto kExpectedColumns = kAlterTableCount + 2;
  ASSERT_OK(conn.FetchMatrix(select_all, kExpectedRows, kExpectedColumns));
  ASSERT_OK(aux_conn.FetchMatrix(select_all, kExpectedRows, kExpectedColumns));
  constexpr size_t kUniqueQueriesPerRefresh = 3;
  constexpr auto kUniqueQueries = kAlterTableCount * kUniqueQueriesPerRefresh;
  constexpr auto kTotalQueries = kConnectionCount * kUniqueQueries;
  ASSERT_EQ(metrics.cache_queries, kTotalQueries);
  ASSERT_EQ(metrics.cache_hits, kTotalQueries - kUniqueQueries);
  ASSERT_LE(metrics.read_rpc, 720);
}

TEST_F_EX(PgCatalogPerfTest,
          ResponseCacheEfficiencyInConnectionStart,
          PgCatalogWithUnlimitedCachePerfTest) {
  auto conn = ASSERT_RESULT(Connect());
  auto metrics = ASSERT_RESULT(MetricDeltas([this] {
    RETURN_NOT_OK(Connect());
    return static_cast<Status>(Status::OK());
  }));
  ASSERT_EQ(metrics.cache_queries, 4);
  ASSERT_EQ(metrics.cache_hits, 4);
}

// The test checks response cache renewing process in case of 'Snapshot too old' error.
// This error is possible in the following situation:
//   - several days ago at time T1 first connection was established to DB
//   - multiple (due to paging) responses for YSQL sys catalog cache were cached on a local tserver
//     by the PgResponseCache. These responses has catalog_read_time equal to T1
//   - later some (but not all) of these responses were discarded from the LRU cache
//     in the PgResponseCache
//   - new connection is establishing to same DB
//   - PgResponseCache provides cached response for initial request with read time T1
//   - PgResponseCache doesn't have cached responses for further requests with read time T1 and
//     send read request to a Master
//   - Master responds with 'Snapshot too old' error on attempt to read at really old read time T1
TEST_F_EX(PgCatalogPerfTest,
          ResponseCacheWithTooOldSnapshot,
          PgCatalogWithStaleResponseCacheTest) {
  auto connector = [this] {
    RETURN_NOT_OK(Connect());
    return static_cast<Status>(Status::OK());
  };

  auto first_connection_metrics = ASSERT_RESULT(MetricDeltas(connector));
  ASSERT_EQ(first_connection_metrics.cache_renew_hard, 0);
  ASSERT_EQ(first_connection_metrics.cache_renew_soft, 0);
  ASSERT_EQ(first_connection_metrics.cache_hits, 0);
  ASSERT_EQ(first_connection_metrics.cache_queries, 4);

  std::this_thread::sleep_for(std::chrono::milliseconds(
      2 * FLAGS_pg_cache_response_renew_soft_lifetime_limit_ms));

  auto second_connection_metrics = ASSERT_RESULT(MetricDeltas(connector));
  ASSERT_EQ(second_connection_metrics.cache_renew_hard, 0);
  ASSERT_EQ(second_connection_metrics.cache_renew_soft, 1);
  ASSERT_EQ(second_connection_metrics.cache_hits, 1);
  ASSERT_EQ(second_connection_metrics.cache_queries, 6);
}

// The test checks that GC keeps response cache memory lower than limit
TEST_F_EX(PgCatalogPerfTest, ResponseCacheMemoryLimit, PgCatalogWithLimitedCachePerfTest) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t(k SERIAL PRIMARY KEY, v INT)"));
  auto aux_conn = ASSERT_RESULT(Connect());
  constexpr size_t kAlterTableCount = 10;
  const auto metrics = ASSERT_RESULT(MetricDeltas(
      [&conn, &aux_conn] {
        for (size_t i = 0; i < kAlterTableCount; ++i) {
          RETURN_NOT_OK(conn.ExecuteFormat("ALTER TABLE t ADD COLUMN v_$0 INT", i));
          RETURN_NOT_OK(aux_conn.ExecuteFormat("INSERT INTO t(v) VALUES(1)"));
        }
        return static_cast<Status>(Status::OK());
      }));
  ASSERT_EQ(metrics.cache_gc_calls, 9);
  ASSERT_EQ(metrics.cache_entries_removed_by_gc, 26);
  auto response_cache_mem_tracker =
      cluster_->mini_tablet_server(0)->server()->mem_tracker()->FindChild("PgResponseCache");
  ASSERT_TRUE(response_cache_mem_tracker);
  const auto peak_consumption = response_cache_mem_tracker->peak_consumption();
  ASSERT_GT(peak_consumption, 0);
  ASSERT_LE(peak_consumption, FLAGS_pg_response_cache_size_bytes);
}

} // namespace yb::pgwrapper
