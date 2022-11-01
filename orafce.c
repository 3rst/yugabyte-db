#include "postgres.h"
#include "storage/lwlock.h"
#include "storage/shmem.h"
#include "utils/guc.h"

#if PG_VERSION_NUM >=  160000

#include "utils/guc_hooks.h"

#else

#include "commands/variable.h"

#endif

#include "orafce.h"
#include "builtins.h"
#include "pipe.h"

#if PG_VERSION_NUM >= 150000

#include "miscadmin.h"

#endif

/*  default value */
char  *nls_date_format = NULL;
char  *orafce_timezone = NULL;


#if PG_VERSION_NUM >= 150000

shmem_request_hook_type prev_shmem_request_hook = NULL;

#endif

#if PG_VERSION_NUM >= 150000

static void
orafce_shmem_request(void)
{
	if (prev_shmem_request_hook)
		prev_shmem_request_hook();

	RequestAddinShmemSpace(SHMEMMSGSZ);
}

#endif


void
_PG_init(void)
{

#if PG_VERSION_NUM >= 150000

	prev_shmem_request_hook = shmem_request_hook;
	shmem_request_hook = orafce_shmem_request;

#else

	RequestAddinShmemSpace(SHMEMMSGSZ);

#endif

	/* Define custom GUC variables. */
	DefineCustomStringVariable("orafce.nls_date_format",
									"Emulate oracle's date output behaviour.",
									NULL,
									&nls_date_format,
									NULL,
									PGC_USERSET,
									0,
									NULL,
									NULL, NULL);

	DefineCustomStringVariable("orafce.timezone",
									"Specify timezone used for sysdate function.",
									NULL,
									&orafce_timezone,
									"GMT",
									PGC_USERSET,
									0,
									check_timezone, NULL, NULL);

	DefineCustomBoolVariable("orafce.varchar2_null_safe_concat",
									"Specify timezone used for sysdate function.",
									NULL,
									&orafce_varchar2_null_safe_concat,
									false,
									PGC_USERSET,
									0,
									NULL, NULL, NULL);

	EmitWarningsOnPlaceholders("orafce");
}
