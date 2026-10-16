// @declaration: true
// @declarationMap: true
// An expando assigned an identifier under another name is `export { x as y }`, x mapped to the assigned identifier
// (sequelize's `sql.join = joinSql`); one under its own name isn't mapped
function joinSql(a: string) { return a; }
function attribute(a: string) { return a; }
export function sql() { return 1; }
sql.attribute = attribute;
sql.join = joinSql;
