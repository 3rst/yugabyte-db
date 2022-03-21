#!/usr/bin/perl

use strict;
use warnings;
use String::Util qw(trim);
use File::Basename;
use File::Compare;
use PostgresNode;
use Test::More;

# Expected folder where expected output will be present
my $expected_folder = "t/expected";

# Results/out folder where generated results files will be placed
my $results_folder = "t/results";

# Check if results folder exists or not, create if it doesn't
unless (-d $results_folder)
{
   mkdir $results_folder or die "Can't create folder $results_folder: $!\n";;
}

# Check if expected folder exists or not, bail out if it doesn't
unless (-d $expected_folder)
{
   BAIL_OUT "Expected files folder $expected_folder doesn't exist: \n";;
}

# Get filename of the this perl file
my $perlfilename = basename($0);

#Remove .pl from filename and store in a variable
$perlfilename =~ s/\.[^.]+$//;
my $filename_without_extension = $perlfilename;

# Create expected filename with path
my $expected_filename = "${filename_without_extension}.out";
my $expected_filename_with_path = "${expected_folder}/${expected_filename}" ;

# Create results filename with path
my $out_filename = "${filename_without_extension}.out";
my $out_filename_with_path = "${results_folder}/${out_filename}" ;

# Delete already existing result out file, if it exists.
if ( -f $out_filename_with_path)
{
   unlink($out_filename_with_path) or die "Can't delete already existing $out_filename_with_path: $!\n";
}

# Create new PostgreSQL node and do initdb
my $node = PostgresNode->get_new_node('test');
my $pgdata = $node->data_dir;
$node->dump_info;
$node->init;

# Update postgresql.conf to include/load pg_stat_monitor library
open my $conf, '>>', "$pgdata/postgresql.conf";
print $conf "shared_preload_libraries = 'pg_stat_monitor'\n";
print $conf "pg_stat_monitor.pgsm_enable_query_plan = 'yes'\n";
close $conf;

# Start server
my $rt_value = $node->start;
ok($rt_value == 1, "Start Server");

# Create extension and change out file permissions
my ($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE EXTENSION pg_stat_monitor;', extra_params => ['-a']);
ok($cmdret == 0, "Create PGSM Extension");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");
chmod(0640 , $out_filename_with_path)
    or die("unable to set permissions for $out_filename_with_path");

# Run required commands/queries and dump output to out file.
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM Extension");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT * from pg_stat_monitor_settings;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM Extension Settings");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE TABLE TBL_0(key text primary key, txt_0 text, value_0 int);', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', "INSERT INTO TBL_0(key, txt_0, value_0) VALUES('000000', '846930886', 1804289383);", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT key, txt_0, value_0 FROM TBL_0;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT key, txt_0, value_0 FROM TBL_0;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'UPDATE TBL_0 SET value_0 = 1681692777;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select substr(query, 0,50) as query, calls, query_plan from pg_stat_monitor order by query,calls;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

# Test: planid is not 0 or empty
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select planid from pg_stat_monitor where calls = 2 ;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
isnt($stdout,'',"Test: planid should not be empty");
ok(length($stdout) == 16, 'Length of planid is 16');

$node->append_conf('postgresql.conf', "pg_stat_monitor.pgsm_enable_query_plan = 'no'\n");
$node->restart();

# Run required commands/queries and dump output to out file.
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'Drop TABLE TBL_0;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT pg_stat_monitor_reset();', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Reset PGSM Extension");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT * from pg_stat_monitor_settings;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
ok($cmdret == 0, "Print PGSM Extension Settings");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'CREATE TABLE TBL_0(key text primary key, txt_0 text, value_0 int);', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', "INSERT INTO TBL_0(key, txt_0, value_0) VALUES('000000', '846930886', 1804289383);", extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT key, txt_0, value_0 FROM TBL_0;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'SELECT key, txt_0, value_0 FROM TBL_0;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'UPDATE TBL_0 SET value_0 = 1681692777;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select substr(query, 0,50) as query, calls, query_plan from pg_stat_monitor order by query,calls;', extra_params => ['-a', '-Pformat=aligned','-Ptuples_only=off']);
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

# Test: planid is empty
($cmdret, $stdout, $stderr) = $node->psql('postgres', 'select planid from pg_stat_monitor where calls = 2 ;', extra_params => ['-Pformat=unaligned','-Ptuples_only=on']);
trim($stdout);
is($stdout,'',"Test: planid should be empty");
ok(length($stdout) == 0, 'Length of planid is 0');

# Drop extension
$stdout = $node->safe_psql('postgres', 'Drop extension pg_stat_monitor;',  extra_params => ['-a']);
ok($cmdret == 0, "Drop PGSM  Extension");
TestLib::append_to_file($out_filename_with_path, $stdout . "\n");

# Stop the server
$node->stop;

# compare the expected and out file
my $compare = compare($expected_filename_with_path, $out_filename_with_path);

# Test/check if expected and result/out file match. If Yes, test passes.
is($compare,0,"Compare Files: $expected_filename_with_path and $out_filename_with_path match.");

# Done testing for this testcase file.
done_testing();
