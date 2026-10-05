// @filename: koa.d.ts
declare namespace Application {
    type DefaultStateExtends = any;
    interface DefaultState extends DefaultStateExtends {}
}
export = Application;
// @filename: a.ts
import Koa = require("./koa");
export function f<T>(t: T) { const d: Koa.DefaultState = t; }
