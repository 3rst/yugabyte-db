#ifndef __PG_STAT_MONITOR_H__
#define __PG_STAT_MONITOR_H__

#include "postgres.h"

#include <arpa/inet.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "access/hash.h"
#include "catalog/pg_authid.h"
#include "executor/instrument.h"
#include "common/ip.h"
#include "funcapi.h"
#include "access/twophase.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "optimizer/planner.h"
#include "postmaster/bgworker.h"
#include "parser/analyze.h"
#include "parser/parsetree.h"
#include "parser/scanner.h"
#include "parser/scansup.h"
#include "pgstat.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "storage/spin.h"
#include "tcop/utility.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/timestamp.h"
#include "utils/lsyscache.h"
#include "utils/guc.h"

#define IsHashInitialize()	(pgss || pgss_hash || pgss_object_hash || pgss_agghash || pgss_buckethash || pgss_waiteventshash)

#define MAX_BACKEND_PROCESES (MaxBackends + NUM_AUXILIARY_PROCS + max_prepared_xacts)

/* Time difference in miliseconds */
#define	TIMEVAL_DIFF(start, end) (((double) end.tv_sec + (double) end.tv_usec / 1000000.0) \
	- ((double) start.tv_sec + (double) start.tv_usec / 1000000.0)) * 1000

#define  ArrayGetTextDatum(x) array_get_datum(x)

/* XXX: Should USAGE_EXEC reflect execution time and/or buffer usage? */
#define USAGE_EXEC(duration)	(1.0)
#define USAGE_INIT				(1.0)	/* including initial planning */
#define ASSUMED_MEDIAN_INIT		(10.0)	/* initial assumed median usage */
#define ASSUMED_LENGTH_INIT		1024	/* initial assumed mean query length */
#define USAGE_DECREASE_FACTOR	(0.99)	/* decreased every entry_dealloc */
#define STICKY_DECREASE_FACTOR	(0.50)	/* factor for sticky entries */
#define USAGE_DEALLOC_PERCENT	5	/* free this % of entries at once */

#define JUMBLE_SIZE				1024	/* query serialization buffer size */

#define MAX_RESPONSE_BUCKET 10
#define MAX_REL_LEN			255
#define MAX_BUCKETS			10
#define MAX_OBJECT_CACHE	100
#define TEXT_LEN			255

typedef struct GucVariables
{
	int		guc_variable;
	char	guc_name[TEXT_LEN];
	char	guc_desc[TEXT_LEN];
	int		guc_default;
	int		guc_min;
	int		guc_max;
	bool 	guc_restart;
} GucVariable;
typedef enum pgssStoreKind
{
	PGSS_INVALID = -1,

	/*
	 * PGSS_PLAN and PGSS_EXEC must be respectively 0 and 1 as they're used to
	 * reference the underlying values in the arrays in the Counters struct,
	 * and this order is required in pg_stat_statements_internal().
	 */
	PGSS_PLAN = 0,
	PGSS_EXEC,

	PGSS_NUMKIND				/* Must be last value of this enum */
} pgssStoreKind;


/*
 * Type of aggregate keys
 */
typedef enum AGG_KEY
{
	AGG_KEY_DATABASE = 0,
	AGG_KEY_USER,
	AGG_KEY_HOST
} AGG_KEY;

/* Bucket shared_memory storage */
typedef struct pgssBucketHashKey
{
	uint64		bucket_id;			/* bucket number */
} pgssBucketHashKey;

typedef struct pgssBucketCounters
{
	Timestamp			current_time;   /* start time of the bucket */
	int					resp_calls[MAX_RESPONSE_BUCKET];	/* execution time's in msec */
}pgssBucketCounters;

typedef struct pgssBucketEntry
{
	pgssBucketHashKey	key;			/* hash key of entry - MUST BE FIRST */
	pgssBucketCounters  counters;
	slock_t				mutex;			/* protects the counters only */
}pgssBucketEntry;

/* Objects shared memory storage */
typedef struct pgssObjectHashKey
{
	uint64		queryid;		/* query id */
} pgssObjectHashKey;

typedef struct pgssObjectEntry
{
	pgssObjectHashKey	key;						/* hash key of entry - MUST BE FIRST */
	char				tables_name[MAX_REL_LEN];   /* table names involved in the query */
	slock_t				mutex;						/* protects the counters only */
} pgssObjectEntry;

/* Aggregate shared memory storage */
typedef struct pgssAggHashKey
{
	uint64		id;				/* dbid, userid or ip depend upon the type */
	uint64		type;			/* type of id dbid, userid or ip */
	uint64		queryid;		/* query identifier, foreign key to the query */
	uint64		bucket_id;		/* bucket_id is the foreign key to pgssBucketHashKey */
} pgssAggHashKey;

typedef struct pgssAggCounters
{
	uint64		total_calls;		/* number of quries per database/user/ip */
} pgssAggCounters;

typedef struct pgssAggEntry
{
	pgssAggHashKey	key;			/* hash key of entry - MUST BE FIRST */
	pgssAggCounters	counters;		/* the statistics aggregates */
	slock_t			mutex;			/* protects the counters only */
} pgssAggEntry;


typedef struct pgssWaitEventKey
{
	uint64		processid;
} pgssWaitEventKey;

#define MAX_QUERY_LEN 1024
typedef struct pgssWaitEventEntry
{
	pgssAggHashKey	key;			/* hash key of entry - MUST BE FIRST */
	uint64			queryid;
	uint64			pid;
	uint32 			wait_event_info;
	char			query[MAX_QUERY_LEN];
	slock_t			mutex;			/* protects the counters only */
} pgssWaitEventEntry;


/* shared nenory storage for the query */
typedef struct pgssHashKey
{
	uint64		bucket_id;		/* bucket number */
	uint64		queryid;		/* query identifier */
	Oid			userid;			/* user OID */
	Oid			dbid;			/* database OID */
} pgssHashKey;

typedef struct QueryInfo
{
	uint64		queryid;					/* query identifier */
	Oid			userid;						/* user OID */
	Oid			dbid;						/* database OID */
	uint		host;						/* client IP */
	char		tables_name[MAX_REL_LEN];   /* table names involved in the query */
} QueryInfo;

typedef struct Calls
{
	int64		calls;						/* # of times executed */
	int64		rows;						/* total # of retrieved or affected rows */
	double		usage;						/* usage factor */
} Calls;

typedef struct CallTime
{
	double		total_time;					/* total execution time, in msec */
	double		min_time;					/* minimum execution time in msec */
	double		max_time;					/* maximum execution time in msec */
	double		mean_time;					/* mean execution time in msec */
	double		sum_var_time;				/* sum of variances in execution time in msec */
} CallTime;

typedef struct Blocks
{
	int64		shared_blks_hit;			/* # of shared buffer hits */
	int64		shared_blks_read;			/* # of shared disk blocks read */
	int64		shared_blks_dirtied;		/* # of shared disk blocks dirtied */
	int64		shared_blks_written;		/* # of shared disk blocks written */
	int64		local_blks_hit;				/* # of local buffer hits */
	int64		local_blks_read;			/* # of local disk blocks read */
	int64		local_blks_dirtied;			/* # of local disk blocks dirtied */
	int64		local_blks_written;			/* # of local disk blocks written */
	int64		temp_blks_read;				/* # of temp blocks read */
	int64		temp_blks_written;			/* # of temp blocks written */
	double		blk_read_time;				/* time spent reading, in msec */
	double		blk_write_time;				/* time spent writing, in msec */
} Blocks;

typedef struct SysInfo
{
	float		utime;						/* user cpu time */
	float		stime;						/* system cpu time */
} SysInfo;

/*
 * The actual stats counters kept within pgssEntry.
 */
typedef struct Counters
{
	uint64		bucket_id;		/* bucket id */
	Calls		calls[PGSS_NUMKIND];
	QueryInfo	info;
	CallTime	time[PGSS_NUMKIND];
	Blocks		blocks;
	SysInfo		sysinfo;
} Counters;

/* Some global structure to get the cpu usage, really don't like the idea of global variable */

/*
 * Statistics per statement
 */
typedef struct pgssEntry
{
	pgssHashKey		key;			/* hash key of entry - MUST BE FIRST */
	Counters		counters;		/* the statistics for this query */
	int				encoding;		/* query text encoding */
	slock_t			mutex;			/* protects the counters only */
} pgssEntry;

typedef struct QueryFifo
{
		int head;
		int tail;
} QueryFifo;

/*
 * Global shared state
 */
typedef struct pgssSharedState
{
	LWLock			*lock;				/* protects hashtable search/modification */
	double			cur_median_usage;	/* current median usage in hashtable */
	slock_t			mutex;				/* protects following fields only: */
	Size			extent;				/* current extent of query file */
	int				n_writers;			/* number of active writers to query file */
	uint64			current_wbucket;
	uint64			prev_bucket_usec;
	uint64			bucket_overflow[MAX_BUCKETS];
	uint64			bucket_entry[MAX_BUCKETS];
	QueryFifo		query_fifo[MAX_BUCKETS];
} pgssSharedState;

#define ResetSharedState(x) \
do { \
		x->cur_median_usage = ASSUMED_MEDIAN_INIT; \
		x->cur_median_usage = ASSUMED_MEDIAN_INIT; \
		x->n_writers = 0; \
		x->current_wbucket = 0; \
		x->prev_bucket_usec = 0; \
		memset(&x->bucket_overflow, 0, MAX_BUCKETS * sizeof(uint64)); \
		memset(&x->bucket_entry, 0, MAX_BUCKETS * sizeof(uint64)); \
		memset(&x->query_fifo, 0, MAX_BUCKETS * sizeof(uint64)); \
} while(0)



unsigned char *pgss_qbuf[MAX_BUCKETS];

/*
 * Struct for tracking locations/lengths of constants during normalization
 */
typedef struct pgssLocationLen
{
	int			location;		/* start offset in query text */
	int			length;			/* length in bytes, or -1 to ignore */
} pgssLocationLen;

/*
 * Working state for computing a query jumble and producing a normalized
 * query string
 */
typedef struct pgssJumbleState
{
	/* Jumble of current query tree */
	unsigned char *jumble;

	/* Number of bytes used in jumble[] */
	Size		jumble_len;

	/* Array of locations of constants that should be removed */
	pgssLocationLen *clocations;

	/* Allocated length of clocations array */
	int			clocations_buf_size;

	/* Current number of valid entries in clocations array */
	int			clocations_count;

	/* highest Param id we've seen, in order to start normalization correctly */
	int			highest_extern_param_id;
} pgssJumbleState;

typedef enum
{
	PGSM_TRACK_NONE,			/* track no statements */
	PGSM_TRACK_TOP,				/* only top level statements */
	PGSM_TRACK_ALL				/* all statements, including nested ones */
} PGSSTRACKlEVEL;

static const struct config_enum_entry track_options[] =
{
	{"none", PGSM_TRACK_NONE, false},
	{"top", PGSM_TRACK_TOP, false},
	{"all", PGSM_TRACK_ALL, false},
	{NULL, 0, false}
};

#define PGSS_ENABLED() \
	(PGSM_TRACK == PGSM_TRACK_ALL || \
	(PGSM_TRACK == PGSM_TRACK_TOP && nested_level == 0))

/* guc.c */
void init_guc(void);

/*---- GUC variables ----*/
#define PGSM_MAX conf[0].guc_variable
#define PGSM_QUERY_MAX_LEN conf[1].guc_variable
#define PGSM_TRACK conf[2].guc_variable
#define PGSM_TRACK_UTILITY conf[3].guc_variable
#define PGSM_NORMALIZED_QUERY conf[4].guc_variable
#define PGSM_MAX_BUCKETS conf[5].guc_variable
#define PGSM_BUCKET_TIME conf[6].guc_variable
#define PGSM_QUERY_BUF_SIZE conf[7].guc_variable
#define PGSM_OBJECT_CACHE conf[8].guc_variable
#define PGSM_RESPOSE_TIME_LOWER_BOUND conf[9].guc_variable
#define PGSM_RESPOSE_TIME_STEP conf[10].guc_variable
#define PGSM_TRACK_PLANNING conf[11].guc_variable

GucVariable conf[12];
#endif
