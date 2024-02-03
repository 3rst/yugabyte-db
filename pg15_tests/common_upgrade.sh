# This file is sourced by each upgrade test. It contains shared code, such as
# functions and variables.

# Upgrade tests use a 3-node cluster and need to reference the second and third nodes.
pghost2=127.0.0.$((ip_start + 1))
pghost3=127.0.0.$((ip_start + 2))

# Downloads, runs, and pushds the directory for pg11.
run_and_pushd_pg11() {
  prefix="/tmp"
  ybversion_pg11="2.18.4.2"
  ybbuild="b2"
  if [[ $OSTYPE = linux* ]]; then
    arch="linux-x86_64"
  fi
  if [[ $OSTYPE = darwin* ]]; then
    arch="darwin-x86_64"
  fi
  ybfilename_pg11="yugabyte-$ybversion_pg11-$ybbuild-$arch.tar.gz"

  if [ ! -d "$prefix"/"yugabyte-$ybversion_pg11" ]; then
    curl "https://downloads.yugabyte.com/releases/$ybversion_pg11/$ybfilename_pg11" \
        | tar xzv -C "$prefix"
  fi

  if [[ $OSTYPE = linux* ]]; then
    "$prefix/yugabyte-$ybversion_pg11/bin/post_install.sh"
  fi
  pushd "$prefix/yugabyte-$ybversion_pg11"
  yb_ctl_destroy_create --rf=3
}

# Restarts the masters in a mode that runs initdb and is aware of the PG11 to PG15 upgrade process.
# Must be run while the current directory is a pg15 directory, typically the directory for the
# source code being built.
upgrade_masters_run_initdb() {
  for i in {1..3}; do
    yb_ctl restart_node $i --master \
        --master_flags="master_auto_run_initdb=true,TEST_online_pg11_to_pg15_upgrade=true"
  done

  # Wait until initdb has finished. On a Mac, it takes around 22 seconds on a release build, or over
  # 90 seconds on debug. On Linux release it can take over 2 minutes. initdb may start on any node,
  # so we monitor all of them. Typically it runs on node 1 or node 2.
  echo initdb starting at $(date +"%r")
  timeout 180 bash -c "tail -F $data_dir/node-1/disk-1/yb-data/master/logs/yb-master.INFO \
                               $data_dir/node-2/disk-1/yb-data/master/logs/yb-master.INFO \
                               $data_dir/node-3/disk-1/yb-data/master/logs/yb-master.INFO | \
      grep -m 1 \"initdb completed successfully\""
}

# Upgrades the cluster to PG15, using node 2 as the PG15 tserver. Currently it upgrades only
# database "yugabyte", not global objects or other databases. On exit, node 2 is a working PG15
# tserver.
# Assumes node 2 is drained of all traffic before calling.
ysql_upgrade_using_node_2() {
  # Restart tserver 2 to PG15 for the upgrade, with postgres binaries in binary_upgrade mode
  yb_ctl restart_node 2 --tserver_flags="TEST_pg_binary_upgrade=true"

  # To simulate a core piece of pg_upgrade, we run ysql_dump and pg_restore. Together these
  # utilities
  # with these options migrate the metadata for database 'yugabyte'.
  build/latest/postgres/bin/ysql_dump -h "$PGHOST" --schema-only --quote-all-identifiers \
      --binary-upgrade --format=custom --file="$data_dir"/yugabyte-db.custom \
      --include-yb-metadata dbname=yugabyte
  build/latest/postgres/bin/pg_restore -h "$pghost2" --clean --create --exit-on-error --verbose \
      --dbname template1 "$data_dir"/yugabyte-db.custom

  # The upgrade is finished. Restart node 2 with postgres binaries *not* in binary upgrade mode
  yb_ctl restart_node 2
}
