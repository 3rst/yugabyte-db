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
 * XCluster Config Specification
 * @export
 * @interface XClusterConfigSpec
 */
export interface XClusterConfigSpec  {
  /**
   * The name of the config to be created
   * @type {string}
   * @memberof XClusterConfigSpec
   */
  name: string;
  /**
   * The UUID of the source cluster
   * @type {string}
   * @memberof XClusterConfigSpec
   */
  source_cluster_id: string;
  /**
   * UUID of the target cluster
   * @type {string}
   * @memberof XClusterConfigSpec
   */
  target_cluster_id: string;
  /**
   * Names of Tables to be replicated
   * @type {string[]}
   * @memberof XClusterConfigSpec
   */
  table_ids: string[];
}



