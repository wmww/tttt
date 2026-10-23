// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: function_array.ts
x = [f\u0075nction () {}];
// @filename: function_array2.ts
x = [1, f\u0075nction () {}];
// @filename: function_arrow.ts
x = () => f\u0075nction () {};
// @filename: function_call.ts
f(f\u0075nction () {});
// @filename: function_call2.ts
f(1, f\u0075nction () {});
// @filename: function_new.ts
new X(f\u0075nction () {});
// @filename: function_opt.ts
f?.(f\u0075nction () {});
// @filename: function_targ.ts
f<T>(f\u0075nction () {});
// @filename: import_array.ts
x = [i\u006dport("x")];
// @filename: import_array2.ts
x = [1, i\u006dport("x")];
// @filename: import_arrow.ts
x = () => i\u006dport("x");
// @filename: import_call.ts
f(i\u006dport("x"));
// @filename: import_call2.ts
f(1, i\u006dport("x"));
// @filename: import_new.ts
new X(i\u006dport("x"));
// @filename: import_opt.ts
f?.(i\u006dport("x"));
// @filename: import_targ.ts
f<T>(i\u006dport("x"));
// @filename: new_array.ts
x = [n\u0065w X()];
// @filename: new_array2.ts
x = [1, n\u0065w X()];
// @filename: new_arrow.ts
x = () => n\u0065w X();
// @filename: new_call.ts
f(n\u0065w X());
// @filename: new_call2.ts
f(1, n\u0065w X());
// @filename: new_new.ts
new X(n\u0065w X());
// @filename: new_opt.ts
f?.(n\u0065w X());
// @filename: new_targ.ts
f<T>(n\u0065w X());
// @filename: null_array.ts
x = [n\u0075ll];
// @filename: null_array2.ts
x = [1, n\u0075ll];
// @filename: null_arrow.ts
x = () => n\u0075ll;
// @filename: null_call.ts
f(n\u0075ll);
// @filename: null_call2.ts
f(1, n\u0075ll);
// @filename: null_new.ts
new X(n\u0075ll);
// @filename: null_opt.ts
f?.(n\u0075ll);
// @filename: null_targ.ts
f<T>(n\u0075ll);
// @filename: this_array.ts
x = [t\u0068is];
// @filename: this_array2.ts
x = [1, t\u0068is];
// @filename: this_arrow.ts
x = () => t\u0068is;
// @filename: this_call.ts
f(t\u0068is);
// @filename: this_call2.ts
f(1, t\u0068is);
// @filename: this_new.ts
new X(t\u0068is);
// @filename: this_opt.ts
f?.(t\u0068is);
// @filename: this_targ.ts
f<T>(t\u0068is);
// @filename: void_array.ts
x = [v\u006fid 0];
// @filename: void_array2.ts
x = [1, v\u006fid 0];
// @filename: void_arrow.ts
x = () => v\u006fid 0;
// @filename: void_call.ts
f(v\u006fid 0);
// @filename: void_call2.ts
f(1, v\u006fid 0);
// @filename: void_new.ts
new X(v\u006fid 0);
// @filename: void_opt.ts
f?.(v\u006fid 0);
// @filename: void_targ.ts
f<T>(v\u006fid 0);
