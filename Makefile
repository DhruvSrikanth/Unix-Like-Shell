# Compiler: gcc
GCC = gcc

# Standard:
STD = gnu11

# Home:
ROOT = home/root

# User flags:
# -h   print this message
# -v   print additional diagnostic information
# -p   do not emit a command prompt


# Recipes:
shell: run remove_exe

compile: ./tsh.c
	$(GCC) -std=$(STD) -o tsh ./tsh.c

remove_exe: 
	@rm ./tsh

run: compile
	./tsh

reset: remove_exe
	@rm -rf proc/*
	@rm -rf $(ROOT)/*
	@mkdir $(ROOT)
	@touch $(ROOT)/.tsh_history

help: compile
	./tsh -h
	@rm ./tsh
