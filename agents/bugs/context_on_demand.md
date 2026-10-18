# A function expression's parameters typed before its call gives them a context

Found by the second repository refresh (2026-10-18, todo entry 7): rocket's `src/cli/RocketBuild.js(53,24)` spurious
TS7031, the one day-one mismatch left on the source sweep's new repositories.

## Mechanism
ty/expr.c's header: contextual types are pushed *down* (whoever knows an expression's contextual type checks it with
that type before anything else asks), and a function expression's parameters read their context back from the
KEY_CONTEXT_PARAM links the enclosing call sets. That holds while every expression's type is first demanded by its
parent. A JavaScript `this.x = v` assignment declaration breaks it: the member's type (assignment_declaration_type())
asks for `v`'s type directly, from wherever the member is read, possibly another file checked first. If `v` is a
callback's parameter (`program.action(({ outputDir }) => { this.outputDir = outputDir })`) and the call hasn't been
checked yet, initialized_binding_evaluate_assigned_type() finds no link and types the parameter from its pattern
(`{ outputDir: any }`), memoized; the call later can't change it and function_impl_check_implicit_any_parameters()
reports TS7031. tsc's getContextuallyTypedParameterType walks up to the call instead (getContextualSignature ->
getResolvedSignature), whoever asks first.

## Repro (tsgo: no error; tttt: TS7031, and `{ outputDir }: { outputDir: any; }` in the types)
```js
// cmd.d.ts
export declare class Command { action(fn: (this: this, ...args: any[]) => void | Promise<void>): this; }
// build.js (checked after cli.js: program order puts the import-type target first)
export class RocketBuild {
  /** @param {import('./cmd').Command} program  @param {import('./cli.js').RocketCli} cli */
  async setupCommand(program, cli) {
    program.action(async ({ outputDir }) => { if (outputDir) { this.outputDir = outputDir; } });
  }
}
// cli.js: the union's subtype reduction reads RocketBuild's `outputDir` first
import { RocketBuild } from './build.js';
export class RocketInit { init() {} }
export class RocketCli { async start() { for (const C of [RocketInit, RocketBuild]) { const p = new C(); } } }
```
(allowJs, checkJs, strict). Simpler readers (`/** @param {RocketBuild} b */ f(b) { return b.outputDir }` in a file
checked first) did *not* reproduce with a plain function-typed parameter instead of the class method `action`: the
exact conditions aren't pinned down. `TTTT_CHECK_ONLY=a,b` (comma-separated since this session) checks two files
in program order.

## Direction (not a plan yet)
Give a function expression a way to find its context on demand: the resolve walk records each function expression's
outermost context root (the expression statement's expression, the variable initializer, the return expression...),
and a parameter without a link whose root isn't checked yet checks the root first (as the typecheck walk would:
`ty_expr_type` of a statement expression, the variable's type for an initializer) and reads the link again; a root in
flight falls back to today's behaviour. Questions for whoever plans it: diagnostics produced while checking a root
early (are any reported at compute time rather than by the walk?), flow isolation, and whether TypeScript code hits
it too (only JavaScript assignment declarations and `typeof` of a local reach into function bodies from outside?).
Rare on real code so far: 1 project of 3566.
