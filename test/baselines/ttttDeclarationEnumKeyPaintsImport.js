//// [test/cases/ttttDeclarationEnumKeyPaintsImport.ts] ////

//// [e.ts]
export const enum Id { A = "a", B = "b" }
//// [c.ts]
import { Id } from "./e";
export const props = {
    [Id.A]: { type: "string" },
};


//// [e.js]
export {};
//// [c.js]
export const props = {
    ["a" /* Id.A */]: { type: "string" },
};


//// [e.d.ts]
export declare const enum Id {
    A = "a",
    B = "b"
}
//// [c.d.ts]
import { Id } from "./e";
export declare const props: {
    [Id.A]: {
        type: string;
    };
};
