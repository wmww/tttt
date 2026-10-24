// origin: n8n-io/n8n at 3c7a553dc packages/nodes-base (named projects, seed 20269001), nodes/Airtop/actions/agent/agent.utils.ts:15
// An initialized parameter whose annotation names a type of a module that isn't there prints that name: the parameter
// symbol's type has no undefined for an initializer (tsc's getTypeOfSymbol), so the error type keeps its name
// @strict: true
import type { IData } from "missing-module";
function f(this: any, body: IData = {}, n: number = 1) {}
const v: number = f;
class C { m(body: IData = {}) {} }
const w: number = new C().m;
const arrow = (body: IData = {}) => body;
const u: number = arrow;
function g(body?: IData) {}
const t: number = g;
