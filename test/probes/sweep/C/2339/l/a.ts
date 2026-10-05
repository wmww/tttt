declare namespace d.f {
    enum S { A, B }
    interface I { status: S; }
}
declare const i: d.f.I;
i.status.toFixed();
declare enum E { A, B }
declare const e: E;
e.toFixed();
