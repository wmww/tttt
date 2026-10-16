// A generic function's object literal spreads an optional property in: its annotation is reused where the function's
// return type is instantiated too, as long as it still says the type (tsgo's serializeTypeForDeclaration of the
// property's declaration), not the type printed with `| undefined` (trpc's tests printed
// `transformer?: TypeError<"..."> | undefined`)
// @strict: true
// @declaration: true
// @filename: /a.ts
declare const sym: unique symbol;
export type Tagged<M extends string> = M & { _: typeof sym };
export type Opts<T> = {
  links: T[];
  transformer?: Tagged<'moved'>;
  kind?: 'x' | 'y';
  count?: number;
  shape?: { a: 1 };
};
// @filename: /b.ts
import type { Opts } from './a';
function copy<T>(o: Opts<T>) {
  return { ...o };
}
export function generic<R>(o: Opts<R>) {
  return copy(o);
}
export function concrete(o: Opts<number>) {
  return copy(o);
}
