// @moduleResolution: bundler
// @module: esnext
// @strict: true
// @filename: /node_modules/expect/package.json
{
  "name": "expect",
  "version": "1.0.0",
  "type": "module",
  "exports": { ".": { "types": "./index.d.ts", "default": "./dist/index.js" }, "./*": "./*" },
  "types": "./index.d.ts"
}
// @filename: /node_modules/expect/index.d.ts
import './dist/chai.cjs'
export * from './dist/index.js'
// @filename: /node_modules/expect/dist/chai.d.cts
declare namespace Chai {
    interface Assertion { not: Assertion; }
}
// @filename: /node_modules/expect/dist/index.d.ts
type VitestAssertion<A, T> = { [K in keyof A]: A[K] extends Chai.Assertion ? Assertion<T> : A[K] };
export interface Assertion<T = any> extends VitestAssertion<Chai.Assertion, T> { toBe(x: T): void; }
export declare function expect<T>(x: T): Assertion<T>;
// @filename: /main.ts
import { expect } from "expect";
expect(1).not.toBe(2);
