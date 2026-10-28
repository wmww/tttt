// @strict: true
// @target: es2022
// A decorated class expression takes modifiers after its decorators (tsc's parseDecoratedExpression): TS1184 at the
// first (checkGrammarModifiers of a ClassExpression), `abstract` still makes it abstract (TS2511), and its abstract
// members are TS1244, an abstract class being a class declaration to tsc (bun 80-R08)
declare function dec(...a: any[]): any;
const c1 = @dec abstract class { abstract m(): void };
const c2 = @dec public class {};
const c3 = (@dec export default class {});
new c1();
