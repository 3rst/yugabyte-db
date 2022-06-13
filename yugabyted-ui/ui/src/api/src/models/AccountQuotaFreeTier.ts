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




/**
 * 
 * @export
 * @interface AccountQuotaFreeTier
 */
export interface AccountQuotaFreeTier  {
  /**
   * Maximum number of free tier clusters allowed to exist simultaneously
   * @type {number}
   * @memberof AccountQuotaFreeTier
   */
  max_num_clusters: number;
  /**
   * Number of additional free tier clusters allowed to be created
   * @type {number}
   * @memberof AccountQuotaFreeTier
   */
  num_clusters_remaining: number;
}



