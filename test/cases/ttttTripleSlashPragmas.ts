// `/// <reference>` pragmas as tsgo 7 reads them (extractPragmas, processPragmasIntoFields): names case-insensitive,
// blanks are spaces and tabs, values quoted, no `/>` needed, `types` over `lib` over `path`, the last of a repeated
// argument; no TS2458 for a second amd-module name (tsgo has no amd-module pragma)
// @filename: a.ts
export const a = 1;
// @filename: types.d.ts
declare const t: number;
// @filename: main.ts
/// <amd-module name="one"/>
/// <amd-module name="two"/>
/// <REFERENCE PATH="types.d.ts"/>
/// <reference path=types.d.ts />
/// <reference path="missing1.d.ts" types="missing2"/>
/// <reference foo="x"/>
/// <reference path='missing3.d.ts' path='missing4.d.ts'/>
/// <reference	path="missing5.d.ts"/>
/// <reference lib="es2015" path="missing6.d.ts"/>
/// <reference no-default-lib="false"/>
/// <reference path ="missing7.d.ts" />
/// <reference types="missing8" resolution-mode="bad"/>
/// <reference path="missing9.d.ts" resolution-mode="bad"/>
/// <reference path="missing10.d.ts"
///<reference path="missing11.d.ts" preserve="true" />
/// <referencex path="missing12.d.ts"/>
/// <reference path="missing13.d.ts" bogus=x path="missing14.d.ts"/>
export const m = t;
