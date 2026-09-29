type Sel = Record<string, unknown>;
declare class View<TName extends string = string, TExisting extends boolean = boolean, TSel extends Sel = Sel> {
    constructor(c: { name: TName; fields: Sel });
    _: { name: TName; existing: TExisting; sel: TSel };
}
type WithSel<TName extends string, TExisting extends boolean, TSel extends Sel> = View<TName, TExisting, TSel> & TSel;
type Build<T extends Record<string, { x: unknown }>> = { [K in keyof T]: { col: T[K]["x"] } } & {};
declare function prox<T extends object>(t: T, h: {}): T;
function f<TName extends string, TColumns extends Record<string, { x: unknown }>>(name: TName, cols: Sel) {
    const a = prox(new View({ name, fields: cols }), {}) as WithSel<TName, true, Build<TColumns>>;
    const b = new View({ name, fields: cols }) as WithSel<TName, true, Build<TColumns>>;
    const c: WithSel<TName, true, Build<TColumns>> = prox(new View({ name, fields: cols }), {});
}
