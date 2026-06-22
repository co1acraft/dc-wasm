// bc2c_refl.h : reflection (hlc_static_call / hlc_get_wrapper / wrap_*)
// These are required by hlc_main.c (registered into hl_setup).

typedef struct { int nargs; int ret; int args[16]; } sigt;
static sigt *sigs=NULL; static int nsigs=0, sigcap=0;
static int kid(hl_type_kind k){ switch(k){case HVOID:return 0;case HBOOL:case HUI8:case HUI16:case HI32:return 1;case HF32:return 2;case HF64:return 3;case HI64:return 4;default:return 5;} }
static const char* idctype(int id){ switch(id){case 0:return"void";case 1:return"int";case 2:return"float";case 3:return"double";case 4:return"int64";default:return"vdynamic*";} }
static const char* idvfield(int id){ switch(id){case 1:return"i";case 2:return"f";case 3:return"d";case 4:return"i64";default:return"ptr";} }
static const char* idwrapstr(int id){ switch(id){case 0:return"v";case 1:return"i";case 2:return"f";case 3:return"d";case 4:return"i64";default:return"p";} }
static int idisptr(int id){ return id==5; }

static void add_sig(hl_type*ft){ if(ft->kind!=HFUN&&ft->kind!=HMETHOD) return; hl_type_fun*fu=ft->fun; if(fu->nargs>9) return;
	sigt s; s.nargs=fu->nargs; s.ret=kid(fu->ret->kind); for(int k=0;k<fu->nargs;k++) s.args[k]=kid(fu->args[k]->kind);
	for(int i=0;i<nsigs;i++){ if(sigs[i].nargs==s.nargs&&sigs[i].ret==s.ret){ int same=1; for(int k=0;k<s.nargs;k++) if(sigs[i].args[k]!=s.args[k]){same=0;break;} if(same) return; } }
	if(nsigs==sigcap){ sigcap=sigcap?sigcap*2:128; sigs=realloc(sigs,sizeof(sigt)*sigcap);} sigs[nsigs++]=s;
}
static int sig_chk(sigt*s){ int chk=s->ret; for(int k=0;k<s->nargs;k++) chk|= s->args[k] << ((k+1)*3); return chk; }
static void wrap_name(sigt*s,char*b){ int p=0; for(int k=0;k<s->nargs;k++) p+=snprintf(b+p,64-p,"%s",idwrapstr(s->args[k])); snprintf(b+p,64-p,"_%s",idwrapstr(s->ret)); }

static void emit_reflection(void){
	LN("// reflection");
	for(int fx=0;fx<nfindex;fx++) if(ftype[fx]) add_sig(ftype[fx]);
	for(int i=0;i<code->nfunctions;i++){ hl_function*f=&code->functions[i];
		for(int j=0;j<f->nops;j++){ hl_opcode*op=&f->ops[j];
			if(op->op==OSafeCast||op->op==ODynGet){ hl_type*dt=f->regs[op->p1]; if(dt->kind==HFUN||dt->kind==HMETHOD) add_sig(dt); } } }
	int maxn=0; for(int i=0;i<nsigs;i++) if(sigs[i].nargs>maxn) maxn=sigs[i].nargs;
	wt(); fprintf(o,"static int TKIND[] = {"); for(int k=0;k<24;k++) fprintf(o,"%s%d",k?",":"",kid((hl_type_kind)k)); fprintf(o,"};\n");

	LN("void *hlc_static_call( void *fun, hl_type *t, void **args, vdynamic *out ) {"); tabs++;
	EX("int chk = TKIND[t->fun->ret->kind]");
	LN("switch( t->fun->nargs ) {");
	for(int n=0;n<=maxn;n++){ int any=0; for(int i=0;i<nsigs;i++) if(sigs[i].nargs==n){any=1;break;} if(!any) continue;
		LN("case %d:",n); tabs++;
		for(int i=0;i<n;i++) EX("chk |= TKIND[t->fun->args[%d]->kind] << %d",i,(i+1)*3);
		LN("switch( chk ) {");
		for(int s=0;s<nsigs;s++){ if(sigs[s].nargs!=n) continue; sigt*sg=&sigs[s]; int chk=sig_chk(sg);
			LN("case %d:",chk); tabs++;
			char call[BS]; int cp=snprintf(call,BS,"((%s (*)(",idctype(sg->ret));
			if(n==0) cp+=snprintf(call+cp,BS-cp,"void"); else for(int k=0;k<n;k++) cp+=snprintf(call+cp,BS-cp,"%s%s",k?",":"",idctype(sg->args[k]));
			cp+=snprintf(call+cp,BS-cp,"))fun)(");
			for(int k=0;k<n;k++){ if(idisptr(sg->args[k])) cp+=snprintf(call+cp,BS-cp,"%s(%s)args[%d]",k?",":"",idctype(sg->args[k]),k);
				else cp+=snprintf(call+cp,BS-cp,"%s*(%s*)args[%d]",k?",":"",idctype(sg->args[k]),k); }
			cp+=snprintf(call+cp,BS-cp,")");
			if(idisptr(sg->ret)) EX("return %s",call);
			else if(sg->ret==0){ EX("%s",call); EX("return NULL"); }
			else { EX("out->v.%s = %s",idvfield(sg->ret),call); EX("return &out->v.%s",idvfield(sg->ret)); }
			tabs--;
		}
		LN("}"); EX("break"); tabs--;
	}
	LN("}");
	EX("hl_fatal(\"Unsupported dynamic call\")"); EX("return NULL");
	tabs--; LN("}");

	for(int s=0;s<nsigs;s++){ sigt*sg=&sigs[s]; char nm[64]; wrap_name(sg,nm);
		wt(); fprintf(o,"static %s wrap_%s(void *value",idctype(sg->ret),nm);
		for(int k=0;k<sg->nargs;k++) fprintf(o,",%s p%d",idctype(sg->args[k]),k);
		fprintf(o,") {\n"); tabs++;
		if(sg->nargs){ wt(); fprintf(o,"void *args[] = {"); for(int k=0;k<sg->nargs;k++) fprintf(o,"%s%s p%d",k?",":"", idisptr(sg->args[k])?"":"&", k); fprintf(o,"};\n"); }
		const char*va=sg->nargs?"args":"NULL";
		if(sg->ret==0) EX("hl_wrapper_call(value,%s,NULL)",va);
		else if(idisptr(sg->ret)) EX("return hl_wrapper_call(value,%s,NULL)",va);
		else { EX("vdynamic ret"); EX("hl_wrapper_call(value,%s,&ret)",va); EX("return ret.v.%s",idvfield(sg->ret)); }
		tabs--; LN("}");
	}
	LN("void *hlc_get_wrapper( hl_type *t ) {"); tabs++;
	EX("int chk = TKIND[t->fun->ret->kind]");
	LN("switch( t->fun->nargs ) {");
	for(int n=0;n<=maxn;n++){ int any=0; for(int i=0;i<nsigs;i++) if(sigs[i].nargs==n){any=1;break;} if(!any) continue;
		LN("case %d:",n); tabs++;
		for(int i=0;i<n;i++) EX("chk |= TKIND[t->fun->args[%d]->kind] << %d",i,(i+1)*3);
		LN("switch( chk ) {");
		for(int s=0;s<nsigs;s++){ if(sigs[s].nargs!=n) continue; char nm[64]; wrap_name(&sigs[s],nm); EX("case %d: return wrap_%s",sig_chk(&sigs[s]),nm); }
		LN("}"); EX("break"); tabs--;
	}
	LN("}");
	EX("return NULL");
	tabs--; LN("}");
}
