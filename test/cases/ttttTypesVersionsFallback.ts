// A `typesVersions` pattern that matches but whose substitutions find no file falls back to the plain lookup in tsc
// (tryLoadModuleUsingPaths continues): moment maps `*` to `ts3.1-typings/*`, which has no `locale/es`, and
// `import "moment/locale/es"` finds locale/es.js (no TS2882); `moment/sub` finds the plain sub.d.ts
// @noUncheckedSideEffectImports: true
// @moduleResolution: bundler
// @module: esnext
// @allowJs: true
// @noEmit: true
// @noTypesAndSymbols: true
// @filename: /node_modules/moment/package.json
{ "name": "moment", "main": "./moment.js", "typings": "./moment.d.ts", "typesVersions": { ">=3.1": { "*": ["ts3.1-typings/*"] } } }
// @filename: /node_modules/moment/moment.d.ts
export declare const old: number;
// @filename: /node_modules/moment/ts3.1-typings/moment.d.ts
export declare const now: number;
// @filename: /node_modules/moment/locale/es.js
export {};
// @filename: /node_modules/moment/sub.d.ts
export declare const sub: string;
// @filename: /a.ts
import "moment/locale/es";
import "moment/locale/missing";
import { sub } from "moment/sub";
import { now } from "moment";
export const x: number = sub;
export const y: string = now;
