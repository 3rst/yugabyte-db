// tslint:disable
/**
 * Yugabyte Cloud
 * YugabyteDB as a Service
 *
 * The version of the OpenAPI document: v1
 * Contact: support@yugabyte.com
 *
 * NOTE: This class is auto generated by OpenAPI Generator (https://openapi-generator.tech).
 * https://openapi-generator.tech
 * Do not edit the class manually.
 */

// eslint-disable-next-line @typescript-eslint/ban-ts-comment
// @ts-ignore
import { useQuery, useInfiniteQuery, useMutation, UseQueryOptions, UseInfiniteQueryOptions, UseMutationOptions } from 'react-query';
import Axios from '../runtime';
import type { AxiosInstance } from 'axios';
// eslint-disable-next-line @typescript-eslint/ban-ts-comment
// @ts-ignore
import type {
  ApiError,
  VoyagerMigrateDataMetrics,
} from '../models';

export interface GetVoyagerDataMigrationMetricsForQuery {
  uuid: string;
}

/**
 * Get Voyager data migration metrics
 * Get Voyager data migration metrics
 */

export const getVoyagerDataMigrationMetricsAxiosRequest = (
  requestParameters: GetVoyagerDataMigrationMetricsForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<VoyagerMigrateDataMetrics>(
    {
      url: '/migration_metrics',
      method: 'GET',
      params: {
        uuid: requestParameters['uuid'],
      }
    },
    customAxiosInstance
  );
};

export const getVoyagerDataMigrationMetricsQueryKey = (
  requestParametersQuery: GetVoyagerDataMigrationMetricsForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/migration_metrics`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useGetVoyagerDataMigrationMetricsInfiniteQuery = <T = VoyagerMigrateDataMetrics, Error = ApiError>(
  params: GetVoyagerDataMigrationMetricsForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<VoyagerMigrateDataMetrics, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getVoyagerDataMigrationMetricsQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<VoyagerMigrateDataMetrics, Error, T>(
    queryKey,
    () => getVoyagerDataMigrationMetricsAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetVoyagerDataMigrationMetricsQuery = <T = VoyagerMigrateDataMetrics, Error = ApiError>(
  params: GetVoyagerDataMigrationMetricsForQuery,
  options?: {
    query?: UseQueryOptions<VoyagerMigrateDataMetrics, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getVoyagerDataMigrationMetricsQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<VoyagerMigrateDataMetrics, Error, T>(
    queryKey,
    () => getVoyagerDataMigrationMetricsAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};






