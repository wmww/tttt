// @strict: true
// A switch is exhaustive by the type of its expression as written: `switch (k!)` over `K | undefined` covers K (tsc's
// computeExhaustiveSwitchStatement checks node.Expression() itself), so the end isn't reachable
type K = 'a' | 'b';
declare const p: { kind?: K };
const { kind } = p;
function f1(): number { switch (kind!) { case 'a': case 'b': return 1; } }
declare const k2: K | undefined;
function f2(): number { switch (k2!) { case 'a': case 'b': return 1; } }
function f3(): number { const k = p.kind; switch (k!) { case 'a': case 'b': return 1; } }
function f4(): number { switch (p.kind!) { case 'a': case 'b': return 1; } }
function f5(): number { switch ((k2)!) { case 'a': case 'b': return 1; } }
function f6(): number { switch (k2) { case 'a': case 'b': return 1; } }
const g = () => { switch (k2!) { case 'a': return 1; case 'b': return 2; } };
const n: number = g();
