// An export default expression that isn't a literal value prints the default export's type, which is the
// expression's widened like a variable's: fresh literals widen (tsc's widenTypeForVariableLikeDeclaration), so
// `'ok' satisfies string` is `string` (vitest's test/e2e/deps/linked printed `"ok"`)
// @strict: true
// @declaration: true
// @filename: /a.ts
export default 'ok' satisfies string;
// @filename: /b.ts
export default Math.random() ? 'a' : 'b';
// @filename: /c.ts
export default (1 satisfies number);
// @filename: /d.ts
export default ('ok' as const);
