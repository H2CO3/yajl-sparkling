all:
	clang -std=c99 -pedantic -dynamiclib -Wall -o yajl_spn.dylib -DUSE_DYNAMIC_LOADING yajl_sparkling.c -lyajl -lspn

clean:
	rm -f yajl_spn.dylib
