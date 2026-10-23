// @module: esnext
// @moduleResolution: bundler
// @strict: true
// @noEmit: true
// An ES module's `import * as ns` of a CommonJS declaration file has a synthetic `default` whatever the module setting
// (tsgo's resolveESModuleSymbol: isEsmCjsRef, then getTypeWithSyntheticDefaultImportType where canHaveSyntheticDefault),
// not only under node16-nodenext (node-pg-migrate's docs: twoslash's .d.mts `import * as typescript from 'typescript'`,
// typescript 7's lib/version.d.cts)
// @filename: lib.d.cts
export declare const version: string;
export declare const versionMajorMinor = "7.0";
// @filename: marked.d.cts
export declare const __esModule: true;
export declare const x: number;
// @filename: user.mts
import * as ns from './lib.cjs'
import * as marked from './marked.cjs'
const v: number = ns.default.versionMajorMinor
const w = marked.default
