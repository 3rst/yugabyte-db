/*
 * Copyright (c) YugaByte, Inc.
 */

package upgrade

import (
	"github.com/spf13/cobra"
)

// UpgradeUniverseCmd represents the universe command
var UpgradeUniverseCmd = &cobra.Command{
	Use:   "upgrade",
	Short: "Upgrade a YugabyteDB Anywhere universe",
	Long:  "Upgrade a universe in YugabyteDB Anywhere",
	Run: func(cmd *cobra.Command, args []string) {
		cmd.Help()
	},
}

func init() {
	UpgradeUniverseCmd.Flags().SortFlags = false

	UpgradeUniverseCmd.AddCommand(upgradeSoftwareCmd)
	UpgradeUniverseCmd.AddCommand(upgradeGflagsCmd)

	UpgradeUniverseCmd.PersistentFlags().StringP("name", "n", "",
		"[Required] The name of the universe to be ugraded.")
	UpgradeUniverseCmd.MarkPersistentFlagRequired("name")

	UpgradeUniverseCmd.PersistentFlags().BoolP("force", "f", false,
		"[Optional] Bypass the prompt for non-interactive usage.")
	UpgradeUniverseCmd.PersistentFlags().BoolP("skip-validations", "s", false,
		"[Optional] Skip validations before running the CLI command.")
}
