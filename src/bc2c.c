// bc2c: HashLink bytecode (.hl / hlboot.dat) -> HL/C source transpiler.
//
// Reuses HashLink's own bytecode reader (src/code.c) to deserialize into the
// in-memory hl_code* structure, then emits C that links against libhl + the
// HL/C runtime (hlc_main.c), mirroring the Haxe compiler's hl2c.ml generator
// but with an index-based naming scheme (T<i> types, O<i> objects, f<findex>
// functions, g<i> globals).
//
// Output is split into multiple translation units (dc.h + dc_types.c +
// dc_f<N>.c chunks + dc_main.c + dc_stubs.c) so a huge game (Dead Cells:
// 37k functions, 25k types) is actually compilable.
//
// Build:  cc -I <hashlink>/src bc2c.c <hashlink>/src/code.c -lhl -o bc2c
// Usage:  ./bc2c hlboot.dat <out_dir>
#include <hl.h>
#include <hlmodule.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static hl_code *code;
static FILE *o;
static int tabs = 0;

/* ----- output helpers ----- */
static void wt(void){ for(int i=0;i<tabs;i++) fputc('\t',o); }
#define LN(...)  do{ wt(); fprintf(o,__VA_ARGS__); fputc('\n',o);}while(0)
#define EX(...)  do{ wt(); fprintf(o,__VA_ARGS__); fputs(";\n",o);}while(0)

/* ring of scratch buffers so multiple helpers can be used in one fprintf */
#define NB 96
#define BS 2048
static char ringbuf[NB][BS];
static int ringi = 0;
static char *gb(void){ ringi=(ringi+1)%NB; return ringbuf[ringi]; }

static int tidx(hl_type *t){ int i=(int)(t-code->types); if(i<0||i>=code->ntypes){fprintf(stderr,"bad type ptr %d\n",i);exit(3);} return i; }

/* ----- ustring helpers ----- */
static void us2a(const uchar*s,char*out){ int i=0; if(s) for(;s[i];i++) out[i]=(char)s[i]; out[i]=0; }

/* ----- C identifier sanitizing (hl2c ident) ----- */
static int is_kw(const char*s){
	static const char*k[]={"auto","bool","break","case","char","const","continue","default","do","double","else","enum","extern","float","for","goto","if","int","long","register","return","short","signed","sizeof","static","struct","switch","typedef","union","unsigned","void","volatile","while","NULL","true","false","asm","typeof","inline","restrict","_restrict","t",0};
	for(int i=0;k[i];i++) if(!strcmp(s,k[i])) return 1; return 0;
}
static char* ident(const char*s){
	char*b=gb();
	if(is_kw(s)||(s[0]=='_'&&s[1]=='_')) snprintf(b,BS,"_hx_%s",s); else snprintf(b,BS,"%s",s);
	return b;
}

/* ----- type -> C ----- */
static const char* ctype_base(hl_type*t,int*nptr){
	*nptr=0;
	switch(t->kind){
	case HVOID: return "void";
	case HUI8: return "unsigned char";
	case HUI16: return "unsigned short";
	case HI32: return "int";
	case HI64: return "int64";
	case HF32: return "float";
	case HF64: return "double";
	case HBOOL: return "bool";
	case HBYTES: *nptr=1; return "vbyte";
	case HDYN: *nptr=1; return "vdynamic";
	case HFUN: *nptr=1; return "vclosure";
	case HOBJ: case HSTRUCT: { char*b=gb(); snprintf(b,BS,"O%d",tidx(t)); return b; }
	case HARRAY: *nptr=1; return "varray";
	case HTYPE: *nptr=1; return "hl_type";
	case HREF: { int n; const char*bb=ctype_base(t->tparam,&n); *nptr=n+1; return bb; }
	case HVIRTUAL: *nptr=1; return "vvirtual";
	case HDYNOBJ: *nptr=1; return "vdynobj";
	case HABSTRACT: { char*b=gb(); char a[256]; us2a(t->abs_name,a); snprintf(b,BS,"%s",a); *nptr=1; return b; }
	case HENUM: *nptr=1; return "venum";
	case HNULL: *nptr=1; return "vdynamic";
	case HMETHOD: *nptr=1; return "void";
	case HPACKED: { int n; const char*bb=ctype_base(t->tparam,&n); char*b=gb(); snprintf(b,BS,"struct _%s",bb); *nptr=n; return b; }
	default: return "void";
	}
}
static char* ctype(hl_type*t){
	int n; const char*b=ctype_base(t,&n);
	char*r=gb(); int l=snprintf(r,BS,"%s",b); for(int i=0;i<n&&l<BS-1;i++) r[l++]='*'; r[l]=0; return r;
}
static char* tval(hl_type*t){ char*b=gb(); snprintf(b,BS,"&T%d",tidx(t)); return b; }

static int tsame(hl_type*a,hl_type*b){ return a==b; }
static int is_ptr(hl_type*t){ switch(t->kind){case HVOID:case HUI8:case HUI16:case HI32:case HI64:case HF32:case HF64:case HBOOL:return 0;default:return 1;} }
static int is_dyn(hl_type*t){ switch(t->kind){case HDYN:case HFUN:case HOBJ:case HARRAY:case HVIRTUAL:case HDYNOBJ:case HNULL:case HENUM:return 1;default:return 0;} }
static int is_gcptr(hl_type*t){ switch(t->kind){case HVOID:case HUI8:case HUI16:case HI32:case HI64:case HF32:case HF64:case HBOOL:case HTYPE:case HREF:case HMETHOD:case HPACKED:return 0;default:return 1;} }

/* ----- self-contained hashing (ordered, collision-avoiding; mirrors hl_hash_gen) ----- */
typedef struct { int h; const uchar*name; } hentry;
static hentry *htab=NULL; static int hcount=0,hcap=0;
static const uchar **horder=NULL; static int hordn=0,hordc=0;
static int upure(const uchar*s){ int h=0; while(*s){ h=223*h+(unsigned)*s; s++; } h%=0x1FFFFF7B; return h; }
static int my_ucmp(const uchar*a,const uchar*b){ while(*a&&*a==*b){a++;b++;} return (int)*a-(int)*b; }
static uchar EMPTY_USTR[1]={0};
static int myhash(const uchar*name){
	if(!name) name=EMPTY_USTR;
	int h=upure(name);
	for(;;){ int f=-1; for(int i=0;i<hcount;i++) if(htab[i].h==h){f=i;break;}
		if(f<0) break; if(my_ucmp(htab[f].name,name)==0) return h; h++; }
	if(hcount==hcap){ hcap=hcap?hcap*2:512; htab=realloc(htab,sizeof(hentry)*hcap); }
	htab[hcount].h=h; htab[hcount].name=name; hcount++;
	if(hordn==hordc){ hordc=hordc?hordc*2:512; horder=realloc(horder,sizeof(void*)*hordc); }
	horder[hordn++]=name; return h;
}

/* ----- string emission ----- */
static void esc_into(char*b,int*pp,const char*s,int len){
	int p=*pp;
	for(int i=0;i<len&&p<BS-8;i++){ unsigned char c=(unsigned char)s[i];
		if(c=='"'||c=='\\') p+=snprintf(b+p,BS-p,"\\%c",c);
		else if(c=='\n') p+=snprintf(b+p,BS-p,"\\n");
		else if(c=='\t') p+=snprintf(b+p,BS-p,"\\t");
		else if(c=='\r') p+=snprintf(b+p,BS-p,"\\r");
		else if(c>=0x20&&c<0x7f){ b[p++]=c; b[p]=0; }
		else p+=snprintf(b+p,BS-p,"\\%03o",c);
	}
	*pp=p;
}
// USTR-able only if short and pure ASCII (printable + \t\n\r); else UTF-16 byte array
static int usrt_able(int sid){ int len=code->strings_lens[sid]; if(len>=60) return 0; const char*s=code->strings[sid];
	for(int i=0;i<len;i++){ unsigned char c=s[i]; if(c=='\t'||c=='\n'||c=='\r') continue; if(c<0x20||c>=0x7f) return 0; } return 1; }
static char* sstr(int sid){ /* string-table index -> USTR(...) or hlstr$ ref */
	char*b=gb();
	if(usrt_able(sid)){ int p=snprintf(b,BS,"USTR(\"");
		esc_into(b,&p,code->strings[sid],code->strings_lens[sid]); snprintf(b+p,BS-p,"\")"); }
	else snprintf(b,BS,"hlstr$%d",sid);
	return b;
}
static char* ustr(const uchar*s){ /* ustring -> USTR(...) (names; assumed short/ASCII) */
	char*b=gb(); int p=snprintf(b,BS,"USTR(\"");
	if(s) for(int i=0;s[i]&&p<BS-8;i++){ unsigned c=s[i];
		if(c=='"'||c=='\\') p+=snprintf(b+p,BS-p,"\\%c",c);
		else if(c=='\n') p+=snprintf(b+p,BS-p,"\\n");
		else if(c>=0x20&&c<0x7f){ b[p++]=(char)c; b[p]=0; }
		else p+=snprintf(b+p,BS-p,"\\%03o",c);
	}
	snprintf(b+p,BS-p,"\")"); return b;
}

/* ----- function table ----- */
static char **fname; static hl_type **ftype; static int nfindex;
static const char* native_cname(int nat){
	hl_native*n=&code->natives[nat]; const char*l=n->lib,*nm=n->name; if(l[0]=='?') l++;
	char*b=gb(); if(!strcmp(l,"std")) snprintf(b,BS,"hl_%s",nm); else snprintf(b,BS,"%s_%s",l,nm);
	if(!strcmp(b,"hl_tls_get")){ char*b2=gb(); snprintf(b2,BS,"hl_tls_get_w"); return b2; }
	return b;
}
static void build_ftable(void){
	nfindex=code->nfunctions+code->nnatives;
	fname=calloc(nfindex,sizeof(char*)); ftype=calloc(nfindex,sizeof(hl_type*));
	for(int i=0;i<code->nnatives;i++){ hl_native*n=&code->natives[i]; ftype[n->findex]=n->t; fname[n->findex]=strdup(native_cname(i)); }
	for(int i=0;i<code->nfunctions;i++){ hl_function*f=&code->functions[i]; ftype[f->findex]=f->type; char b[64]; snprintf(b,64,"f%d",f->findex); fname[f->findex]=strdup(b); }
}

/* ----- field resolution ----- */
static void resolve_field(hl_type*ot,int fid,const uchar**pn,hl_type**pt){
	hl_type*chain[256]; int n=0; hl_type*c=ot; while(c&&n<256){ chain[n++]=c; c=c->obj->super; }
	for(int i=n-1;i>=0;i--){ hl_type_obj*ob=chain[i]->obj; if(fid<ob->nfields){ *pn=ob->fields[fid].name; *pt=ob->fields[fid].t; return; } fid-=ob->nfields; }
	*pn=NULL; *pt=&code->types[0];
}
static char* objfield_name(const uchar*name,int absfid){
	char*b=gb(); if(!name||name[0]==0) snprintf(b,BS,"f$%d",absfid);
	else { char a[1024]; us2a(name,a); snprintf(b,BS,"%s",ident(a)); } return b;
}
static char* enum_cname(hl_type*et,int cid){
	char*b=gb(); if(et->tenum->constructs[cid].nparams==0) snprintf(b,BS,"venum"); else snprintf(b,BS,"EC%d_%d",tidx(et),cid); return b;
}

static char DYNT[32];

#include "bc2c_emit.h"

int main(int argc,char**argv){
	if(argc<3){ fprintf(stderr,"usage: bc2c in.hl out_dir\n"); return 1; }
	FILE*f=fopen(argv[1],"rb"); if(!f){perror("open");return 1;}
	fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
	unsigned char*data=malloc(sz); if(fread(data,1,sz,f)!=(size_t)sz){perror("read");return 1;} fclose(f);
	hl_global_init(); hl_register_thread(&f);
	char*err=NULL; code=hl_code_read(data,(int)sz,&err);
	if(!code){ fprintf(stderr,"parse error: %s\n",err?err:"?"); return 2; }
	build_ftable();
	{ int di=-1; for(int i=0;i<code->ntypes;i++) if(code->types[i].kind==HDYN){di=i;break;} if(di>=0) snprintf(DYNT,32,"&T%d",di); else snprintf(DYNT,32,"&hlt_dyn"); }

	const char*dir=argv[2]; char path[1024];
	#define OPEN_UNIT(p) do{ o=fopen(p,"wb"); if(!o){perror(p);return 1;} fprintf(o,"#define HLC_BOOT\n#include \"dc.h\"\n"); }while(0)

	snprintf(path,sizeof(path),"%s/dc.h",dir); o=fopen(path,"wb"); if(!o){perror(path);return 1;} emit_header(); fclose(o);
	snprintf(path,sizeof(path),"%s/dc_types.c",dir); OPEN_UNIT(path);
	emit_type_values(); emit_type_payloads(); emit_globals(); emit_init_types(); emit_reflection(); fclose(o);
	int CHUNK=1500, nchunks=0;
	for(int start=0;start<code->nfunctions;start+=CHUNK){ int end=start+CHUNK; if(end>code->nfunctions) end=code->nfunctions;
		snprintf(path,sizeof(path),"%s/dc_f%d.c",dir,nchunks); OPEN_UNIT(path);
		for(int i=start;i<end;i++) generate_function(&code->functions[i]);
		fclose(o); nchunks++;
	}
	snprintf(path,sizeof(path),"%s/dc_main.c",dir); OPEN_UNIT(path);
	emit_func_tables(); emit_init_hashes(); emit_entry(); fclose(o);
	snprintf(path,sizeof(path),"%s/dc_stubs.c",dir); OPEN_UNIT(path);
	emit_native_stubs(); fclose(o);

	fprintf(stderr,"bc2c: %s -> %s/ (%d funcs in %d chunks, %d types, %d natives)\n",argv[1],dir,code->nfunctions,nchunks,code->ntypes,code->nnatives);
	return 0;
}
