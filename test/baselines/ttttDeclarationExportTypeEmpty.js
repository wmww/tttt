//// [test/cases/ttttDeclarationExportTypeEmpty.ts] ////

//// [a.ts]
// a file that is a module only
export type {};
//// [b.ts]
export type   {   } ;
const x = 1;


//// [a.js]
export {};
//// [b.js]
const x = 1;
export {};


//// [a.d.ts]
export type {};
//# sourceMappingURL=a.d.ts.map//// [b.d.ts]
export type {};
//# sourceMappingURL=b.d.ts.map