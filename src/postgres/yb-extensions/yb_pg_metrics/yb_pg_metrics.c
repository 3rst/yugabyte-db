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

#include "postgres.h"

#include <assert.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>

#include "access/htup_details.h"
#include "executor/instrument.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_database.h"
#include "common/ip.h"
#include "datatype/timestamp.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "parser/parsetree.h"
#include "pgstat.h"
#include "postmaster/bgworker.h"
#include "postmaster/postmaster.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/proc.h"
#include "storage/shmem.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/catcache.h"
#include "utils/datetime.h"
#include "utils/syscache.h"
#include "yb/yql/pggate/webserver/pgsql_webserver_wrapper.h"

#include "pg_yb_utils.h"

#define YSQL_METRIC_PREFIX "handler_latency_yb_ysqlserver_SQLProcessor_"
#define NumBackendStatSlots (MaxBackends + NUM_AUXPROCTYPES)

PG_MODULE_MAGIC;

typedef enum statementType
{
  Select,
  Insert,
  Delete,
  Update,
  Begin,
  Commit,
  Rollback,
  Other,
  Single_Shard_Transaction,
  SingleShardTransaction,
  Transaction,
  AggregatePushdown,
  CatCacheMisses,
  CatCacheMisses_Start,
  CatCacheMisses_0 = CatCacheMisses_Start,
  CatCacheMisses_1,
  CatCacheMisses_2,
  CatCacheMisses_3,
  CatCacheMisses_4,
  CatCacheMisses_5,
  CatCacheMisses_6,
  CatCacheMisses_7,
  CatCacheMisses_8,
  CatCacheMisses_9,
  CatCacheMisses_10,
  CatCacheMisses_11,
  CatCacheMisses_12,
  CatCacheMisses_13,
  CatCacheMisses_14,
  CatCacheMisses_15,
  CatCacheMisses_16,
  CatCacheMisses_17,
  CatCacheMisses_18,
  CatCacheMisses_19,
  CatCacheMisses_20,
  CatCacheMisses_21,
  CatCacheMisses_22,
  CatCacheMisses_23,
  CatCacheMisses_24,
  CatCacheMisses_25,
  CatCacheMisses_26,
  CatCacheMisses_27,
  CatCacheMisses_28,
  CatCacheMisses_29,
  CatCacheMisses_30,
  CatCacheMisses_31,
  CatCacheMisses_32,
  CatCacheMisses_33,
  CatCacheMisses_34,
  CatCacheMisses_35,
  CatCacheMisses_36,
  CatCacheMisses_37,
  CatCacheMisses_38,
  CatCacheMisses_39,
  CatCacheMisses_40,
  CatCacheMisses_41,
  CatCacheMisses_42,
  CatCacheMisses_43,
  CatCacheMisses_44,
  CatCacheMisses_45,
  CatCacheMisses_46,
  CatCacheMisses_47,
  CatCacheMisses_48,
  CatCacheMisses_49,
  CatCacheMisses_50,
  CatCacheMisses_51,
  CatCacheMisses_52,
  CatCacheMisses_53,
  CatCacheMisses_54,
  CatCacheMisses_55,
  CatCacheMisses_56,
  CatCacheMisses_57,
  CatCacheMisses_58,
  CatCacheMisses_59,
  CatCacheMisses_60,
  CatCacheMisses_61,
  CatCacheMisses_62,
  CatCacheMisses_63,
  CatCacheMisses_64,
  CatCacheMisses_65,
  CatCacheMisses_66,
  CatCacheMisses_67,
  CatCacheMisses_68,
  CatCacheMisses_69,
  CatCacheMisses_70,
  CatCacheMisses_71,
  CatCacheMisses_72,
  CatCacheMisses_73,
  CatCacheMisses_74,
  CatCacheMisses_75,
  CatCacheMisses_76,
  CatCacheMisses_77,
  CatCacheMisses_78,
  CatCacheMisses_79,
  CatCacheMisses_80,
  CatCacheMisses_81,
  CatCacheMisses_82,
  CatCacheMisses_83,
  CatCacheMisses_84,
  CatCacheMisses_End,
  kMaxStatementType
} statementType;
int num_entries = kMaxStatementType;
ybpgmEntry *ybpgm_table = NULL;

/* Statement nesting level is used when setting up dml statements.
 * - Some state variables are set up for the top-level query but not the nested query.
 * - Time recorder is initialized and used for top-level query only.
 */
static int statement_nesting_level = 0;

/* Block nesting level is used when setting up execution block such as "DO $$ ... END $$;".
 * - Some state variables are set up for the top level block but not the nested blocks.
 */
static int block_nesting_level = 0;

/*
 * Flag to determine whether a transaction block has been entered.
 */
static bool is_inside_transaction_block = false;

/*
 * Flag to determine whether a DML or Other statement type has been executed.
 * Multiple statements will count as a single transaction within a transaction block.
 * DDL statements which are autonomous will be counted as its own transaction
 * even within a transaction block.
 */
static bool is_statement_executed = false;

char *metric_node_name = NULL;
struct WebserverWrapper *webserver = NULL;
int port = 0;
static bool log_accesses = false;
static bool log_tcmalloc_stats = false;
static int webserver_profiler_sample_freq_bytes = 0;
static int num_backends = 0;
static rpczEntry *rpcz = NULL;
static MemoryContext ybrpczMemoryContext = NULL;
PgBackendStatus *backendStatusArray = NULL;
extern int MaxConnections;

static long last_cache_misses_val = 0;
static long last_cache_id_misses_val[SysCacheSize] = {0};

static volatile sig_atomic_t got_SIGHUP = false;
static volatile sig_atomic_t got_SIGTERM = false;

void		_PG_init(void);
/*
 * Variables used for storing the previous values of used hooks.
 */
static shmem_request_hook_type prev_shmem_request_hook = NULL;
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;
static ExecutorStart_hook_type prev_ExecutorStart = NULL;
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;
static ExecutorRun_hook_type prev_ExecutorRun = NULL;
static ExecutorFinish_hook_type prev_ExecutorFinish = NULL;
static ProcessUtility_hook_type prev_ProcessUtility = NULL;

static void set_metric_names(void);
static void ybpgm_shmem_request(void);
static void ybpgm_startup_hook(void);
static Size ybpgm_memsize(void);
static bool isTopLevelStatement(void);
static void ybpgm_ExecutorStart(QueryDesc *queryDesc, int eflags);
static void ybpgm_ExecutorRun(QueryDesc *queryDesc, ScanDirection direction, uint64 count,
                              bool execute_once);
static void ybpgm_ExecutorFinish(QueryDesc *queryDesc);
static void ybpgm_ExecutorEnd(QueryDesc *queryDesc);
static void ybpgm_ProcessUtility(PlannedStmt *pstmt, const char *queryString, bool readOnlyTree,
                                 ProcessUtilityContext context,
                                 ParamListInfo params, QueryEnvironment *queryEnv,
                                 DestReceiver *dest, QueryCompletion *qc);
static void ybpgm_Store(statementType type, uint64_t time, uint64_t rows);
static void ybpgm_StoreCount(statementType type, uint64_t time, uint64_t count);

static void ws_sighup_handler(SIGNAL_ARGS);
static void ws_sigterm_handler(SIGNAL_ARGS);

/*
 * Function used for checking if the current statement is a top level statement.
 */
bool
isTopLevelStatement(void)
{
  return statement_nesting_level == 0;
}

static void
IncStatementNestingLevel(void)
{
  statement_nesting_level++;
}

static void
DecStatementNestingLevel(void)
{
  statement_nesting_level--;
}

bool
isTopLevelBlock(void)
{
  return block_nesting_level == 0;
}

static void
IncBlockNestingLevel(void)
{
  block_nesting_level++;
}

static void
DecBlockNestingLevel(void)
{
  block_nesting_level--;
}

void
set_metric_names(void)
{
  strcpy(ybpgm_table[Select].name, YSQL_METRIC_PREFIX "SelectStmt");
  strcpy(ybpgm_table[Insert].name, YSQL_METRIC_PREFIX "InsertStmt");
  strcpy(ybpgm_table[Delete].name, YSQL_METRIC_PREFIX "DeleteStmt");
  strcpy(ybpgm_table[Update].name, YSQL_METRIC_PREFIX "UpdateStmt");
  strcpy(ybpgm_table[Begin].name, YSQL_METRIC_PREFIX "BeginStmt");
  strcpy(ybpgm_table[Commit].name, YSQL_METRIC_PREFIX "CommitStmt");
  strcpy(ybpgm_table[Rollback].name, YSQL_METRIC_PREFIX "RollbackStmt");
  strcpy(ybpgm_table[Other].name, YSQL_METRIC_PREFIX "OtherStmts");
  // Deprecated. Names with "_"s may cause confusion to metric conumsers.
  strcpy(
      ybpgm_table[Single_Shard_Transaction].name, YSQL_METRIC_PREFIX "Single_Shard_Transactions");

  strcpy(ybpgm_table[SingleShardTransaction].name, YSQL_METRIC_PREFIX "SingleShardTransactions");
  strcpy(ybpgm_table[Transaction].name, YSQL_METRIC_PREFIX "Transactions");
  strcpy(ybpgm_table[AggregatePushdown].name, YSQL_METRIC_PREFIX "AggregatePushdowns");
  strcpy(ybpgm_table[CatCacheMisses].name, YSQL_METRIC_PREFIX "CatalogCacheMisses");
  for (int i = CatCacheMisses_Start; i < CatCacheMisses_End; ++i)
  {
	int cache_id = i - CatCacheMisses_Start;
	char index_name[NAMEDATALEN + 16];
	strcpy(ybpgm_table[i].name, YSQL_METRIC_PREFIX "CatalogCacheMisses");
	sprintf(index_name, "_%d_%s",
			cache_id, YbGetCatalogCacheIndexName(cache_id));
	Assert(strlen(ybpgm_table[i].name) + strlen(index_name) <
		   sizeof(ybpgm_table[i].name));
	strcat(ybpgm_table[i].name, index_name);
  }
}

/*
 * Function to calculate milliseconds elapsed from start_time to stop_time.
 */
int64
getElapsedMs(TimestampTz start_time, TimestampTz stop_time)
{
  long secs;
  int microsecs;

  TimestampDifference(start_time, stop_time, &secs, &microsecs);

  long millisecs = (secs * 1000) + (microsecs / 1000);
  return millisecs;
}

void
pullRpczEntries(void)
{
  ybrpczMemoryContext = AllocSetContextCreate(TopMemoryContext,
                                             "YB RPCz memory context",
                                             ALLOCSET_SMALL_SIZES);

  MemoryContext oldcontext = MemoryContextSwitchTo(ybrpczMemoryContext);
  rpcz = (rpczEntry *) palloc(sizeof(rpczEntry) * NumBackendStatSlots);

  num_backends = NumBackendStatSlots;
  volatile PgBackendStatus *beentry = backendStatusArray;

  for (int i = 0; i < NumBackendStatSlots; i++)
  {
    /* To prevent locking overhead, the BackendStatusArray in postgres maintains a st_changecount
     * field for each entry. This field is incremented once before a backend starts modifying the
     * entry, and once after it is done modifying the entry. So, we check if st_changecount changes
     * while we're copying the entry or if its odd. The check for odd is needed for when a backend
     * has begun changing the entry but hasn't finished.
     */
    int attempt = 1;
    while (yb_pgstat_log_read_activity(beentry, ++attempt))
    {
      int			before_changecount;
      int			after_changecount;

      before_changecount = beentry->st_changecount;

      rpcz[i].proc_id = beentry->st_procpid;

      /* avoid filling any more fields if invalid */
      if (beentry->st_procpid <= 0) {
        break;
      }

      rpcz[i].db_oid = beentry->st_databaseid;

      rpcz[i].query = (char *) palloc(pgstat_track_activity_query_size);
      strcpy(rpcz[i].query, (char *) beentry->st_activity_raw);

      rpcz[i].application_name = (char *) palloc(NAMEDATALEN);
      strcpy(rpcz[i].application_name, (char *) beentry->st_appname);

      rpcz[i].db_name = (char *) palloc(NAMEDATALEN);
      strcpy(rpcz[i].db_name, beentry->st_databasename);

      rpcz[i].process_start_timestamp = beentry->st_proc_start_timestamp;
      rpcz[i].transaction_start_timestamp = beentry->st_xact_start_timestamp;
      rpcz[i].query_start_timestamp = beentry->st_activity_start_timestamp;

      rpcz[i].backend_type = (char *) palloc(40);
      strcpy(rpcz[i].backend_type, GetBackendTypeDesc(beentry->st_backendType));

      rpcz[i].backend_active = 0;
      rpcz[i].backend_status = (char *) palloc(30);
      switch (beentry->st_state) {
        case STATE_IDLE:
          strcpy(rpcz[i].backend_status, "idle");
          break;
        case STATE_RUNNING:
          rpcz[i].backend_active = 1;
          strcpy(rpcz[i].backend_status, "active");
          break;
        case STATE_IDLEINTRANSACTION:
          strcpy(rpcz[i].backend_status, "idle in transaction");
          break;
        case STATE_FASTPATH:
          rpcz[i].backend_active = 1;
          strcpy(rpcz[i].backend_status, "fastpath function call");
          break;
        case STATE_IDLEINTRANSACTION_ABORTED:
          strcpy(rpcz[i].backend_status, "idle in transaction (aborted)");
          break;
        case STATE_DISABLED:
          strcpy(rpcz[i].backend_status, "disabled");
          break;
        case STATE_UNDEFINED:
          strcpy(rpcz[i].backend_status, "");
          break;
      }

      char remote_host[NI_MAXHOST];
      char remote_port[NI_MAXSERV];
      int ret;

      remote_host[0] = '\0';
      remote_port[0] = '\0';
      ret = pg_getnameinfo_all((struct sockaddr_storage *) &beentry->st_clientaddr.addr,
                               beentry->st_clientaddr.salen,
                               remote_host, sizeof(remote_host),
                               remote_port, sizeof(remote_port),
                               NI_NUMERICHOST | NI_NUMERICSERV);
      if (ret == 0)
      {
        rpcz[i].host = (char *) palloc(NI_MAXHOST);
        rpcz[i].port = (char *) palloc(NI_MAXSERV);
        clean_ipv6_addr(beentry->st_clientaddr.addr.ss_family, remote_host);
        strcpy(rpcz[i].host, remote_host);
        strcpy(rpcz[i].port, remote_port);
      }
      else
      {
        rpcz[i].host = NULL;
        rpcz[i].port = NULL;
      }
      after_changecount = beentry->st_changecount;

      if (before_changecount == after_changecount &&
          (before_changecount & 1) == 0)
        break;
    }
    beentry++;
  }
  MemoryContextSwitchTo(oldcontext);
}

void
freeRpczEntries(void)
{
  MemoryContextDelete(ybrpczMemoryContext);
  ybrpczMemoryContext = NULL;
}

/* SIGHUP: set flag to re-read config file at next convenient time */
void
ws_sighup_handler(SIGNAL_ARGS) {
	int			save_errno = errno;

	got_SIGHUP = true;
	SetLatch(MyLatch);

	errno = save_errno;
}


/* SIGTERM: time to die */
static void
ws_sigterm_handler(SIGNAL_ARGS)
{
	int			save_errno = errno;

	got_SIGTERM = true;
	SetLatch(MyLatch);

	errno = save_errno;
}

/*
 * Function that is executed when the YSQL webserver process is started.
 * We don't use the argument "unused", however, a postgres background worker's function
 * is required to have an argument of type Datum.
 */
void
webserver_worker_main(Datum unused)
{
  YBCInitThreading();
  /*
   * We call YBCInit here so that HandleYBStatus can correctly report potential error.
   */
  HandleYBStatus(YBCInit(NULL /* argv[0] */, palloc, NULL /* cstring_to_text_with_len_fn */));

  backendStatusArray = getBackendStatusArray();

  BackgroundWorkerUnblockSignals();

  /*
   * Assert that shared memory is allocated to backendStatusArray before this webserver
   * is started.
  */
  if (!backendStatusArray)
    ereport(FATAL,
            (errcode(ERRCODE_INTERNAL_ERROR),
             errmsg("Shared memory not allocated to BackendStatusArray before starting YSQL webserver")));

  webserver = CreateWebserver(ListenAddresses, port);

  RegisterMetrics(ybpgm_table, num_entries, metric_node_name);

  postgresCallbacks callbacks;
  callbacks.pullRpczEntries      = pullRpczEntries;
  callbacks.freeRpczEntries      = freeRpczEntries;
  callbacks.getTimestampTz       = GetCurrentTimestamp;
  callbacks.getTimestampTzDiffMs = getElapsedMs;
  callbacks.getTimestampTzToStr  = timestamptz_to_str;

  YbConnectionMetrics conn_metrics;
  conn_metrics.max_conn = &MaxConnections;
  conn_metrics.too_many_conn = yb_too_many_conn;
  conn_metrics.new_conn = yb_new_conn;

  RegisterRpczEntries(&callbacks, &num_backends, &rpcz, &conn_metrics);
  HandleYBStatus(StartWebserver(webserver));

	pqsignal(SIGHUP, ws_sighup_handler);
	pqsignal(SIGTERM, ws_sigterm_handler);

  SetWebserverConfig(webserver, log_accesses, log_tcmalloc_stats,
                     webserver_profiler_sample_freq_bytes);

  int rc;
  while (!got_SIGTERM)
  {
    rc = WaitLatch(MyLatch, WL_LATCH_SET | WL_POSTMASTER_DEATH, -1, PG_WAIT_EXTENSION);
    ResetLatch(MyLatch);

		if (rc & WL_POSTMASTER_DEATH)
			break;

    if (got_SIGHUP)
    {
      got_SIGHUP = false;
      ProcessConfigFile(PGC_SIGHUP);
      SetWebserverConfig(webserver, log_accesses, log_tcmalloc_stats,
                         webserver_profiler_sample_freq_bytes);
    }
  }

  if (rpcz != NULL && ybrpczMemoryContext != NULL)
  {
    MemoryContext oldcontext = MemoryContextSwitchTo(ybrpczMemoryContext);
    pfree(rpcz);
    MemoryContextSwitchTo(oldcontext);
  }

	if (webserver)
	{
		DestroyWebserver(webserver);
		webserver = NULL;
	}

  if (rc & WL_POSTMASTER_DEATH)
      proc_exit(1);

  proc_exit(0);
}

/*
 * Module load callback
 */
void
_PG_init(void)
{
  if (!process_shared_preload_libraries_in_progress)
    return;

  /*
   * Parameters that we expect to receive from the tserver process when it starts up postmaster.
   * We set the flags GUC_NO_SHOW_ALL, GUC_NO_RESET_ALL, GUC_NOT_IN_SAMPLE, GUC_DISALLOW_IN_FILE
   * so that these parameters aren’t visible, resettable, or configurable by our end user.
   */
  DefineCustomStringVariable("yb_pg_metrics.node_name", "Node name for YB metrics", NULL,
                             &metric_node_name, "", PGC_POSTMASTER,
                             GUC_NO_SHOW_ALL | GUC_NO_RESET_ALL | GUC_NOT_IN_SAMPLE
                             | GUC_DISALLOW_IN_FILE,
                             NULL, NULL, NULL);

  DefineCustomIntVariable("yb_pg_metrics.port", "Port for YSQL webserver", NULL,
                          &port, 0, 0, INT_MAX, PGC_POSTMASTER,
                          GUC_NO_SHOW_ALL | GUC_NO_RESET_ALL | GUC_NOT_IN_SAMPLE
                          | GUC_DISALLOW_IN_FILE,
                          NULL, NULL, NULL);

  DefineCustomBoolVariable("yb_pg_metrics.log_accesses",
                          "Log each request received by the YSQL webserver",
                          NULL, &log_accesses, false, PGC_SUSET, 0,
                          NULL, NULL, NULL);

  DefineCustomBoolVariable("yb_pg_metrics.log_tcmalloc_stats",
                          "Log each request received by the YSQL webserver",
                          NULL, &log_tcmalloc_stats, false, PGC_SUSET, 0,
                          NULL, NULL, NULL);

  DefineCustomIntVariable("yb_pg_metrics.webserver_profiler_sample_freq_bytes",
                          "The frequency at which Google TCMalloc should sample allocations in the "
                          "YSQL webserver. If this is 0, sampling is disabled. ",
                          NULL, &webserver_profiler_sample_freq_bytes, 1024 * 1024, 0, INT_MAX,
                          PGC_SUSET, 0, NULL, NULL, NULL);

  BackgroundWorker worker;

  /* Registering the YSQL webserver as a background worker */
  MemSet(&worker, 0, sizeof(BackgroundWorker));
  strcpy(worker.bgw_name, "YSQL webserver");
  worker.bgw_flags = BGWORKER_SHMEM_ACCESS;
  worker.bgw_start_time = BgWorkerStart_PostmasterStart;
  /* Value of 1 allows the background worker for webserver to restart */
  worker.bgw_restart_time = 1;
  worker.bgw_main_arg = (Datum) 0;
  strcpy(worker.bgw_library_name, "yb_pg_metrics");
  strcpy(worker.bgw_function_name, "webserver_worker_main");
  worker.bgw_notify_pid = 0;
  if (getenv("FLAGS_yb_webserver_oom_score_adj") != NULL)
    strncpy(worker.bgw_oom_score_adj, getenv("FLAGS_yb_webserver_oom_score_adj"), BGW_MAXLEN);

  RegisterBackgroundWorker(&worker);
  /*
   * Set the value of the hooks.
   */
  
  prev_shmem_request_hook = shmem_request_hook;
	shmem_request_hook = ybpgm_shmem_request;

  prev_shmem_startup_hook = shmem_startup_hook;
  shmem_startup_hook = ybpgm_startup_hook;

  prev_ExecutorStart = ExecutorStart_hook;
  ExecutorStart_hook = ybpgm_ExecutorStart;

  prev_ExecutorRun = ExecutorRun_hook;
  ExecutorRun_hook = ybpgm_ExecutorRun;

  prev_ExecutorFinish = ExecutorFinish_hook;
  ExecutorFinish_hook = ybpgm_ExecutorFinish;

  prev_ExecutorEnd = ExecutorEnd_hook;
  ExecutorEnd_hook = ybpgm_ExecutorEnd;

  prev_ProcessUtility = ProcessUtility_hook;
  ProcessUtility_hook = ybpgm_ProcessUtility;
  static_assert(SysCacheSize == CatCacheMisses_End - CatCacheMisses_Start,
				"Wrong catalog cache number");
}

/*
 * shmem_request hook: request additional shared resources.  We'll allocate or
 * attach to the shared resources in ybpgm_startup_hook().
 */
static void
ybpgm_shmem_request(void)
{
	if (prev_shmem_request_hook)
		prev_shmem_request_hook();

	RequestAddinShmemSpace(ybpgm_memsize());
	RequestNamedLWLockTranche("yb_pg_metrics", 1);
}

/*
 * Allocate or attach to shared memory.
 */
static void
ybpgm_startup_hook(void)
{
  if (prev_shmem_startup_hook)
    prev_shmem_startup_hook();

  bool found;

  ybpgm_table = ShmemInitStruct("yb_pg_metrics",
                                 num_entries * sizeof(struct ybpgmEntry),
                                 &found);
  set_metric_names();
}

static void
ybpgm_ExecutorStart(QueryDesc *queryDesc, int eflags)
{
  /* Each PORTAL execution will run the following steps.
   * 1- ExecutorStart()
   * 2- Execute statements in the portal.
   *    Some statement execution (CURSOR execution) can open a nested PORTAL. Our metric routines
   *    will ignore the nested PORTAL for now.
   * 3- ExecutorEnd()
   */
  if (prev_ExecutorStart)
    prev_ExecutorStart(queryDesc, eflags);
  else
    standard_ExecutorStart(queryDesc, eflags);

  /* PORTAL run can be nested inside another PORTAL, and we only run metric routines for the top
   * level portal statement. The current design of using global variable "statement_nesting_level"
   * is very flawed as it cannot find the starting and ending point of a top statement execution.
   * For now, as a workaround, "queryDesc" attribute is used as an indicator for logging metric.
   * Whenever "time value" is not null, it is logged at the end of a portal run.
   * - When starting, we allocate "queryDesc->totaltime".
   * - When ending, we check for "queryDesc->totaltime". If not null, its metric is log.
   */
  if (isTopLevelStatement() && !queryDesc->totaltime)
  {
    MemoryContext oldcxt;
    oldcxt = MemoryContextSwitchTo(queryDesc->estate->es_query_cxt);
    queryDesc->totaltime = InstrAlloc(1, INSTRUMENT_TIMER, false);
    MemoryContextSwitchTo(oldcxt);
  }
}

static void
ybpgm_ExecutorRun(QueryDesc *queryDesc, ScanDirection direction, uint64 count,
                 bool execute_once)
{
  IncStatementNestingLevel();
  PG_TRY();
  {
    if (prev_ExecutorRun)
      prev_ExecutorRun(queryDesc, direction, count, execute_once);
    else
      standard_ExecutorRun(queryDesc, direction, count, execute_once);
    DecStatementNestingLevel();
  }
  PG_CATCH();
  {
    DecStatementNestingLevel();
    PG_RE_THROW();
  }
  PG_END_TRY();
}

static void
ybpgm_ExecutorFinish(QueryDesc *queryDesc)
{
  IncStatementNestingLevel();
  PG_TRY();
  {
    if (prev_ExecutorFinish)
      prev_ExecutorFinish(queryDesc);
    else
      standard_ExecutorFinish(queryDesc);
    DecStatementNestingLevel();
  }
  PG_CATCH();
  {
    DecStatementNestingLevel();
    PG_RE_THROW();
  }
  PG_END_TRY();
}

static void
ybpgm_ExecutorEnd(QueryDesc *queryDesc)
{
  statementType type;

  switch (queryDesc->operation) {
    case CMD_SELECT:
      type = Select;
      break;
    case CMD_INSERT:
      type = Insert;
      break;
    case CMD_DELETE:
      type = Delete;
      break;
    case CMD_UPDATE:
      type = Update;
      break;
    default:
      type = Other;
      break;
  }

  is_statement_executed = true;

  /* Collecting metric.
   * - Only processing metric for top level statement in top level portal.
   *   For example, CURSOR execution can have many nested portal and nested statement. The metric
   *   for all of the nested items are not processed.
   * - However, it's difficult to know the starting and ending point of a statement, we check for
   *   not null "queryDesc->totaltime".
   * - The design for this metric module for using global state variables is very flawed, so we
   *   use this not-null check for now.
   */
  if (isTopLevelStatement() && queryDesc->totaltime) {
	InstrEndLoop(queryDesc->totaltime);
	const uint64_t time = (uint64_t) (queryDesc->totaltime->total * 1000000.0);
	const uint64 rows_count = queryDesc->estate->es_processed;

	ybpgm_Store(type, time, rows_count);

	if (queryDesc->estate->yb_es_is_single_row_modify_txn)
	{
		ybpgm_Store(Single_Shard_Transaction, time, rows_count);
		ybpgm_Store(SingleShardTransaction, time, rows_count);
	}

	if (!is_inside_transaction_block)
	  ybpgm_Store(Transaction, time, rows_count);

	if (IsA(queryDesc->planstate, AggState) &&
		castNode(AggState, queryDesc->planstate)->yb_pushdown_supported)
	ybpgm_Store(AggregatePushdown, time, rows_count);

	long current_cache_misses = YbGetCatCacheMisses();
	long* current_cache_id_misses = YbGetCatCacheIdMisses();

	long total_delta = current_cache_misses - last_cache_misses_val;
	last_cache_misses_val = current_cache_misses;

	/* Currently we set the time parameter to 0 as we don't have metrics
	 * for that available
	 * TODO: Get timing metrics for catalog cache misses
	 */
	ybpgm_StoreCount(CatCacheMisses, 0, total_delta);
	if (total_delta > 0)
		for (int i = CatCacheMisses_Start; i < CatCacheMisses_End; ++i)
		{
			int j = i - CatCacheMisses_Start;
			ybpgm_StoreCount(i, 0, current_cache_id_misses[j] - last_cache_id_misses_val[j]);
			last_cache_id_misses_val[j] = current_cache_id_misses[j];
		}
  }

  IncStatementNestingLevel();
  PG_TRY();
  {
    if (prev_ExecutorEnd)
      prev_ExecutorEnd(queryDesc);
    else
      standard_ExecutorEnd(queryDesc);
    DecStatementNestingLevel();
  }
  PG_CATCH();
  {
    DecStatementNestingLevel();
    PG_RE_THROW();
  }
  PG_END_TRY();
}

/*
 * Estimate shared memory space needed.
 */
static Size
ybpgm_memsize(void)
{
  Size		size;

  size = MAXALIGN(num_entries * sizeof(struct ybpgmEntry));

  return size;
}

/*
 * Get the statement type for a transactional statement.
 */
static statementType ybpgm_getStatementType(TransactionStmt *stmt) {
  statementType type = Other;
  switch (stmt->kind) {
    case TRANS_STMT_BEGIN:
    case TRANS_STMT_START:
      type = Begin;
      break;
    case TRANS_STMT_COMMIT:
    case TRANS_STMT_COMMIT_PREPARED:
      type = Commit;
      break;
    case TRANS_STMT_ROLLBACK:
    case TRANS_STMT_ROLLBACK_TO:
    case TRANS_STMT_ROLLBACK_PREPARED:
      type = Rollback;
      break;
    case TRANS_STMT_SAVEPOINT:
    case TRANS_STMT_RELEASE:
    case TRANS_STMT_PREPARE:
      type = Other;
      break;
    default:
      elog(ERROR, "unrecognized statement kind: %d", stmt->kind);
  }
  return type;
}

/*
 * Hook used for tracking "Other" statements.
 */
static void
ybpgm_ProcessUtility(PlannedStmt *pstmt, const char *queryString, bool readOnlyTree,
                     ProcessUtilityContext context,
                     ParamListInfo params, QueryEnvironment *queryEnv,
                     DestReceiver *dest, QueryCompletion *qc)
{
  if (isTopLevelBlock() && !IsA(pstmt->utilityStmt, ExecuteStmt) &&
      !IsA(pstmt->utilityStmt, PrepareStmt) && !IsA(pstmt->utilityStmt, DeallocateStmt))
  {
    instr_time start;
    instr_time end;
    statementType type;

    if (IsA(pstmt->utilityStmt, TransactionStmt)) {
      TransactionStmt *stmt = (TransactionStmt *)(pstmt->utilityStmt);
      type = ybpgm_getStatementType(stmt);
    } else {
      type = Other;
    }

    INSTR_TIME_SET_CURRENT(start);

    IncBlockNestingLevel();
    PG_TRY();
    {
      if (prev_ProcessUtility)
        prev_ProcessUtility(pstmt, queryString, readOnlyTree,
                            context, params, queryEnv,
                            dest, qc);
      else
        standard_ProcessUtility(pstmt, queryString, readOnlyTree,
                                context, params, queryEnv,
                                dest, qc);
      DecBlockNestingLevel();
    }
    PG_CATCH();
    {
      DecBlockNestingLevel();
      PG_RE_THROW();
    }
    PG_END_TRY();

    INSTR_TIME_SET_CURRENT(end);
    INSTR_TIME_SUBTRACT(end, start);

    YbDdlModeOptional ddl_mode = YbGetDdlMode(pstmt, context);
    if (ddl_mode.has_value)
    {
      ybpgm_Store(Transaction, INSTR_TIME_GET_MICROSEC(end), 0);
    }
    else if (type == Other)
    {
      is_statement_executed = true;
    }

    if (type == Begin && !is_inside_transaction_block)
    {
      is_inside_transaction_block = true;
      is_statement_executed = false;
    }
    if (type == Rollback)
    {
      is_inside_transaction_block = false;
      is_statement_executed = false;
    }
    /*
     * TODO: Once savepoint and rollback to specific transaction are supported,
     * transaction block counter needs to be revisited.
     * Current logic is to increment non-empty transaction block by 1
     * if non-DDL statement types executed prior to committing.
     */
    if (type == Commit) {
      if (qc->commandTag != CMDTAG_ROLLBACK &&
          is_inside_transaction_block &&
          is_statement_executed)
      {
        ybpgm_Store(Transaction, INSTR_TIME_GET_MICROSEC(end), 0);
      }
      is_inside_transaction_block = false;
      is_statement_executed = false;
    }

    ybpgm_Store(type, INSTR_TIME_GET_MICROSEC(end), 0 /* rows */);
  }
  else
  {
    if (prev_ProcessUtility)
      prev_ProcessUtility(pstmt, queryString, readOnlyTree,
                          context, params, queryEnv,
                          dest, qc);
    else
      standard_ProcessUtility(pstmt, queryString, readOnlyTree,
                              context, params, queryEnv,
                              dest, qc);
  }
}

static void
ybpgm_Store(statementType type, uint64_t time, uint64_t rows) {
  struct ybpgmEntry *entry = &ybpgm_table[type];
  entry->total_time += time;
  entry->calls += 1;
  entry->rows += rows;
}

static void
ybpgm_StoreCount(statementType type, uint64_t time, uint64_t count) {
  struct ybpgmEntry *entry = &ybpgm_table[type];
  entry->total_time += time;
  entry->calls += count;
  entry->rows += count;
}
