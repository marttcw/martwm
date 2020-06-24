all:
	gcc -o mmdtwm\
		-lxcb -lxcb-randr -lxcb-keysyms\
		-Isrc\
		-std=c99 -pedantic-errors -pedantic -Wall -msse2 -Wpointer-arith\
		-Wstrict-prototypes\
		-fomit-frame-pointer -ffast-math\
		-g\
		src/main.c

#-flto -Os    | -g for debugging

