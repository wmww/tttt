async function f() {
  try {
    await Promise.resolve(1);
  } catch (error: any) {
    return error?.code;
  }
}
function g() {
  try { } catch (e: unknown) { }
}
