--
-- create user defined conversion
--

-- directory paths and dlsuffix are passed to us in environment variables
\getenv libdir PG_LIBDIR
\getenv dlsuffix PG_DLSUFFIX

\set regresslib :libdir '/regress' :dlsuffix

CREATE FUNCTION test_enc_conversion(bytea, name, name, bool, validlen OUT int, result OUT bytea)
    AS :'regresslib', 'test_enc_conversion'
    LANGUAGE C STRICT;
