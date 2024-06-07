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
  InlineResponse200,
} from '../models';


/**
 * Retrieve the list of PITR schedules in the YugabyteDB cluster.
 * Get PITR Schedules
 */

export const getPITRSchedulesAxiosRequest = (
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<InlineResponse200>(
    {
      url: '/pitr',
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const getPITRSchedulesQueryKey = (
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/pitr`,
  pageParam,
];


export const useGetPITRSchedulesInfiniteQuery = <T = InlineResponse200, Error = ApiError>(
  options?: {
    query?: UseInfiniteQueryOptions<InlineResponse200, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getPITRSchedulesQueryKey(pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<InlineResponse200, Error, T>(
    queryKey,
    () => getPITRSchedulesAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetPITRSchedulesQuery = <T = InlineResponse200, Error = ApiError>(
  options?: {
    query?: UseQueryOptions<InlineResponse200, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getPITRSchedulesQueryKey(version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<InlineResponse200, Error, T>(
    queryKey,
    () => getPITRSchedulesAxiosRequest(customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};






