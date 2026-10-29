// An undeclared name in a value export specifier is reported twice by tsc: TS2304 by resolveAlias (no suggestion),
// TS2552 by markExportSpecifierAliasReferenced; a type-only one once
export let exportedFoo: any;
type Roomx = {};
export { nonexportedFoo, type Roomy };
export { zzz };
