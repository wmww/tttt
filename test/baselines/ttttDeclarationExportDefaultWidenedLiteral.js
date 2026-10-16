//// [test/cases/ttttDeclarationExportDefaultWidenedLiteral.ts] ////

//// [a.ts]
export default 'ok' satisfies string;
//// [b.ts]
export default Math.random() ? 'a' : 'b';
//// [c.ts]
export default (1 satisfies number);
//// [d.ts]
export default ('ok' as const);


//// [a.js]
export default 'ok';
//// [b.js]
export default Math.random() ? 'a' : 'b';
//// [c.js]
export default 1;
//// [d.js]
export default 'ok';


//// [a.d.ts]
declare const _default: string;
export default _default;
//// [b.d.ts]
declare const _default: string;
export default _default;
//// [c.d.ts]
declare const _default: number;
export default _default;
//// [d.d.ts]
declare const _default: 'ok';
export default _default;
