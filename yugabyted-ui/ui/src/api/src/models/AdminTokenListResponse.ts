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


// eslint-disable-next-line no-duplicate-imports
import type { AdminTokenInfo } from './AdminTokenInfo';
// eslint-disable-next-line no-duplicate-imports
import type { PagingMetadata } from './PagingMetadata';


/**
 * 
 * @export
 * @interface AdminTokenListResponse
 */
export interface AdminTokenListResponse  {
  /**
   * 
   * @type {AdminTokenInfo[]}
   * @memberof AdminTokenListResponse
   */
  data: AdminTokenInfo[];
  /**
   * 
   * @type {PagingMetadata}
   * @memberof AdminTokenListResponse
   */
  _metadata: PagingMetadata;
}



