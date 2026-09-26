function a1(...[x, ...ys]: [number, ...string[]]) { }
function a2(...[x, y = 1]) { }
function a3(...[x, [y, z]]: [number, [string, boolean]]) { }
function a4(...[]: []) { }
