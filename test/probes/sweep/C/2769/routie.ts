interface R {
    (path: string): void;
    (path: string, fn: Function): void;
    (routes: { [key: string]: Function }): void;
}
declare var r: R;
r({ "users": function() {}, "about": function() {} });
declare function g(routes: { [key: string]: Function }): void;
g({ "users": function() {} });
declare function h(routes: { [key: string]: Function }): void;
h({ users: function() {} });
