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
import type { CustomImageData } from './CustomImageData';
// eslint-disable-next-line no-duplicate-imports
import type { CustomImageSetInfo } from './CustomImageSetInfo';
// eslint-disable-next-line no-duplicate-imports
import type { CustomImageSetSpec } from './CustomImageSetSpec';


/**
 * Custom Image Set data
 * @export
 * @interface CustomImageSetData
 */
export interface CustomImageSetData  {
  /**
   * 
   * @type {CustomImageSetSpec}
   * @memberof CustomImageSetData
   */
  spec?: CustomImageSetSpec;
  /**
   * 
   * @type {CustomImageSetInfo}
   * @memberof CustomImageSetData
   */
  info?: CustomImageSetInfo;
  /**
   * List of Custom Images
   * @type {CustomImageData[]}
   * @memberof CustomImageSetData
   */
  custom_images?: CustomImageData[];
}



