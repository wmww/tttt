// An export declaration in a namespace with a module specifier that isn't a string literal is TS1141 alone: tsc's
// checkExternalImportOrExportDeclaration returns there, before TS1194 (bun 96-H2)
export { default } from 123;
export namespace N {
  export { J } from 123;
}
