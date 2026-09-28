// Matches tsc except the symbol of `c.name` in the inline decorator (tsc: ClassMethodDecoratorContext.name, the
// intersection with the override type)
// @target: es2022
declare function field<T, V>(target: undefined, context: ClassFieldDecoratorContext<T, V>): (value: V) => V;
declare function strField(target: undefined, context: ClassFieldDecoratorContext<unknown, string>): void;
declare function method<T extends (...args: any[]) => any>(target: T, context: ClassMethodDecoratorContext): T;
declare function wrong(target: Function, context: ClassMethodDecoratorContext): number;
declare function getter(target: () => number, context: ClassGetterDecoratorContext<unknown, number>): void;
declare function acc<T, V>(target: ClassAccessorDecoratorTarget<T, V>, context: ClassAccessorDecoratorContext<T, V>): ClassAccessorDecoratorResult<T, V>;
declare function named(target: unknown, context: { name: "exact"; private: false; static: false }): void;
declare function priv(target: unknown, context: { name: "#p"; private: true }): void;
declare function cls<C extends abstract new (...args: any) => any>(target: C, context: ClassDecoratorContext<C>): C | void;
declare function none(): void;

@cls
class D {
    @field x = 1;
    @strField y = 1;
    @method m() { return 1; }
    @wrong w() {}
    @getter get g() { return 1; }
    @acc accessor a = "";
    @named exact() {}
    @named notExact() {}
    @priv #p() {}
    @none n() {}
    @((t, c) => { c.name; }) inline() {}
    @((t: unknown, c: ClassMethodDecoratorContext<D, () => void>) => t) inferred() {}
}
