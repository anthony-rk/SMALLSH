# Type "make" in the directory to build the program
base:		smallsh.c
	  gcc -std=c99 -o smallsh smallsh.c 

# Type "make test" in the directory to to run the testscript
test:
		/class/cs344/assignments/02-smallsh/testscript.sh ./smallsh

# Type "make run" in the directory to build and run the program
run: base
		PS1="$$ " ./smallsh

# Type "make leaks" to check for leaks
leaks: base
	PS1="$$ " valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all ./smallsh
