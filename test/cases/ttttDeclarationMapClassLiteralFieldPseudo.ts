// @declaration: true
// @declarationMap: true
// @emitDeclarationOnly: true
// @strict: true
// @target: es2022
// @lib: es2022,dom
// A class expression printed as a type literal writes each inferred field from its pseudo type, mapped where the
// field's initializer says it (tsgo's serializeTypeForDeclaration of the PropertyDeclaration; lit's ssr-dom-shim)
export const Shim = class ElementInternals {
  form = null;
  labels = [] as unknown as NodeListOf<HTMLLabelElement>;
  validity = {} as ValidityState;
  n = 1;
  s = new Set<string>();
  o = { a: 1 };
  willValidate = true;
};
