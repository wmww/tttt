// @experimentalDecorators: true
// @emitDecoratorMetadata: true
// @isolatedModules: true
// @module: esnext
// @target: es2022
// A decorated method's rest parameter is checked by its element type as written (tsc's
// getParameterTypeNodeForDecoratorCheck): a reference's first type argument, `T[]`'s T, nothing else. So a type-only
// import used as `...args: ArgsOf<'x'>` isn't TS1272 (immich's `@OnEvent() onJobRun(...[queue, job]: ArgsOf<'JobRun'>)`)
// @filename: types.ts
export type ArgsOf<T extends string> = [T, number];
export interface Item { a: number }
export type Plain = string[];
// @filename: main.ts
import { ArgsOf, Item, Plain } from "./types";
declare function dec(...a: any[]): any;
export class C {
  @dec m1(...[name, n]: ArgsOf<"x">) {}
  @dec m2(...items: Item[]) {}
  @dec m3(...items: Array<Item>) {}
  @dec m4(...items: Plain) {}
  @dec m5(item: Item) {}
  @dec m6(...items: readonly Item[]) {}
  @dec m7(...items: ReadonlyArray<Item>) {}
  @dec m8(...items: (Item[])) {}
}
