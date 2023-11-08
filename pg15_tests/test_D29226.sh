#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

# Modified slightly due to D29444.
pushd src/postgres/src/test/regress
diff <(for f in yb_*_schedule; do ./yb_lint_regress_schedule.sh "$f" || echo "$f"; done) - <<EOT
test: yb_pg_rolenames
yb_pg_auth_serial_schedule
test: yb_pg_create_function_0
yb_pg_foreign_data_schedule
test: yb_pg_create_function_3
test: yb_pg_identity
yb_pg_misc_independent_serial_schedule
test: yb_pg_create_function_1
test: yb_pg_create_type
test: yb_pg_create_table
test: yb_pg_create_function_2
yb_pg_misc_serial_schedule
test: yb_pg_create_function_1
yb_pg_types_udt_serial_schedule
test: yb_pg_create_function_0
yb_triggers_schedule
EOT
popd
