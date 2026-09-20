// The real corpus' biggest bug (utilities.ts: `import { Node } from barrel` + `function Node(this: Node...)`).
// An import whose target has no value meaning merges with a local value of the same name: `Node` the type is the
// imported interface (not the DOM's global Node), `Node` the value is the local function. No errors in tsc.
// @filename: types.ts
export interface Node { kind: number }
export interface Identifier extends Node { text: string }
export class Both { b = 1 }

// @filename: barrel.ts
export * from "./types";
export * from "./util";

// @filename: util.ts
import { Node, Identifier, Both } from "./barrel";
function Node(this: Node, kind: number) { this.kind = kind; }
export function setParent<T extends Node>(child: T): T { return child; }
declare let i: Identifier;
setParent(i);
const ctor: Function = Node;
function Both() {} // TS2440: this one does have a value
