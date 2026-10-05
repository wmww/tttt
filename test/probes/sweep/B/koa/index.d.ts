import Koa = require("koa");
declare function f<T>(t: T): void;
declare const t: unknown;
declare namespace KW {
    interface MC<StateT> { state: StateT; }
    interface X extends MC<unknown> {}
    interface Y extends X { state: Koa.DefaultState }
}
export = KW;
