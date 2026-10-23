// @strict: true
// @noEmit: true
// The explicit type of a dotted name read for an assertion check is none while it is read further up (tsc's
// getExplicitTypeOfSymbol: resolvingExplicitTypeOfSymbol). holds' type is asked for by one flow analysis's effects
// check of a call statement, and hold's return type then walks back over `holds.push(h)`, whose effects check finds
// holds again: no effects, rather than a cycle (openclaw's server.chat-membership-authority.product.test.ts)
declare function deferred(): { promise: Promise<void> };
declare function it(name: string, fn: () => Promise<void>): void;
it("x", async () => {
  const holds: ReturnType<typeof hold>[] = [];
  function hold(message: string) {
    const h = { message, entered: deferred(), claimed: false };
    holds.push(h);
    return h;
  }
  let reply = "a";
  hold("b");
  const found = holds.find((c) => !c.claimed);
  if (found) found.claimed = true;
  reply.length;
});
