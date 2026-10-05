interface R {
    (path: string): void;
    (routes: { [key: string]: Function }): void;
}
declare var r: R;
r({ users: function() {} });
interface R2 {
    (path: string): void;
    (routes: { [key: string]: Function }): void;
}
declare var r2: R2;
r2({ users: () => {} });
interface R3 {
    (path: string): void;
    (routes: { [key: string]: () => void }): void;
}
declare var r3: R3;
r3({ users: function() {} });
