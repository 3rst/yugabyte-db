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
import type { VPCStateEnum } from './VPCStateEnum';


/**
 * 
 * @export
 * @interface SingleTenantVpcDataResponseInfo
 */
export interface SingleTenantVpcDataResponseInfo  {
  /**
   * 
   * @type {string}
   * @memberof SingleTenantVpcDataResponseInfo
   */
  id: string;
  /**
   * 
   * @type {string}
   * @memberof SingleTenantVpcDataResponseInfo
   */
  cloud_provider_project?: string;
  /**
   * 
   * @type {string[]}
   * @memberof SingleTenantVpcDataResponseInfo
   */
  cluster_ids: string[];
  /**
   * 
   * @type {string[]}
   * @memberof SingleTenantVpcDataResponseInfo
   */
  peering_ids: string[];
  /**
   * 
   * @type {VPCStateEnum}
   * @memberof SingleTenantVpcDataResponseInfo
   */
  state: VPCStateEnum;
  /**
   * 
   * @type {string}
   * @memberof SingleTenantVpcDataResponseInfo
   */
  external_vpc_id?: string;
}



