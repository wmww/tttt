//// [test/cases/ttttDeclarationReexportModeMismatch.ts] ////

//// [types.ts]
export type Statements = { readonly [resource: string]: readonly string[] };
//// [index.ts]
export * from "./types";
import type { Statements } from "./types";
export declare function createAccessControl<const T extends Statements>(s: T): {
    newRole<const R extends Statements>(statements: R): R;
    statements: T;
};
//// [index.ts]
export * from "./access";
//// [statement.ts]
import { createAccessControl } from "../../access";
export const ac = createAccessControl({ user: ["create", "list"] } as const);


//// [types.js]
export {};
//// [index.js]
export * from "./types";
//// [index.js]
export * from "./access";
//// [statement.js]
import { createAccessControl } from "../../access";
export const ac = createAccessControl({ user: ["create", "list"] });


//// [types.d.ts]
export type Statements = {
    readonly [resource: string]: readonly string[];
};
//// [index.d.ts]
export * from "./types";
import type { Statements } from "./types";
export declare function createAccessControl<const T extends Statements>(s: T): {
    newRole<const R extends Statements>(statements: R): R;
    statements: T;
};
//// [index.d.ts]
export * from "./access";
//// [statement.d.ts]
export declare const ac: {
    newRole<const R extends import("../..").Statements>(statements: R): R;
    statements: {
        readonly user: readonly ["create", "list"];
    };
};
