// origin: npm @vkontakte/vk-mini-apps-router@1.8.6 https://github.com/VKCOM/vk-mini-apps-router tsconfig.json seed 20261207
// flip: no
// @strict: true
// @noEmit: true
// @module: esnext
// @moduleResolution: bundler
// tsgo reports TS2322 "Type 'Params' is not assignable to type 'Params<T>'" on the return (next to the TS2304 and
// TS18048 both sides report). tttt is silent. tsgo accepts the same code put in one file, so the layout matters
// (Params reached through the package's re-export, ViewNavigationRecord through export-star): perhaps the alias's
// variance measurement.
// @filename: node_modules/@remix-run/router/package.json
{"name": "@remix-run/router", "version": "1.23.2", "types": "./dist/index.d.ts"}
// @filename: node_modules/@remix-run/router/dist/index.d.ts
export type { Params } from "./utils";
// @filename: node_modules/@remix-run/router/dist/utils.d.ts
export type Params<Key extends string = string> = {
    readonly [key in Key]: string | undefined;
};
// @filename: src/hooks/useParams.ts
import { Params } from '@remix-run/router';
import { ViewNavigationRecord } from '../services';
function getActualRecordParams<T extends string>(
  previousRecord: ViewNavigationRecord | undefined,
): Params<T> | undefined {
  return currentRouteDoesNotMatchId ? previousRecord.params : undefined;
}
// @filename: src/services/ViewNavigationRecord.type.ts
import { Params } from '@remix-run/router';
export interface ViewNavigationRecord {
  params: Params;
}
// @filename: src/services/index.ts
export * from './ViewNavigationRecord.type';
