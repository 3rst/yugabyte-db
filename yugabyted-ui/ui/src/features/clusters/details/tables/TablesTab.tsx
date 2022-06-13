import React, { FC, useState } from 'react';
import clsx from 'clsx';
import { useRouteMatch } from 'react-router-dom';
import { makeStyles, Box, Typography, InputAdornment, MenuItem } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { AlertVariant, YBTable, YBLoadingBox, YBButton, YBInput, YBDropdown } from '@app/components';
import { ApiError, useGetClusterTablesQuery, GetClusterTablesApiEnum } from '@app/api/src';
import { getMemorySizeUnits, useToast } from '@app/helpers';
import SearchIcon from '@app/assets/search.svg';
import TriangleDownIcon from '@app/assets/triangle-down.svg';

const useStyles = makeStyles((theme) => ({
  icon: {
    marginLeft: 'auto',
    color: theme.palette.grey[600]
  },
  apiButton: {
    borderRadius: theme.shape.borderRadius,
    marginRight: theme.spacing(1),

    '&:hover': {
      borderColor: theme.palette.grey[300]
    }
  },
  buttonText: {
    color: theme.palette.text.primary
  },
  selected: {
    backgroundColor: theme.palette.grey[300],

    '&:hover': {
      backgroundColor: theme.palette.grey[300]
    }
  },
  sizeCell: {
    textAlign: 'right',
    paddingRight: theme.spacing(1.25)
  },
  dropdown: {
    cursor: 'pointer',
    marginRight: theme.spacing(1)
  }
}));

// enum ApiEnum {
//   Ysql = "YSQL",
//   Ycql = "YCQL"
// };

export const TablesTab: FC = () => {
  const [dbApi, setDbApi] = useState(GetClusterTablesApiEnum.Ysql);
  const { params } = useRouteMatch<App.RouteParams>();
  const { addToast } = useToast();
  const { data: clusterTablesResponse } = useGetClusterTablesQuery(
    {
      ...params,
      api: dbApi
    },
    {
      query: {
        onError: (error: ApiError) => {
          const message = error?.error?.detail ?? '';
          addToast(AlertVariant.Error, message);
        }
      }
    }
  );
  const classes = useStyles();
  const { t } = useTranslation();
  // Change me!
  const tableRows = clusterTablesResponse?.data ?? [];
  const [keyspace, setKeyspace] = useState(0);
  const [searchInput, setSearchInput] = useState<string>();

  const clusterTablesColumns = [
    {
      name: 'id',
      options: {
        display: false,
        filter: false
      }
    },
    {
      name: 'name',
      label: t('clusterDetail.tables.tableName'),
      options: {
        filter: true
      }
    },
    {
      name: 'keyspace',
      label:
        dbApi === "YSQL"
          ? t('clusterDetail.tables.namespace')
          : t('clusterDetail.tables.keyspace'),
      options: {
        filter: true
      }
    },
    {
      name: 'size_bytes',
      label: t('clusterDetail.tables.size'),
      options: {
        filter: true,
        setCellHeaderProps: () => ({ style: { textAlign: 'right' } }),
        setCellProps: () => ({ className: classes.sizeCell }),
        customBodyRender: (value: number) => getMemorySizeUnits(value)
      }
    }
  ];

  const handleKeyspaceChange = (index: number) => {
    setKeyspace(index);
  };

  const keyspaceOptions = [{ label: t('common.all'), value: '' }];
  tableRows
    .reduce((items, curr) => items.add(curr.keyspace), new Set<string>())
    .forEach((k) => keyspaceOptions.push({ label: k, value: k }));

  let displayedTables = searchInput
    ? tableRows.filter((x) => x.name.includes(searchInput) || x.keyspace.includes(searchInput))
    : tableRows;
  if (keyspace > 0) {
    displayedTables = displayedTables.filter((x) => x.keyspace === keyspaceOptions[keyspace].value);
  }

  return (
    <>
      <Box display="flex" alignItems="center">
        <>
          <YBButton
            className={clsx(classes.apiButton, dbApi === GetClusterTablesApiEnum.Ysql && classes.selected)}
            onClick={() => {
              handleKeyspaceChange(0);
              setTimeout(() => {
                setDbApi(GetClusterTablesApiEnum.Ysql);
              });
            }}
          >
            <Typography variant="body2" className={classes.buttonText}>
              {'YSQL'}
            </Typography>
          </YBButton>
          <YBButton
            className={clsx(classes.apiButton, dbApi === GetClusterTablesApiEnum.Ycql && classes.selected)}
            onClick={() => {
              handleKeyspaceChange(0);
              setTimeout(() => {
                setDbApi(GetClusterTablesApiEnum.Ycql);
              });
            }}
          >
            <Typography variant="body2" className={classes.buttonText}>
              {'YCQL'}
            </Typography>
          </YBButton>
        </>
        <Box ml="auto" display="flex" alignItems="center">
          <YBDropdown
            origin={
              <Box display="flex" alignItems="center">
                <strong>{`${
                  dbApi === GetClusterTablesApiEnum.Ysql
                    ? t('clusterDetail.tables.namespace')
                    : t('clusterDetail.tables.keyspace')
                }:`}</strong>
                <Box pl={0.25}>{keyspaceOptions[keyspace].label}</Box>
                <TriangleDownIcon />
              </Box>
            }
            position={'bottom'}
            growDirection={'left'}
            className={classes.dropdown}
          >
            {keyspaceOptions.map((opt, index) => (
              <MenuItem
                key={`keyspaces-${opt.value.replace(' ', '-')}`}
                selected={keyspace === index}
                onClick={() => handleKeyspaceChange(index)}
              >
                {opt.label}
              </MenuItem>
            ))}
          </YBDropdown>
          <YBInput
            placeholder={t('clusterDetail.tables.filterKeyword')}
            InputProps={{
              startAdornment: (
                <InputAdornment position="end">
                  <SearchIcon />
                </InputAdornment>
              )
            }}
            onChange={(ev) => setSearchInput(ev.target.value)}
          />
        </Box>
      </Box>
      <Box mt={3} mb={2}>
        <Typography variant="h5">{t('clusterDetail.tables.numTables', { count: displayedTables.length })}</Typography>
      </Box>
      {displayedTables.length ? (
        <Box pb={4} pt={1}>
          <YBTable data={displayedTables} columns={clusterTablesColumns} options={{ pagination: false }} />
        </Box>
      ) : (
        <YBLoadingBox>{t('clusterDetail.tables.noTablesCopy')}</YBLoadingBox>
      )}
    </>
  );
};
