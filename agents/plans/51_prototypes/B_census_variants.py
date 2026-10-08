#!/usr/bin/env python3
# Census variants: each a list of (file, old, new) replacements on a clean tree. Usage: variants.py NAME
import sys
import os
R = os.environ.get('VROOT', '/tmp/wt-51b/')
V = {
 # KEY_CONTEXT_RETURN_SELF never written (its readers: function.c's "context is its own type", is_self)
 'self': [('src/ty/expr.c',
   '''        *intern_slot(me, &my links, (u64[]){ KEY_CONTEXT_RETURN_SELF, (u64)(uintptr_t)function }, 2) = function;
''', '')],
 # KEY_CONTEXT_NONE never written -> KEY_FUNCTION_SIG_CONTEXT and ty_late_contextual_return dead
 'none': [('src/ty/expr.c',
   '''                    *intern_slot(me, &my links, none_key, 2) = my resolving_call;
''', '')],
 # only ty_late_contextual_return off (KEY_CONTEXT_NONE still decides is_self and the first-check rule)
 'late': [('src/ty/expr.c',
   '''    CallExpr const* call = intern_get(&my links, (u64[]){ KEY_CONTEXT_NONE, (u64)(uintptr_t)function }, 2);
    if (!call''', '''    CallExpr const* call = nullptr;
    if (!call''')],
 # TS7024 context-is-self off
 'isself': [('src/ty/expr.c',
   '''bool ty_function_context_is_self(Checker* me, FunctionImpl const* function) {
''', '''bool ty_function_context_is_self(Checker* me, FunctionImpl const* function) {
    if (me) return false;
''')],
 # KEY_FUNCTION_CONTEXT fallback in widening_reportable
 'fctx': [('src/ty/expr.c',
   '''        Ty const* contextual = intern_get(&my links, (u64[]){ KEY_FUNCTION_CONTEXT, (u64)(uintptr_t)function }, 2);
        sig = contextual''', '''        Ty const* contextual = nullptr;
        sig = contextual''')],
 'iife': [('src/ty/expr.c',
   '''        if (!*slot) *slot = ctx.contextual;
    }
    ty_assign_iife_parameter_types(me, call);''', '''        (void)slot;
    }
    ty_assign_iife_parameter_types(me, call);''')],
 'lparent': [('src/ty/expr.c',
   '''            *intern_slot(me, &my links, parent_key, 2) = expr;
''', '')],
 'linfer': [('src/ty/expr.c',
   '''        if (serial || intern_get(&my links, inference_key, 2)) {''', '''        if (false) {''')],
 # `this` of literal methods doesn't read the literal's contextual type (the pattern check still does)
 'lctx': [('src/ty/expr.c',
   '''        for (u32 depth = 0; outer && depth < 32; depth++) {
            u64 key[2] = { KEY_LITERAL_CONTEXT, (u64)(uintptr_t)outer };
            Ty const* outer_context = intern_get(&my links, key, 2);''', '''        contextual = nullptr;
        for (u32 depth = 0; outer && depth < 32; depth++) {
            Ty const* outer_context = nullptr;''')],
 # generic-mode restore of KEY_LITERAL_CONTEXT
 'lctxgen': [('src/ty/expr.c',
   '''            if (recorded) *intern_slot(me, &my links, context_key, 2) = recorded;''', '''            (void)recorded;''')],
 # KEY_CONTEXT_RETURN: first write wins instead of last
 'retfirst': [('src/ty/expr.c',
   '''    if (ret && !(ret->flags & ignored)) *intern_slot(me, &my links, key, 2) = ret;''',
   '''    if (ret && !(ret->flags & ignored) && !intern_get(&my links, key, 2)) *intern_slot(me, &my links, key, 2) = ret;''')],
 # the expression memo: the first contextual check wins (a later one with another context recomputes, doesn't store)
 'memofirst': [('src/ty/expr.c',
   '''    lazy_type_set(lazy, ret, ctx.contextual);
    return ret;
}''', '''    if (memo && memo->done && memo->context) return ret;
    lazy_type_set(lazy, ret, ctx.contextual);
    return ret;
}''')],
 'widening': [('src/ty/expr.c',
   '''    if (compiler_options()->strict_null_checks || !compiler_options()->no_implicit_any || function->ret_explicit) return;
    Signature const** slot''', '''    if (me) return;
    Signature const** slot''')],
 'blocked': [('src/ty/expr.c',
   '''                *intern_slot(me, &my links, (u64[]){ KEY_CONTEXT_BLOCKED, (u64)(uintptr_t)expr->function }, 2) = expr->function;''',
   '''                (void)0;''')],
}
V['litall'] = V['lparent'] + V['linfer'] + V['lctx'] + V['lctxgen']
V['nonegroup'] = V['none'] + V['isself']
for name in sys.argv[1].split('+'):
    for f, old, new in V[name]:
        p = R + f
        s = open(p).read()
        assert s.count(old) == 1, (name, old[:60], s.count(old))
        open(p, 'w').write(s.replace(old, new))
print('applied', sys.argv[1])
