// A function argument's return type is inferred by its contextual check before the argument is related (tsc's
// contextuallyCheckFunctionExpressionOrObjectLiteralMethod, no pushTypeResolution): the relation's request in the
// nested re-resolution of the call then finds c's re-entered evaluation in flight, TS7022 x2 and TS7024 (genkit's
// dev-ui-gallery, `lazyToolNames(() => ai)` with unresolved callees around it)
// @strict: true
declare function lazy(f: () => { r?: unknown }): string[];
const c = undeclared(c2.plugin());
const c2 = undeclared({ e: lazy(() => c) });
