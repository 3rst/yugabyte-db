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
 * Information about the Subnet
 * @export
 * @interface SubnetData
 */
export interface SubnetData  {
  /**
   * The subnet reference ID in the provider cloud
   * @type {string}
   * @memberof SubnetData
   */
  subnet_id?: string;
  /**
   * The region/zone of the subnet
   * @type {string}
   * @memberof SubnetData
   */
  subnet_mapping?: string;
  /**
   * If the subnet is used for the primary NIC
   * @type {boolean}
   * @memberof SubnetData
   */
  is_primary?: boolean;
}



