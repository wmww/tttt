//// [test/cases/ttttDeclarationInferredPredicatePaintsImport.ts] ////

//// [charCode.ts]
export const enum CharCode { Slash = 47, Backslash = 92 }
//// [extpath.ts]
import { CharCode } from "./charCode";
export function isPathSeparator(code: number) {
    return code === CharCode.Slash || code === CharCode.Backslash;
}


//// [charCode.js]
export {};
//// [extpath.js]
export function isPathSeparator(code) {
    return code === 47 /* CharCode.Slash */ || code === 92 /* CharCode.Backslash */;
}


//// [charCode.d.ts]
export declare const enum CharCode {
    Slash = 47,
    Backslash = 92
}
//// [extpath.d.ts]
import { CharCode } from "./charCode";
export declare function isPathSeparator(code: number): code is CharCode;
