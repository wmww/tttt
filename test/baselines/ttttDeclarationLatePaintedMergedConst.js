//// [test/cases/ttttDeclarationLatePaintedMergedConst.ts] ////

//// [context.ts]
export interface Tag<Id, Service> { readonly id: Id; readonly s: Service }
export declare const GenericTag: <Identifier, Service = Identifier>(key: string) => Tag<Identifier, Service>;
//// [main.ts]
import * as Context from "./context"
interface Counter {
  readonly _: unique symbol
}
const Counter = Context.GenericTag<Counter, { count: number }>("counter")
export declare const x: Counter;


//// [context.js]
export {};
//// [main.js]
import * as Context from "./context";
const Counter = Context.GenericTag("counter");


//// [context.d.ts]
export interface Tag<Id, Service> {
    readonly id: Id;
    readonly s: Service;
}
export declare const GenericTag: <Identifier, Service = Identifier>(key: string) => Tag<Identifier, Service>;
//// [main.d.ts]
import * as Context from "./context";
interface Counter {
    readonly _: unique symbol;
}
declare const Counter: Context.Tag<Counter, {
    count: number;
}>;
export declare const x: Counter;
export {};
