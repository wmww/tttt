//// [test/cases/ttttDeclarationMapExpandoExportRenamed.ts] ////

//// [ttttDeclarationMapExpandoExportRenamed.ts]
// An expando assigned an identifier under another name is `export { x as y }`, x mapped to the assigned identifier
// (sequelize's `sql.join = joinSql`); one under its own name isn't mapped
function joinSql(a: string) { return a; }
function attribute(a: string) { return a; }
export function sql() { return 1; }
sql.attribute = attribute;
sql.join = joinSql;


//// [ttttDeclarationMapExpandoExportRenamed.js]
// An expando assigned an identifier under another name is `export { x as y }`, x mapped to the assigned identifier
// (sequelize's `sql.join = joinSql`); one under its own name isn't mapped
function joinSql(a) { return a; }
function attribute(a) { return a; }
export function sql() { return 1; }
sql.attribute = attribute;
sql.join = joinSql;


//// [ttttDeclarationMapExpandoExportRenamed.d.ts]
declare function joinSql(a: string): string;
declare function attribute(a: string): string;
export declare function sql(): number;
export declare namespace sql {
    export { attribute };
    export { joinSql as join };
}
export {};
//# sourceMappingURL=ttttDeclarationMapExpandoExportRenamed.d.ts.map