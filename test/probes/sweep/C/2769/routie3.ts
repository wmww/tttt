interface R {
    (path: string): void;
    (routes: { [key: string]: Function }): void;
}
declare var r: R;
r({ users: function() {} });
