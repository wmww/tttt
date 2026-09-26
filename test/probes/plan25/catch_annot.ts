// @strict: true
try { } catch (e: string) { }
try { } catch ({ message }) { message; }
try { } catch ({ message }: any) { message; }
try { } catch (e) { if (e instanceof Error) e.message; }
