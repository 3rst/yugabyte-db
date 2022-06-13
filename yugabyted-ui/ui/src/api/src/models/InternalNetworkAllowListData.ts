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
import type { InternalNetworkAllowListSpec } from './InternalNetworkAllowListSpec';
// eslint-disable-next-line no-duplicate-imports
import type { NetworkAllowListInfo } from './NetworkAllowListInfo';


/**
 * Allow list data
 * @export
 * @interface InternalNetworkAllowListData
 */
export interface InternalNetworkAllowListData  {
  /**
   * 
   * @type {InternalNetworkAllowListSpec}
   * @memberof InternalNetworkAllowListData
   */
  spec?: InternalNetworkAllowListSpec;
  /**
   * 
   * @type {NetworkAllowListInfo}
   * @memberof InternalNetworkAllowListData
   */
  info?: NetworkAllowListInfo;
}



