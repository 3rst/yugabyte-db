#!/usr/bin/perl

use strict;
use warnings;
use File::Basename;
use File::Compare;
use File::Copy;
use String::Util qw(trim);
use Test::More;
use lib 't';
use pgsm;

# Get filename and create out file name and dirs where requried
PGSM::setup_files_dir(basename($0));

# Create new PostgreSQL node and do initdb
my $node = PGSM->pgsm_init_pg();
my $pgdata = $node->data_dir;

# Update postgresql.conf to include/load pg_stat_monitor library
open my $conf, '>>', "$pgdata/postgresql.conf";
print $conf "shared_preload_libraries = 'pg_stat_monitor'\n";
print $conf "pg_stat_monitor.pgsm_overflow_target = 0\n";
print $conf "pg_stat_monitor.pgsm_max = 1\n";
print $conf "pg_stat_monitor.pgsm_query_shared_buffer = 1\n";
close $conf;

# Start server
my $rt_value = $node->start;
ok($rt_value == 1, "Start Server");

# Create extension and change out file permissions
my ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE EXTENSION pg_stat_monitor;', extra_params => ['-a']);
ok($cmdret == 0, "Create PGSM Extension");
PGSM::append_to_file($stdout);

# Run required commands/queries and dump output to out file.
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'create table foo (id int generated by default as identity,col1 varchar(100) not null,primary key(id));', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Create Table foo");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM Extension");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT * from pg_stat_monitor_settings where name='pg_stat_monitor.pgsm_overflow_target';", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM Extension Settings");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', '\i scripts/data_1.sql' , extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Run sql file:  scripts/data.sql");
#PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT substr(query, 0, 50), length(query), bucket, queryid, calls, elevel, sqlcode, message from pg_stat_monitor;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "SELECT substr(query, 0, 50), calls, message from pg_stat_monitor");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT count(queryid) from pg_stat_monitor;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "SELECT count(queryid) from pg_stat_monitor");
PGSM::append_to_file($stdout);

$node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_overflow_target = 1\n");
$node->restart();

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM Extension");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', "SELECT * from pg_stat_monitor_settings where name='pg_stat_monitor.pgsm_overflow_target';", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM Extension Settings");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', '\i scripts/data_2.sql' , extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Run sql file:  scripts/data2.sql");
#PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT substr(query, 0, 50), length(query), bucket, queryid, calls, elevel, sqlcode, message from pg_stat_monitor;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "SELECT substr(query, 0, 50), calls, message from pg_stat_monitor");
PGSM::append_to_file($stdout);

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT count(queryid) from pg_stat_monitor;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "SELECT count(queryid) from pg_stat_monitor");
PGSM::append_to_file($stdout);

# Drop extension
$stdout = $node->safe_psql('postgres', 'Drop extension pg_stat_monitor;',  extra_params => ['-a']);
ok($cmdret == 0, "Drop PGSM  Extension");
PGSM::append_to_file($stdout);

# Stop the server
$node->stop;

# Done testing for this testcase file.
done_testing();


