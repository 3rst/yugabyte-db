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
import type { EntityMetadata } from './EntityMetadata';


/**
 * Credit Information
 * @export
 * @interface CreditInfo
 */
export interface CreditInfo  {
  /**
   * 
   * @type {string}
   * @memberof CreditInfo
   */
  credit_id: string;
  /**
   * 
   * @type {number}
   * @memberof CreditInfo
   */
  credits_redeemed: number;
  /**
   * 
   * @type {EntityMetadata}
   * @memberof CreditInfo
   */
  metadata: EntityMetadata;
}



