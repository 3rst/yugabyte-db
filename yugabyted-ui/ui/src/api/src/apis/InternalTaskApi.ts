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
  RunScheduledTaskResponse,
  TaskListResponse,
} from '../models';

export interface ListTasksAllForQuery {
  accountId: string;
  projectId?: string;
  entity_id?: string;
  entity_type?: ListTasksAllEntityTypeEnum;
  task_type?: ListTasksAllTaskTypeEnum;
  locking?: boolean;
  internal_task?: boolean;
  order?: string;
  order_by?: string;
  limit?: number;
  continuation_token?: string;
}
export interface RunScheduledTaskForQuery {
  task: string;
  task_instance?: string;
}

/**
 * List tasks (including internal tasks)
 * List tasks
 */

export const listTasksAllAxiosRequest = (
  requestParameters: ListTasksAllForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  return Axios<TaskListResponse>(
    {
      url: '/private/accounts/{accountId}/tasks'.replace(`{${'accountId'}}`, encodeURIComponent(String(requestParameters.accountId))),
      method: 'GET',
      params: {
        projectId: requestParameters['projectId'],
        entity_id: requestParameters['entity_id'],
        entity_type: requestParameters['entity_type'],
        task_type: requestParameters['task_type'],
        locking: requestParameters['locking'],
        internal_task: requestParameters['internal_task'],
        order: requestParameters['order'],
        order_by: requestParameters['order_by'],
        limit: requestParameters['limit'],
        continuation_token: requestParameters['continuation_token'],
      }
    },
    customAxiosInstance
  );
};

export const listTasksAllQueryKey = (
  requestParametersQuery: ListTasksAllForQuery,
  pageParam = -1,
  version = 1,
) => [
  `/v${version}/private/accounts/{accountId}/tasks`,
  pageParam,
  ...(requestParametersQuery ? [requestParametersQuery] : [])
];


export const useListTasksAllInfiniteQuery = <T = TaskListResponse, Error = ApiError>(
  params: ListTasksAllForQuery,
  options?: {
    query?: UseInfiniteQueryOptions<TaskListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  pageParam = -1,
  version = 1,
) => {
  const queryKey = listTasksAllQueryKey(params, pageParam, version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useInfiniteQuery<TaskListResponse, Error, T>(
    queryKey,
    () => listTasksAllAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};

export const useListTasksAllQuery = <T = TaskListResponse, Error = ApiError>(
  params: ListTasksAllForQuery,
  options?: {
    query?: UseQueryOptions<TaskListResponse, Error, T>;
    customAxiosInstance?: AxiosInstance;
  },
  version = 1,
) => {
  const queryKey = listTasksAllQueryKey(params,  version);
  const { query: queryOptions, customAxiosInstance } = options ?? {};

  const query = useQuery<TaskListResponse, Error, T>(
    queryKey,
    () => listTasksAllAxiosRequest(params, customAxiosInstance),
    queryOptions
  );

  return {
    queryKey,
    ...query
  };
};



/**
 * Trigger a scheduled task to run
 * Run scheduled task
 */


export const runScheduledTaskMutate = (
  body: RunScheduledTaskForQuery,
  customAxiosInstance?: AxiosInstance
) => {
  const url = '/private/scheduled_tasks/{task}'.replace(`{${'task'}}`, encodeURIComponent(String(body.task)));
  // eslint-disable-next-line
  // @ts-ignore
  delete body.task;
  return Axios<RunScheduledTaskResponse>(
    {
      url,
      method: 'POST',
    },
    customAxiosInstance
  );
};

export const useRunScheduledTaskMutation = <Error = ApiError>(
  options?: {
    mutation?:UseMutationOptions<RunScheduledTaskResponse, Error>,
    customAxiosInstance?: AxiosInstance;
  }
) => {
  const {mutation: mutationOptions, customAxiosInstance} = options ?? {};
  // eslint-disable-next-line
  // @ts-ignore
  return useMutation<RunScheduledTaskResponse, Error, RunScheduledTaskForQuery, unknown>((props) => {
    return  runScheduledTaskMutate(props, customAxiosInstance);
  }, mutationOptions);
};






/**
  * @export
  * @enum {string}
  */
export enum ListTasksAllEntityTypeEnum {
  Backup = 'BACKUP',
  Cluster = 'CLUSTER',
  ClusterAllowList = 'CLUSTER_ALLOW_LIST',
  Project = 'PROJECT'
}
/**
  * @export
  * @enum {string}
  */
export enum ListTasksAllTaskTypeEnum {
  CreateCluster = 'CREATE_CLUSTER',
  EditCluster = 'EDIT_CLUSTER',
  DeleteCluster = 'DELETE_CLUSTER',
  EditAllowList = 'EDIT_ALLOW_LIST',
  CreateBackup = 'CREATE_BACKUP',
  RestoreBackup = 'RESTORE_BACKUP',
  DeleteProject = 'DELETE_PROJECT',
  UpgradeCluster = 'UPGRADE_CLUSTER',
  PauseCluster = 'PAUSE_CLUSTER',
  ResumeCluster = 'RESUME_CLUSTER'
}
