// origin: n8n-io/n8n at 3c7a553dc packages/nodes-base (named projects, seed 20269001), nodes/Form/test/FormTriggerV2.node.test.ts:308 (express's Request)
// A unique-symbol-keyed member counts toward the truncation length as tsgo counts it: its escaped name ("\xFE@name@" and a
// 15-digit symbol id) and the computed name's expression parts (createExpressionFromSymbolChain), so the long type is
// cut after it.
// @strict: true
type AnyRest = [...args: any[]];
declare class EventEmitter { static readonly captureRejectionSymbol: unique symbol; }
interface Big {
    [EventEmitter.captureRejectionSymbol]?<K>(error: Error, event: string | symbol, ...args: AnyRest): void;
    off<K>(eventName: string | symbol, listener: (...args: any[]) => void): this;
    p0?: number;
    p1?: number;
    p2?: number;
    p3?: number;
    p4?: number;
    p5?: number;
    p6?: number;
    p7?: number;
    p8?: number;
    p9?: number;
    last?: number;
}
declare function f(r: { [K in keyof Big]?: Big[K] }): void;
f({ contentType: 1 });
