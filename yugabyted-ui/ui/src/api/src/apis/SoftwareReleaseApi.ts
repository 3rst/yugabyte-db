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
  SoftwareReleaseListResponse,
  SoftwareReleaseResponse,
  SoftwareReleaseTrackListResponse,
  SoftwareReleaseTrackResponse,
} from '../models';

export interface GetReleaseForQuery {
  accountId: string;
  trackId: string;
  releaseId: string;
}
export interface GetTrackByIdForQuery {
  accountId: string;
  trackId: string;
}
export interface ListReleasesForQuery {
  accountId: string;
  trackId: string;
  limit?: number;
  continuation_token?: string;
}
export interface ListTracksForAccountForQuery {
  accountId: string;
}

/**
 * Get Software Release by ID
 * Get Software Release by Id
 */

export const getReleaseAxiosRequest = (
  requestParameters: GetReleaseForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<SoftwareReleaseResponse>(
    {
      url: '/public/accounts/{accountId}/software/tracks/{trackId}/releases/{releaseId}'.replace(`{${'accountId'}}`, encodeURIComponent(String(requestParameters.accountId))).replace(`{${'trackId'}}`, encodeURIComponent(String(requestParameters.trackId))).replace(`{${'releaseId'}}`, encodeURIComponent(String(requestParameters.releaseId))),
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const getReleaseQueryKey = (
  requestParametersQuery: GetReleaseForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/public/accounts/{accountId}/software/tracks/{trackId}/releases/{releaseId}`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useGetReleaseInfiniteQuery = <T = SoftwareReleaseResponse, Error = ApiError>(
  params: GetReleaseForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<SoftwareReleaseResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getReleaseQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<SoftwareReleaseResponse, Error, T>(
    queryKey,
    () => getReleaseAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetReleaseQuery = <T = SoftwareReleaseResponse, Error = ApiError>(
  params: GetReleaseForQuery,
  options?: {
    query?: UseQueryOptions<SoftwareReleaseResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getReleaseQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<SoftwareReleaseResponse, Error, T>(
    queryKey,
    () => getReleaseAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Get release track by ID
 * Get release track by ID
 */

export const getTrackByIdAxiosRequest = (
  requestParameters: GetTrackByIdForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<SoftwareReleaseTrackResponse>(
    {
      url: '/public/accounts/{accountId}/software/tracks/{trackId}'.replace(`{${'accountId'}}`, encodeURIComponent(String(requestParameters.accountId))).replace(`{${'trackId'}}`, encodeURIComponent(String(requestParameters.trackId))),
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const getTrackByIdQueryKey = (
  requestParametersQuery: GetTrackByIdForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/public/accounts/{accountId}/software/tracks/{trackId}`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useGetTrackByIdInfiniteQuery = <T = SoftwareReleaseTrackResponse, Error = ApiError>(
  params: GetTrackByIdForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<SoftwareReleaseTrackResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = getTrackByIdQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<SoftwareReleaseTrackResponse, Error, T>(
    queryKey,
    () => getTrackByIdAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useGetTrackByIdQuery = <T = SoftwareReleaseTrackResponse, Error = ApiError>(
  params: GetTrackByIdForQuery,
  options?: {
    query?: UseQueryOptions<SoftwareReleaseTrackResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = getTrackByIdQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<SoftwareReleaseTrackResponse, Error, T>(
    queryKey,
    () => getTrackByIdAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * List DB software releases by track
 * List DB software releases by track
 */

export const listReleasesAxiosRequest = (
  requestParameters: ListReleasesForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<SoftwareReleaseListResponse>(
    {
      url: '/public/accounts/{accountId}/software/tracks/{trackId}/releases'.replace(`{${'accountId'}}`, encodeURIComponent(String(requestParameters.accountId))).replace(`{${'trackId'}}`, encodeURIComponent(String(requestParameters.trackId))),
      method: 'GET',
      params: {
        limit: requestParameters['limit'],
        continuation_token: requestParameters['continuation_token'],
      }
    },
    customAxiosInstance
  );
};

export const listReleasesQueryKey = (
  requestParametersQuery: ListReleasesForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/public/accounts/{accountId}/software/tracks/{trackId}/releases`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useListReleasesInfiniteQuery = <T = SoftwareReleaseListResponse, Error = ApiError>(
  params: ListReleasesForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<SoftwareReleaseListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = listReleasesQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<SoftwareReleaseListResponse, Error, T>(
    queryKey,
    () => listReleasesAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useListReleasesQuery = <T = SoftwareReleaseListResponse, Error = ApiError>(
  params: ListReleasesForQuery,
  options?: {
    query?: UseQueryOptions<SoftwareReleaseListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = listReleasesQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<SoftwareReleaseListResponse, Error, T>(
    queryKey,
    () => listReleasesAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * List all release tracks linked to account
 * List all release tracks linked to account
 */

export const listTracksForAccountAxiosRequest = (
  requestParameters: ListTracksForAccountForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<SoftwareReleaseTrackListResponse>(
    {
      url: '/public/accounts/{accountId}/software/tracks'.replace(`{${'accountId'}}`, encodeURIComponent(String(requestParameters.accountId))),
      method: 'GET',
      params: {
      }
    },
    customAxiosInstance
  );
};

export const listTracksForAccountQueryKey = (
  requestParametersQuery: ListTracksForAccountForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/public/accounts/{accountId}/software/tracks`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useListTracksForAccountInfiniteQuery = <T = SoftwareReleaseTrackListResponse, Error = ApiError>(
  params: ListTracksForAccountForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<SoftwareReleaseTrackListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = listTracksForAccountQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<SoftwareReleaseTrackListResponse, Error, T>(
    queryKey,
    () => listTracksForAccountAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useListTracksForAccountQuery = <T = SoftwareReleaseTrackListResponse, Error = ApiError>(
  params: ListTracksForAccountForQuery,
  options?: {
    query?: UseQueryOptions<SoftwareReleaseTrackListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = listTracksForAccountQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<SoftwareReleaseTrackListResponse, Error, T>(
    queryKey,
    () => listTracksForAccountAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};






