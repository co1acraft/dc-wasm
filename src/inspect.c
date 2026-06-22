// inspect.c : validate a .hl/hlboot.dat parse and profile opcodes/types/natives.
// Build:  cc -I <hashlink>/src inspect.c <hashlink>/src/code.c -lhl -lm -o inspect
// Usage:  ./inspect hlboot.dat
#include <hl.h>
#include <hlmodule.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "usage: inspect file.hl\n"); return 1; }
	FILE *f = fopen(argv[1], "rb");
	if (!f) { perror("open"); return 1; }
	fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
	unsigned char *data = (unsigned char*)malloc(sz);
	if (fread(data, 1, sz, f) != (size_t)sz) { perror("read"); return 1; }
	fclose(f);

	hl_global_init();
	hl_register_thread(&f);
	char *err = NULL;
	hl_code *code = hl_code_read(data, (int)sz, &err);
	if (!code) { fprintf(stderr, "PARSE ERROR: %s\n", err ? err : "?"); return 2; }

	printf("PARSE OK  version=%d entrypoint=%d hasdebug=%d\n", code->version, code->entrypoint, code->hasdebug);
	printf("nfunctions=%d nnatives=%d ntypes=%d nglobals=%d nconstants=%d\n",
		code->nfunctions, code->nnatives, code->ntypes, code->nglobals, code->nconstants);
	printf("nints=%d nfloats=%d nstrings=%d\n", code->nints, code->nfloats, code->nstrings);

	long counts[256]; memset(counts, 0, sizeof(counts));
	long total = 0, maxops = 0; int maxfn = -1;
	for (int i = 0; i < code->nfunctions; i++) {
		hl_function *fn = &code->functions[i];
		if (fn->nops > maxops) { maxops = fn->nops; maxfn = fn->findex; }
		for (int j = 0; j < fn->nops; j++) { int op = fn->ops[j].op; if (op >= 0 && op < 256) counts[op]++; total++; }
	}
	int distinct = 0; for (int i = 0; i < 256; i++) if (counts[i]) distinct++;
	printf("total_opcodes=%ld distinct_opcodes=%d largest_fn_ops=%ld (findex %d)\n", total, distinct, maxops, maxfn);

	int maxregs=0, maxregs_fx=-1, over40k=0, over50k=0;
	for (int i = 0; i < code->nfunctions; i++) { hl_function *fn=&code->functions[i];
		if (fn->nregs>maxregs){ maxregs=fn->nregs; maxregs_fx=fn->findex; }
		if (fn->nregs>40000) over40k++; if (fn->nregs>50000) over50k++; }
	printf("max_nregs=%d (findex %d); funcs_over_40k=%d funcs_over_50k=%d entrypoint=%d\n",
		maxregs, maxregs_fx, over40k, over50k, code->entrypoint);

	printf("--- opcode histogram ---\n");
	for (int i = 0; i < 256; i++) if (counts[i]) printf("%3d %-18s %ld\n", i, hl_op_name(i), counts[i]);

	const char *libs[64]; int nlibs = 0;
	for (int i = 0; i < code->nnatives; i++) {
		const char *lib = code->natives[i].lib; int found = 0;
		for (int k = 0; k < nlibs; k++) if (strcmp(libs[k], lib) == 0) { found = 1; break; }
		if (!found && nlibs < 64) libs[nlibs++] = lib;
	}
	printf("--- native libs (%d distinct over %d natives) ---\n", nlibs, code->nnatives);
	for (int k = 0; k < nlibs; k++) {
		int c = 0; for (int i = 0; i < code->nnatives; i++) if (strcmp(code->natives[i].lib, libs[k]) == 0) c++;
		printf("  %-10s %d\n", libs[k], c);
	}
	return 0;
}
