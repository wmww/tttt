// @noTypesAndSymbols: true
// An unknown compiler option set to null is no setting and no TS5023 (tsgo's onPropertySet skips a null value before
// it looks the name up); one with a value still is (npm sample 20261023, a bench config nulling removed options)
// @filename: tsconfig.json
{ "compilerOptions": { "noEmit": true, "types": [], "noImplicitUseStrict": null, "noStrictGenericChecks": null, "keyofStringsOnly": true } }
// @filename: a.ts
const sentinel: number = "";
