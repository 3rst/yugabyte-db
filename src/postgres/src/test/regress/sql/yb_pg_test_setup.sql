--
-- TEST_SETUP --- prepare environment expected by regression test scripts
--

-- directory paths and dlsuffix are passed to us in environment variables
\getenv abs_srcdir PG_ABS_SRCDIR
\getenv libdir PG_LIBDIR
\getenv dlsuffix PG_DLSUFFIX

\set regresslib :libdir '/regress' :dlsuffix

--
-- Postgres formerly made the public schema read/write by default,
-- and most of the core regression tests still expect that.
--
GRANT ALL ON SCHEMA public TO public;

--
-- These tables have traditionally been referenced by many tests,
-- so create and populate them.  Insert only non-error values here.
-- (Some subsequent tests try to insert erroneous values.  That's okay
-- because the table won't actually change.  Do not change the contents
-- of these tables in later tests, as it may affect other tests.)
--

-- TODO(jason): see if "PRIMARY KEY" could be removed, which is not present in
-- the upstream test.
CREATE TABLE FLOAT8_TBL(f1 float8 PRIMARY KEY);

INSERT INTO FLOAT8_TBL(f1) VALUES
  ('0.0'),
  ('-34.84'),
  ('-1004.30'),
  ('-1.2345678901234e+200'),
  ('-1.2345678901234e-200');
VACUUM FLOAT8_TBL;

-- YB note: add primary key for sorting
CREATE TABLE INT2_TBL(f1 int2 PRIMARY KEY);

INSERT INTO INT2_TBL(f1) VALUES
  ('0   '),
  ('  1234 '),
  ('    -1234'),
  ('32767'),  -- largest and smallest values
  ('-32767');
VACUUM INT2_TBL;

-- YB note: add primary key for sorting
CREATE TABLE INT4_TBL(f1 int4 PRIMARY KEY);

INSERT INTO INT4_TBL(f1) VALUES
  ('   0  '),
  ('123456     '),
  ('    -123456'),
  ('2147483647'),  -- largest and smallest values
  ('-2147483647');
VACUUM INT4_TBL;

CREATE TABLE INT8_TBL(q1 int8, q2 int8);

INSERT INTO INT8_TBL VALUES
  ('  123   ','  456'),
  ('123   ','4567890123456789'),
  ('4567890123456789','123'),
  (+4567890123456789,'4567890123456789'),
  ('+4567890123456789','-4567890123456789');
VACUUM INT8_TBL;

CREATE TABLE POINT_TBL(f1 point);

INSERT INTO POINT_TBL(f1) VALUES
  ('(0.0,0.0)'),
  ('(-10.0,0.0)'),
  ('(-3.0,4.0)'),
  ('(5.1, 34.5)'),
  ('(-5.0,-12.0)'),
  ('(1e-300,-1e-300)'),  -- To underflow
  ('(1e+300,Inf)'),  -- To overflow
  ('(Inf,1e+300)'),  -- Transposed
  (' ( Nan , NaN ) '),
  ('10.0,10.0');
-- We intentionally don't vacuum point_tbl here; geometry depends on that

-- YB note: add primary key for sorting
CREATE TABLE TEXT_TBL (f1 text PRIMARY KEY);

INSERT INTO TEXT_TBL VALUES
  ('doh!'),
  ('hi de ho neighbor');
VACUUM TEXT_TBL;

CREATE TABLE VARCHAR_TBL(f1 varchar(4));

INSERT INTO VARCHAR_TBL (f1) VALUES
  ('a'),
  ('ab'),
  ('abcd'),
  ('abcd    ');
VACUUM VARCHAR_TBL;

CREATE TABLE onek (
	unique1		int4,
	unique2		int4,
	two			int4,
	four		int4,
	ten			int4,
	twenty		int4,
	hundred		int4,
	thousand	int4,
	twothousand	int4,
	fivethous	int4,
	tenthous	int4,
	odd			int4,
	even		int4,
	stringu1	name,
	stringu2	name,
	string4		name
);

\set filename :abs_srcdir '/data/onek.data'
COPY onek FROM :'filename';
VACUUM ANALYZE onek;

CREATE TABLE onek2 AS SELECT * FROM onek;
VACUUM ANALYZE onek2;

CREATE TABLE tenk1 (
	unique1		int4,
	unique2		int4,
	two			int4,
	four		int4,
	ten			int4,
	twenty		int4,
	hundred		int4,
	thousand	int4,
	twothousand	int4,
	fivethous	int4,
	tenthous	int4,
	odd			int4,
	even		int4,
	stringu1	name,
	stringu2	name,
	string4		name
);

\set filename :abs_srcdir '/data/tenk.data'
COPY tenk1 FROM :'filename';
VACUUM ANALYZE tenk1;

CREATE TABLE tenk2 AS SELECT * FROM tenk1;
VACUUM ANALYZE tenk2;

CREATE TABLE person (
	name 		text,
	age			int4,
	location 	point
);

\set filename :abs_srcdir '/data/person.data'
COPY person FROM :'filename';
VACUUM ANALYZE person;

CREATE TABLE emp (
	salary 		int4,
	manager 	name
) INHERITS (person);


CREATE TABLE student (
	gpa 		float8
) INHERITS (person);


CREATE TABLE stud_emp (
	percent 	int4
) INHERITS (emp, student);

CREATE TABLE road (
	name		text,
	thepath 	path
);

CREATE TABLE ihighway () INHERITS (road);

CREATE TABLE shighway (
	surface		text
) INHERITS (road);

--
-- Also create some non-built-in range types.
--

create type float8range as range (subtype = float8, subtype_diff = float8mi);

create type textrange as range (subtype = text, collation = "C");

--
-- Create some C functions that will be used by various tests.
--

CREATE FUNCTION binary_coercible(oid, oid)
    RETURNS bool
    AS :'regresslib', 'binary_coercible'
    LANGUAGE C STRICT STABLE PARALLEL SAFE;

CREATE FUNCTION ttdummy ()
    RETURNS trigger
    AS :'regresslib'
    LANGUAGE C;

-- Use hand-rolled hash functions and operator classes to get predictable
-- result on different machines.  The hash function for int4 simply returns
-- the sum of the values passed to it and the one for text returns the length
-- of the non-empty string value passed to it or 0.

create function part_hashint4_noop(value int4, seed int8)
    returns int8 as $$
    select value + seed;
    $$ language sql strict immutable parallel safe;

create operator class part_test_int4_ops for type int4 using hash as
    operator 1 =,
    function 2 part_hashint4_noop(int4, int8);

create function part_hashtext_length(value text, seed int8)
    returns int8 as $$
    select length(coalesce(value, ''))::int8
    $$ language sql strict immutable parallel safe;

create operator class part_test_text_ops for type text using hash as
    operator 1 =,
    function 2 part_hashtext_length(text, int8);
