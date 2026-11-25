// bc2c_emit.h : emitters (included by bc2c.c after the helpers, before main)

static const char* kname(hl_type_kind k){
	switch(k){case HVOID:return"HVOID";case HUI8:return"HUI8";case HUI16:return"HUI16";case HI32:return"HI32";case HI64:return"HI64";case HF32:return"HF32";case HF64:return"HF64";case HBOOL:return"HBOOL";case HBYTES:return"HBYTES";case HDYN:return"HDYN";case HFUN:return"HFUN";case HOBJ:return"HOBJ";case HARRAY:return"HARRAY";case HTYPE:return"HTYPE";case HREF:return"HREF";case HVIRTUAL:return"HVIRTUAL";case HDYNOBJ:return"HDYNOBJ";case HABSTRACT:return"HABSTRACT";case HENUM:return"HENUM";case HNULL:return"HNULL";case HMETHOD:return"HMETHOD";case HSTRUCT:return"HSTRUCT";case HPACKED:return"HPACKED";default:return"HVOID";}
}
static char* args_repr(hl_type*ft){
	char*b=gb(); hl_type_fun*fu=ft->fun; if(fu->nargs==0){ snprintf(b,BS,"void"); return b; }
	int p=0; for(int k=0;k<fu->nargs;k++){ char*c=ctype(fu->args[k]); p+=snprintf(b+p,BS-p,"%s%s",k?",":"",c);} return b;
}
static const char* dyn_prefix(hl_type*t){ switch(t->kind){case HUI8:case HUI16:case HI32:case HBOOL:return"i";case HF32:return"f";case HF64:return"d";case HI64:return"i64";default:return"p";} }
static char* tvopt(hl_type*t){ char*b=gb(); switch(t->kind){case HF32:case HF64:case HI64:b[0]=0;break;default:snprintf(b,BS,",&T%d",tidx(t));} return b; }
static const char* dynfield(hl_type*t){ switch(t->kind){case HUI8:return"->v.ui8";case HUI16:return"->v.ui16";case HI32:return"->v.i";case HI64:return"->v.i64";case HF32:return"->v.f";case HF64:return"->v.d";case HBOOL:return"->v.b";default:return"->v.ptr";} }

static void emit_types(void){
	// abstracts (dedup)
	for(int i=0;i<code->ntypes;i++){ hl_type*t=&code->types[i]; if(t->kind!=HABSTRACT) continue;
		char a[256]; us2a(t->abs_name,a);
		if(!strcmp(a,"hl_tls")||!strcmp(a,"hl_mutex")||!strcmp(a,"hl_thread")) continue;
		int dup=0; for(int j=0;j<i;j++){ if(code->types[j].kind==HABSTRACT){ char b[256]; us2a(code->types[j].abs_name,b); if(!strcmp(a,b)){dup=1;break;} } }
		if(!dup) LN("typedef struct _%s %s;",a,a);
	}
	for(int i=0;i<code->ntypes;i++){ hl_type*t=&code->types[i]; if(t->kind==HOBJ||t->kind==HSTRUCT) LN("typedef struct _O%d *O%d;",i,i); }
	for(int i=0;i<code->ntypes;i++){ hl_type*t=&code->types[i]; if(t->kind!=HENUM) continue;
		for(int c=0;c<t->tenum->nconstructs;c++){ hl_enum_construct*ec=&t->tenum->constructs[c]; if(!ec->nparams) continue;
			LN("typedef struct {"); tabs++; LN("HL__ENUM_CONSTRUCT__");
			for(int p=0;p<ec->nparams;p++) EX("%s p%d",ctype(ec->params[p]),p);
			tabs--; LN("} EC%d_%d;",i,c);
		}
	}
	for(int i=0;i<code->ntypes;i++){ hl_type*t=&code->types[i]; if(t->kind!=HOBJ&&t->kind!=HSTRUCT) continue;
		LN("struct _O%d {",i); tabs++;
		hl_type*chain[256]; int n=0; hl_type*c=t; while(c&&n<256){ chain[n++]=c; c=c->obj->super; }
		if(t->kind!=HSTRUCT) LN("hl_type *$type;");
		for(int ci=n-1;ci>=0;ci--){ hl_type_obj*ob=chain[ci]->obj; int base=0; hl_type*s=chain[ci]->obj->super; while(s){base+=s->obj->nfields;s=s->obj->super;}
			for(int fi=0;fi<ob->nfields;fi++){ hl_obj_field*fl=&ob->fields[fi]; EX("%s %s",ctype(fl->t),objfield_name(fl->name,base+fi)); } }
		tabs--; LN("};");
	}
}

static void emit_natives(void){
	LN("// natives");
	char **seen=calloc(code->nnatives,sizeof(char*)); int ns=0;
	for(int i=0;i<code->nnatives;i++){ const char*cn=native_cname(i); hl_type*t=code->natives[i].t;
		int dup=0; for(int j=0;j<ns;j++) if(!strcmp(seen[j],cn)){dup=1;break;} if(dup) continue; seen[ns++]=strdup(cn);
		if(!strcmp(cn,"hl_tls_get_w")){ LN("#define hl_tls_get_w(tls) ((vdynamic*)hl_tls_get(tls))"); continue; }
		if(!strcmp(cn,"hl_tls_set")) continue;
		EX("HL_API %s %s(%s)",ctype(t->fun->ret),cn,args_repr(t));
	}
}

static void emit_type_values(void){
	LN("// type values");
	for(int i=0;i<code->ntypes;i++){ hl_type*t=&code->types[i]; EX("hl_type T%d = { %s }",i,kname(t->kind)); }
}
static void emit_type_payloads(void){
	LN("// type payloads");
	for(int i=0;i<code->ntypes;i++){ hl_type*t=&code->types[i];
		if(t->kind==HOBJ||t->kind==HSTRUCT){ hl_type_obj*ob=t->obj;
			char fields[16]="NULL",proto[16]="NULL",bind[16]="NULL";
			if(ob->nfields){ wt(); fprintf(o,"static hl_obj_field fields%d[] = {",i);
				for(int k=0;k<ob->nfields;k++){ hl_obj_field*fl=&ob->fields[k]; fprintf(o,"%s{(const uchar*)%s,%s,%d}",k?",":"",ustr(fl->name),tval(fl->t),myhash(fl->name?fl->name:(const uchar*)"")); }
				fprintf(o,"};\n"); snprintf(fields,16,"fields%d",i); }
			if(ob->nproto){ wt(); fprintf(o,"static hl_obj_proto proto%d[] = {",i);
				for(int k=0;k<ob->nproto;k++){ hl_obj_proto*p=&ob->proto[k]; fprintf(o,"%s{(const uchar*)%s,%d,%d,%d}",k?",":"",ustr(p->name),p->findex,p->pindex,myhash(p->name?p->name:(const uchar*)"")); }
				fprintf(o,"};\n"); snprintf(proto,16,"proto%d",i); }
			if(ob->nbindings){ wt(); fprintf(o,"static int bindings%d[] = {",i);
				for(int k=0;k<ob->nbindings;k++) fprintf(o,"%s%d,%d",k?",":"",ob->bindings[k<<1],ob->bindings[(k<<1)|1]);
				fprintf(o,"};\n"); snprintf(bind,16,"bindings%d",i); }
			wt(); fprintf(o,"static hl_type_obj obj%d = {%d,%d,%d,(const uchar*)%s,%s,%s,%s,%s};\n",
				i,ob->nfields,ob->nproto,ob->nbindings,ustr(ob->name), ob->super?tval(ob->super):"NULL", fields,proto,bind);
		}
		else if(t->kind==HENUM){ hl_type_enum*en=t->tenum;
			for(int c=0;c<en->nconstructs;c++){ hl_enum_construct*ec=&en->constructs[c]; if(!ec->nparams) continue;
				wt(); fprintf(o,"static hl_type *econstruct%d_%d[] = {",i,c); for(int p=0;p<ec->nparams;p++) fprintf(o,"%s%s",p?",":"",tval(ec->params[p])); fprintf(o,"};\n");
				wt(); fprintf(o,"static int eoffsets%d_%d[] = {",i,c); for(int p=0;p<ec->nparams;p++) fprintf(o,"%s0",p?",":""); fprintf(o,"};\n");
			}
			char cn[16]="NULL";
			if(en->nconstructs){ wt(); fprintf(o,"static hl_enum_construct econs%d[] = {",i);
				for(int c=0;c<en->nconstructs;c++){ hl_enum_construct*ec=&en->constructs[c]; int hp=0; for(int p=0;p<ec->nparams;p++) if(is_gcptr(ec->params[p])) hp=1;
					if(ec->nparams) fprintf(o,"%s{(const uchar*)%s,%d,econstruct%d_%d,sizeof(EC%d_%d),%s,eoffsets%d_%d}",c?",":"",ustr(ec->name),ec->nparams,i,c,i,c,hp?"true":"false",i,c);
					else fprintf(o,"%s{(const uchar*)%s,0,NULL,0,false,NULL}",c?",":"",ustr(ec->name)); }
				fprintf(o,"};\n"); snprintf(cn,16,"econs%d",i); }
			int anon = (!en->name||en->name[0]==0);
			wt(); fprintf(o,"static hl_type_enum tenum%d = {",i);
			if(anon) fprintf(o,"NULL"); else fprintf(o,"(const uchar*)%s",ustr(en->name));
			fprintf(o,",%d,%s,NULL};\n",en->nconstructs,cn);
		}
		else if(t->kind==HVIRTUAL){ hl_type_virtual*v=t->virt; char fn[16]="NULL";
			if(v->nfields){ wt(); fprintf(o,"static hl_obj_field vfields%d[] = {",i);
				for(int k=0;k<v->nfields;k++){ hl_obj_field*fl=&v->fields[k]; fprintf(o,"%s{(const uchar*)%s,%s,%d}",k?",":"",ustr(fl->name),tval(fl->t),myhash(fl->name?fl->name:(const uchar*)"")); }
				fprintf(o,"};\n"); snprintf(fn,16,"vfields%d",i); }
			EX("static hl_type_virtual virt%d = {%s,%d}",i,fn,v->nfields);
		}
		else if(t->kind==HFUN||t->kind==HMETHOD){ hl_type_fun*fu=t->fun; char an[16]="NULL";
			if(fu->nargs){ wt(); fprintf(o,"static hl_type *fargs%d[] = {",i); for(int k=0;k<fu->nargs;k++) fprintf(o,"%s%s",k?",":"",tval(fu->args[k])); fprintf(o,"};\n"); snprintf(an,16,"fargs%d",i); }
			EX("static hl_type_fun tfun%d = {%s,%s,%d}",i,an,tval(fu->ret),fu->nargs);
		}
	}
}
static int class_global(hl_type*t){ int raw=(int)(int_val)t->obj->global_value; return raw?raw-1:-1; }
static int enum_global(hl_type*t){ int raw=(int)(int_val)t->tenum->global_value; return raw?raw-1:-1; }

static void emit_init_types(void){
	int B=800,nb=0;
	for(int start=0;start<code->ntypes;start+=B){ int end=start+B; if(end>code->ntypes)end=code->ntypes;
		LN("static void hl_init_types_%d( hl_module_context *ctx ) {",nb); tabs++;
		for(int i=start;i<end;i++){ hl_type*t=&code->types[i];
			switch(t->kind){
			case HOBJ: case HSTRUCT:{ EX("obj%d.m = ctx",i); int g=class_global(t); if(g>=0) EX("obj%d.global_value = (void**)&g%d",i,g); EX("T%d.obj = &obj%d",i,i); } break;
			case HNULL: case HREF: case HPACKED: EX("T%d.tparam = %s",i,tval(t->tparam)); break;
			case HENUM:{ EX("T%d.tenum = &tenum%d",i,i); int g=enum_global(t); if(g>=0) EX("tenum%d.global_value = (void**)&g%d",i,g); EX("hl_init_enum(&T%d,ctx)",i);} break;
			case HVIRTUAL: EX("T%d.virt = &virt%d",i,i); EX("hl_init_virtual(&T%d,ctx)",i); break;
			case HFUN: case HMETHOD: EX("T%d.fun = &tfun%d",i,i); break;
			default: break;
			}
		}
		tabs--; LN("}"); nb++;
	}
	LN("void hl_init_types( hl_module_context *ctx ) {"); tabs++;
	for(int k=0;k<nb;k++) EX("hl_init_types_%d(ctx)",k);
	tabs--; LN("}");
}

static void emit_globals(void){
	LN("// globals");
	for(int i=0;i<code->nglobals;i++) EX("%s g%d = 0",ctype(code->globals[i]),i);
	// strings emitted as UTF-16 byte arrays (long or non-ASCII)
	for(int i=0;i<code->nstrings;i++){ if(!usrt_able(i)){ const uchar*u=hl_get_ustring(code,i); int n=0; while(u[n])n++;
		wt(); fprintf(o,"vbyte hlstr$%d[] = {",i); for(int k=0;k<=n;k++){ unsigned cu=u[k]; fprintf(o,"%s%d,%d",k?",":"",cu&0xff,(cu>>8)&0xff);} fprintf(o,"};\n"); } }
	char *isconst=calloc(code->nglobals,1);
	for(int k=0;k<code->nconstants;k++){ hl_constant*c=&code->constants[k]; hl_type*t=code->globals[c->global]; isconst[c->global]=1;
		hl_type_obj*ob=t->obj; wt(); fprintf(o,"static struct _O%d const_g%d = {%s",tidx(t),c->global,tval(t));
		for(int j=0;j<c->nfields&&j<ob->nfields;j++){ hl_type*ft=ob->fields[j].t; int vi=c->fields[j];
			if(ft->kind==HI32) fprintf(o,",%d",code->ints[vi]);
			else if(ft->kind==HBYTES) fprintf(o,",(vbyte*)%s",sstr(vi));
			else fprintf(o,",0");
		}
		fprintf(o,"};\n");
	}
	int B=1500,nb=0;
	for(int start=0;start<code->nconstants;start+=B){ int end=start+B; if(end>code->nconstants)end=code->nconstants;
		LN("static void hl_roots_c%d(void) {",nb); tabs++;
		for(int k=start;k<end;k++){ hl_constant*c=&code->constants[k]; EX("g%d = &const_g%d",c->global,c->global); }
		tabs--; LN("}"); nb++; }
	int ncb=nb, ngb=0;
	for(int start=0;start<code->nglobals;start+=B){ int end=start+B; if(end>code->nglobals)end=code->nglobals;
		LN("static void hl_roots_g%d(void) {",ngb); tabs++;
		for(int i=start;i<end;i++){ if(is_ptr(code->globals[i]) && !isconst[i]) EX("hl_add_root((void**)&g%d)",i); }
		tabs--; LN("}"); ngb++; }
	LN("void hl_init_roots() {"); tabs++;
	for(int k=0;k<ncb;k++) EX("hl_roots_c%d()",k);
	for(int k=0;k<ngb;k++) EX("hl_roots_g%d()",k);
	tabs--; LN("}");
}

static void emit_func_protos(void){
	LN("// function prototypes");
	for(int i=0;i<code->nfunctions;i++){ hl_function*f=&code->functions[i]; EX("%s f%d(%s)",ctype(f->type->fun->ret),f->findex,args_repr(f->type)); }
}

/* ---------- shared header (dc.h) ---------- */
static void emit_header(void){
	LN("#ifndef DC_H");
	LN("#define DC_H");
	LN("#include <hlc.h>");
	emit_types();
	emit_natives();
	LN("// extern type values");
	for(int i=0;i<code->ntypes;i++) LN("extern hl_type T%d;",i);
	LN("// extern globals");
	for(int i=0;i<code->nglobals;i++) LN("extern %s g%d;",ctype(code->globals[i]),i);
	emit_func_protos();
	LN("extern void *hl_functions_ptrs[];");
	LN("extern hl_type *hl_functions_types[];");
	LN("void hl_init_types( hl_module_context *ctx );");
	LN("void hl_init_hashes(void);");
	LN("void hl_init_roots(void);");
	LN("void hl_entry_point(void);");
	LN("#endif");
}

/* ---------- native stubs ----------
   'std' come from libhl; 'ui' from the official ui_stub.c; 'sdl' from sdl.c+gl.c;
   'fmt' from fmt.c; core 'openal' from openal.c (-lopenal -> Web Audio, EFX excepted);
   ssl_dgst_make from ssl_impl.c. Everything else = logging no-op. steam_init is
   faked to return true so the Steam gate doesn't quit the game (DRM stub). */
static void emit_native_stubs(void){
	LN("// native stubs (non-std/ui/sdl): log-once + return 0");
	char **seen=calloc(code->nnatives,sizeof(char*)); int ns=0;
	for(int i=0;i<code->nnatives;i++){ const char*lib=code->natives[i].lib; const char*l=(lib[0]=='?')?lib+1:lib;
		if(!strcmp(l,"std")||!strcmp(l,"ui")||!strcmp(l,"sdl")||!strcmp(l,"fmt")) continue;
		const char*cn=native_cname(i); hl_type*t=code->natives[i].t; hl_type_fun*fu=t->fun;
		if(!strcmp(cn,"ssl_dgst_make")) continue; /* provided by ssl_impl.c (real SHA256) */
		/* openal: the core AL/ALC natives come from openal.c (linked with -lopenal ->
		   Emscripten Web Audio). Emscripten has no EFX, so keep only the effect/filter/
		   auxiliary-effect-slot natives as log-once no-op stubs. */
		if(!strcmp(l,"openal") && !strstr(cn,"effect") && !strstr(cn,"filter") && !strstr(cn,"auxiliary")) continue;
		int dup=0; for(int j=0;j<ns;j++) if(!strcmp(seen[j],cn)){dup=1;break;} if(dup) continue; seen[ns++]=strdup(cn);
		char ps[BS]; int p=0; ps[0]=0;
		if(fu->nargs==0) snprintf(ps,BS,"void"); else for(int k=0;k<fu->nargs;k++){ char*c=ctype(fu->args[k]); p+=snprintf(ps+p,BS-p,"%s%s p%d",k?",":"",c,k); }
		/* steam_init + the stats/callback pump come from runtime/steam_shim.c: it fires
		   the async events the title-screen modal waits on (otherwise it hangs, dimming
		   the screen and capturing input). Other steam_* stay log-once no-op stubs. */
		if(!strcmp(cn,"steam_init")||!strcmp(cn,"steam_run_callbacks")||!strcmp(cn,"steam_request_stats")||!strcmp(cn,"steam_request_global_stats")) continue;
		if(fu->ret->kind==HVOID) LN("%s %s(%s) { static int _o=0; if(!_o){_o=1;fprintf(stderr,\"[native] %s\\n\");fflush(stderr);} }",ctype(fu->ret),cn,ps,cn);
		else LN("%s %s(%s) { static int _o=0; if(!_o){_o=1;fprintf(stderr,\"[native] %s\\n\");fflush(stderr);} return (%s)0; }",ctype(fu->ret),cn,ps,cn,ctype(fu->ret));
	}
}

#include "bc2c_refl.h"
#include "bc2c_gen.h"

static void emit_func_tables(void){
	LN("// function tables");
	wt(); fprintf(o,"void *hl_functions_ptrs[] = {");
	for(int fx=0;fx<nfindex;fx++){ const char*nm=fname[fx]?fname[fx]:"NULL"; if(!strcmp(nm,"hl_tls_get_w")) nm="hl_tls_get"; fprintf(o,"%s%s",fx?",":"",nm); }
	fprintf(o,"};\n");
	wt(); fprintf(o,"hl_type *hl_functions_types[] = {");
	for(int fx=0;fx<nfindex;fx++){ if(ftype[fx]) fprintf(o,"%s&T%d",fx?",":"",tidx(ftype[fx])); else fprintf(o,"%sNULL",fx?",":""); }
	fprintf(o,"};\n");
}
static void emit_init_hashes(void){
	int B=2000,nb=0;
	for(int start=0;start<hordn;start+=B){ int end=start+B; if(end>hordn)end=hordn;
		LN("static void hl_hashes_%d(void) {",nb); tabs++;
		for(int i=start;i<end;i++) EX("hl_hash((vbyte*)%s)",ustr(horder[i]));
		tabs--; LN("}"); nb++; }
	LN("void hl_init_hashes() {"); tabs++;
	for(int k=0;k<nb;k++) EX("hl_hashes_%d()",k);
	tabs--; LN("}");
}
static void emit_entry(void){
	LN("void hl_entry_point() {"); tabs++;
	EX("hl_module_context ctx"); EX("hl_alloc_init(&ctx.alloc)");
	EX("ctx.functions_ptrs = hl_functions_ptrs"); EX("ctx.functions_types = hl_functions_types");
	EX("hl_init_types(&ctx)"); EX("hl_init_hashes()"); EX("hl_init_roots()");
	EX("fprintf(stderr,\"[hep] entrypoint\\n\"); fflush(stderr)");
	if(code->entrypoint>=0) EX("f%d()",code->entrypoint);
	EX("fprintf(stderr,\"[hep] entrypoint returned\\n\"); fflush(stderr)");
	tabs--; LN("}");
	LN("#include <hlc_main.c>");
}
