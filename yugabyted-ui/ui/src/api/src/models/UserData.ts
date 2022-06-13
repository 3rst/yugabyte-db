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
import type { UserInfo } from './UserInfo';
// eslint-disable-next-line no-duplicate-imports
import type { UserSpec } from './UserSpec';


/**
 * User Data
 * @export
 * @interface UserData
 */
export interface UserData  {
  /**
   * 
   * @type {UserSpec}
   * @memberof UserData
   */
  spec?: UserSpec;
  /**
   * 
   * @type {UserInfo}
   * @memberof UserData
   */
  info?: UserInfo;
}



