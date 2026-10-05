// @strict: true
import * as t from "nonexistent";
interface Obj { name?: string; visitor: { [k: string]: (p: t.Node) => void } }
type API = { assertVersion(n: number): void } & t.ConfigAPI;
declare function declare<O extends Record<string, any>, R extends Obj = Obj>(b: (api: API, options: O, dirname: string) => R): R;
declare(api => ({ visitor: {} }));
declare((api, o, d) => ({ visitor: { X(p) {} } }));
declare function d2(b: (api: t.ConfigAPI) => void): void;
d2(api => {});
export {};
