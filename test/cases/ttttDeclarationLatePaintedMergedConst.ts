// @declaration: true
// An interface painted by an export paints the const it merges with (every declaration of the name is written), and
// the const's type then paints what it names: the `Context` import (effect's `interface Counter` with `const Counter =
// Context.GenericTag<Counter, ...>(...)`; tttt wrote the const without its import)
// @filename: context.ts
export interface Tag<Id, Service> { readonly id: Id; readonly s: Service }
export declare const GenericTag: <Identifier, Service = Identifier>(key: string) => Tag<Identifier, Service>;
// @filename: main.ts
import * as Context from "./context"
interface Counter {
  readonly _: unique symbol
}
const Counter = Context.GenericTag<Counter, { count: number }>("counter")
export declare const x: Counter;
