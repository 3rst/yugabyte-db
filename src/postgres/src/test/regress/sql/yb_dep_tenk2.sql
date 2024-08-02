--
-- A collection of queries to build the tenk2 table.
--
-- The queries are taken from the relevant dependency files.  Since it is
-- faster to run this rather than each file itself (e.g. dependency chain
-- test_setup, create_index), prefer using this.  Also, to make it usable with
-- yb_pg_test_setup and yb_pg_create_index, make it idempotent.
--
-- DEPENDENCY: this file must be run after tenk1 has been populated.
--

-- To avoid IF NOT EXISTS NOTICE messages.
SET client_min_messages TO WARNING;

--
-- test_setup
--

CREATE TABLE IF NOT EXISTS tenk2 AS SELECT * FROM tenk1;

--
-- yb_pg_create_index
-- (With modification to make them all nonconcurrent for performance.)
--

CREATE INDEX NONCONCURRENTLY IF NOT EXISTS tenk2_unique1 ON tenk2 USING btree(unique1 int4_ops ASC);

CREATE INDEX NONCONCURRENTLY IF NOT EXISTS tenk2_unique2 ON tenk2 USING btree(unique2 int4_ops ASC);

CREATE INDEX NONCONCURRENTLY IF NOT EXISTS tenk2_hundred ON tenk2 USING btree(hundred int4_ops ASC);
