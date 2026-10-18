// A namespace only an augmentation declares keeps the augmentation as its parent in tsc (mergeSymbolTable reparents
// only what it merged): its fully qualified name starts with the augmentation's name, `"./a".N`, `global.G`,
// `"react".React` (an augmentation of an `export =` namespace, whose own `React.X` then finds the inner namespace)
// @filename: /node_modules/react/index.d.ts
export = React;
export as namespace React;
declare namespace React {
    interface Props { children?: React.Node; }
    type Node = string;
}
// @filename: /a.ts
export interface A { x: number }
// @filename: /b.ts
import type * as React from "react";
import { A } from "./a";
declare module "react" {
    namespace React {
        interface Extra { a: string }
    }
}
declare module "./a" {
    namespace N { interface I {} }
}
declare global {
    namespace G { interface I {} }
}
export type T1 = import("./a").N.Missing;
export type T2 = G.Missing;
declare namespace Local { }
export type T3 = Local.Missing;
export const x: React.Props = {};
