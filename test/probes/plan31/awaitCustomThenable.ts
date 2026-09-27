interface Resp { status: number }
declare class Base {
    then<TResult1 = Resp, TResult2 = never>(
        onfulfilled?: ((value: Resp) => TResult1 | PromiseLike<TResult1>) | null,
        onrejected?: ((reason: any) => TResult2 | PromiseLike<TResult2>) | null,
    ): Promise<TResult1 | TResult2>;
    send(): this;
}
declare class Test extends Base { end(): this }
async function f(t: Test, b: Base) {
    const r = await t;
    r.status;
    const r2 = await b;
    r2.status;
    const r3 = await t.send();
    r3.status;
}
