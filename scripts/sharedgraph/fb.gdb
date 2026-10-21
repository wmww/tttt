set pagination off
set print thread-events off
break graph_fallback_hook
commands
silent
bt 40
continue
end
run
