// @strict: true
// @exactOptionalPropertyTypes: true
// Under exactOptionalPropertyTypes an intersection of optional properties, one of them explicitly undefined, reads as
// missing (arktype's NodeErrorContextInput with `{ path?: undefined }` against `{ path?: never }`): an optional
// never takes it
declare function f(x: { path?: never; a?: string }): void;
declare const c: { path?: readonly string[] } & { path?: undefined } & { a?: string };
f(c);
declare const d: { path?: readonly string[] } & { path?: string };
f(d);
