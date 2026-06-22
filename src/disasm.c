// disasm.c : tiny HL disassembler for debugging the boot.
//   ./disasm file.hl <substr>   -> find functions referencing a string and dump them
//   ./disasm file.hl @<findex>  -> dump function by findex + list its callers
//   ./disasm file.hl G<global>  -> dump the String constant for a global index
// Build:  cc -I <hashlink>/src disasm.c <hashlink>/src/code.c -lhl -lm -o disasm
#include <hl.h>
#include <hlmodule.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static hl_code *code;
static char **fname; static int nfindex;
static void build_names(void){
	nfindex=code->nfunctions+code->nnatives; fname=calloc(nfindex,sizeof(char*));
	for(int i=0;i<code->nnatives;i++){ hl_native*n=&code->natives[i]; char b[256]; snprintf(b,256,"%s@%s",n->lib,n->name); fname[n->findex]=strdup(b); }
	for(int i=0;i<code->nfunctions;i++){ char b[32]; snprintf(b,32,"f%d",code->functions[i].findex); fname[code->functions[i].findex]=strdup(b); }
}
static const char* fn(int fx){ return (fx>=0&&fx<nfindex&&fname[fx])?fname[fx]:"?"; }
static void dumpop(hl_function*fnc,int j){ hl_opcode*o=&fnc->ops[j];
	printf("  %4d %-16s p1=%d p2=%d p3=%d",j,hl_op_name(o->op),o->p1,o->p2,o->p3);
	if(o->op==5) printf("  ; \"%s\"",code->strings[o->p2]);
	else if(o->op==1) printf("  ; int=%d",code->ints[o->p2]);
	else if(o->op>=24&&o->op<=29) printf("  ; call %s",fn(o->p2));
	else if(o->op==33||o->op==34) printf("  ; closure %s",fn(o->p2));
	else if(o->op==36||o->op==37) printf("  ; g%d",o->p2);
	printf("\n");
}

int main(int argc,char**argv){
	if(argc<3){ fprintf(stderr,"usage: disasm file.hl <substr|@findex|G<global>>\n"); return 1; }
	FILE*f=fopen(argv[1],"rb"); if(!f){perror("open");return 1;} fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
	unsigned char*data=malloc(sz); if(fread(data,1,sz,f)!=(size_t)sz){perror("read");return 1;} fclose(f);
	hl_global_init(); hl_register_thread(&f);
	char*err=NULL; code=hl_code_read(data,(int)sz,&err); if(!code){fprintf(stderr,"parse: %s\n",err?err:"?");return 2;}
	build_names();

	if(argv[2][0]=='G'){ int g=atoi(argv[2]+1);
		for(int c=0;c<code->nconstants;c++){ hl_constant*cc=&code->constants[c]; if(cc->global!=g) continue;
			printf("global g%d: nfields=%d\n",g,cc->nfields);
			for(int k=0;k<cc->nfields;k++){ int vi=cc->fields[k]; printf("  field[%d]=%d",k,vi);
				if(k==0 && vi>=0 && vi<code->nstrings){ printf("  str=\"%s\" (len %d, bytes:",code->strings[vi],code->strings_lens[vi]);
					for(int b=0;b<code->strings_lens[vi]&&b<8;b++) printf(" %02x",(unsigned char)code->strings[vi][b]); printf(")"); }
				printf("\n"); } }
		return 0;
	}
	if(argv[2][0]=='@'){ int target=atoi(argv[2]+1);
		for(int i=0;i<code->nfunctions;i++){ hl_function*fnc=&code->functions[i]; if(fnc->findex!=target) continue;
			printf("==== f%d (nops=%d nregs=%d) ====\n",target,fnc->nops,fnc->nregs);
			for(int j=0;j<fnc->nops;j++) dumpop(fnc,j); }
		printf("\n-- callers of f%d --\n",target);
		for(int i=0;i<code->nfunctions;i++){ hl_function*fnc=&code->functions[i];
			for(int j=0;j<fnc->nops;j++){ hl_opcode*o=&fnc->ops[j];
				if((o->op>=24&&o->op<=29)&&o->p2==target){ printf("  f%d @op%d\n",fnc->findex,j); break; }
				if((o->op==33||o->op==34)&&o->p2==target){ printf("  f%d @op%d (closure)\n",fnc->findex,j); break; } } }
		return 0;
	}
	// substring search across strings + String-constant globals
	int hits[64]; int nh=0;
	for(int i=0;i<code->nstrings;i++){ if(strstr(code->strings[i],argv[2])){ if(nh<64)hits[nh++]=i; } }
	printf("string matches for '%s': %d\n",argv[2],nh);
	for(int k=0;k<nh;k++) printf("  @%d = \"%s\"\n",hits[k],code->strings[hits[k]]);
	int tg[64]; int ntg=0;
	for(int c=0;c<code->nconstants;c++){ hl_constant*cc=&code->constants[c]; if(cc->nfields<1) continue;
		for(int k=0;k<nh;k++) if(cc->fields[0]==hits[k]){ if(ntg<64) tg[ntg++]=cc->global; printf("  const global g%d -> \"%s\"\n",cc->global,code->strings[hits[k]]); } }
	for(int i=0;i<code->nfunctions;i++){ hl_function*fnc=&code->functions[i]; int ref=-1;
		for(int j=0;j<fnc->nops;j++){ hl_opcode*o=&fnc->ops[j];
			if(o->op==5){ for(int k=0;k<nh;k++) if(o->p2==hits[k]){ref=j;break;} }
			if(o->op==36){ for(int k=0;k<ntg;k++) if(o->p2==tg[k]){ref=j;break;} }
			if(ref>=0)break; }
		if(ref<0) continue;
		printf("\n==== f%d (nops=%d nregs=%d) refs string at op %d ====\n",fnc->findex,fnc->nops,fnc->nregs,ref);
		int from=ref-40; if(from<0)from=0; int to=ref+25; if(to>fnc->nops)to=fnc->nops;
		for(int j=from;j<to;j++) dumpop(fnc,j);
	}
	return 0;
}
