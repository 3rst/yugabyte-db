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
import type { RefactoringCount } from './RefactoringCount';


/**
 * Refectoring recommendations for migrating SQL objects
 * @export
 * @interface RecommendedRefactoringGraph
 */
export interface RecommendedRefactoringGraph  {
  /**
   * 
   * @type {RefactoringCount}
   * @memberof RecommendedRefactoringGraph
   */
  sql_type?: RefactoringCount;
  /**
   * 
   * @type {RefactoringCount}
   * @memberof RecommendedRefactoringGraph
   */
  table?: RefactoringCount;
  /**
   * 
   * @type {RefactoringCount}
   * @memberof RecommendedRefactoringGraph
   */
  view?: RefactoringCount;
  /**
   * 
   * @type {RefactoringCount}
   * @memberof RecommendedRefactoringGraph
   */
  _function?: RefactoringCount;
  /**
   * 
   * @type {RefactoringCount}
   * @memberof RecommendedRefactoringGraph
   */
  triggers?: RefactoringCount;
}



