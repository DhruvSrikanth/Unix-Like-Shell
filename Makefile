# Compiler: gcc
GCC = gcc

# Standard:
STD = gnu11

# User flags:
# -h   print this message
# -v   print additional diagnostic information
# -p   do not emit a command prompt


# Recipes:
shell: run_shell remove_shell_executable

compile_shell: ./tsh.c
	$(GCC) -std=$(STD) -o tsh ./tsh.c

remove_shell_executable: ./tsh
	rm ./tsh

run_shell: compile_shell
	./tsh
