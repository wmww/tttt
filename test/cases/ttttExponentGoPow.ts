// @strict: true
// @noEmit: true

// tsgo folds `**` in constant expressions with Go's math.Pow, which isn't correctly rounded like C's pow: `3 ** 34` is
// 16677181699666568 and `10 ** -23` isn't the literal 1e-23 (bun's 42-C2a/b/c)
const a: "3" = `${27 ** (1 / 3)}`;
const b: "16677181699666570" = `${3 ** 34}`;
enum E { G = 10 ** -23 }
const g: E.G = 1e-23;
const ok: "1024" = `${2 ** 10}`;
