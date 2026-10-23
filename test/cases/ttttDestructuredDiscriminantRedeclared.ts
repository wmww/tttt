// @strict: true
// A destructured parameter's discriminant narrows its siblings by the element's value declaration, the first, also when
// a `var` redeclares it later (tsc's getCandidateDiscriminantPropertyAccess; bun's 21-C21a, 07-C6b)
type Action = { kind: 'A', payload: number } | { kind: 'B', payload: string };
function f1({ kind, payload }: Action) { var kind: 'A' | 'B'; if (kind === 'A') { payload.toFixed(); } }
function f2({ kind, payload }: Action) { var kind: 'A' | 'B' = 'B'; if (kind === 'A') { payload.toFixed(); } }
function f3({ kind, payload }: Action) { if (kind === 'A') { payload.toFixed(); } var kind: 'A' | 'B'; }
function f4(a: Action) { const { kind, payload } = a; var q = 1; if (kind === 'A') { payload.toFixed(); } }
