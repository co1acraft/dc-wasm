// bc2c_gen.h : generate_function (HL opcode -> C), ported from hl2c.ml

static hl_function *CF; static int CFI; static int *label_at; static int g_clid; static int g_trapd;

static hl_type* rtype(int i){ return CF->regs[i]; }
static char* rcast(int v, hl_type*t){ char*b=gb(); if(tsame(rtype(v),t)) snprintf(b,BS,"r%d",v); else snprintf(b,BS,"((%s)r%d)",ctype(t),v); return b; }
static char* rassign(int r, hl_type*t){ char*b=gb();
	if(t->kind==HVOID){ b[0]=0; return b; }
	if(tsame(t,rtype(r))) snprintf(b,BS,"r%d = ",r);
	else if(rtype(r)->kind==HPACKED) snprintf(b,BS,"r%d = (%s)&",r,ctype(rtype(r)));
	else snprintf(b,BS,"r%d = (%s)",r,ctype(rtype(r)));
	return b; }
static char* lbl(int t){ char*b=gb(); snprintf(b,BS,"L%d_%d",CFI,t); return b; }
static const char* scomp(int op){ switch(op){case 0:return"==";case 1:return"!=";case 2:return"<";case 3:return">";case 4:return"<=";case 5:return">=";} return"=="; }
enum { C_EQ,C_NEQ,C_LT,C_GT,C_LTE,C_GTE };

static char* castfun(const char*s, hl_type**at, int n, hl_type*ret){
	char tmp[BS]; int p=snprintf(tmp,BS,"((%s (*)(",ctype(ret));
	if(n==0) p+=snprintf(tmp+p,BS-p,"void"); else for(int k=0;k<n;k++){ char*c=ctype(at[k]); p+=snprintf(tmp+p,BS-p,"%s%s",k?",":"",c); }
	snprintf(tmp+p,BS-p,"))%s)",s); char*b=gb(); snprintf(b,BS,"%s",tmp); return b;
}
static char* dynt(void){ return DYNT; }

static void ocall(int r,int fid,int*args,int n){
	hl_type_fun*cf=ftype[fid]->fun; char as[BS]; int p=0; as[0]=0;
	for(int k=0;k<n;k++){ char*c=rcast(args[k], cf->args[k]); p+=snprintf(as+p,BS-p,"%s%s",k?",":"",c); }
	EX("%s%s(%s)", rassign(r, cf->ret), fname[fid], as);
}
static void mcall(int r,int fid,int*args,int n){
	hl_type*ot=rtype(args[0]);
	if(ot->kind==HOBJ||ot->kind==HSTRUCT){
		hl_type*at[260]; for(int k=0;k<n;k++) at[k]=rtype(args[k]);
		char s[128]; snprintf(s,128,"r%d->$type->vobj_proto[%d]",args[0],fid);
		char*vf=castfun(s,at,n,rtype(r));
		char as[BS]; int p=0; as[0]=0; for(int k=0;k<n;k++) p+=snprintf(as+p,BS-p,"%sr%d",k?",":"",args[k]);
		EX("%s%s(%s)", rassign(r,rtype(r)), vf, as);
	} else if(ot->kind==HVIRTUAL){
		hl_type*rt=rtype(r); hl_obj_field*vf=&ot->virt->fields[fid];
		char meth[160]; snprintf(meth,160,"hl_vfields(r%d)[%d]",args[0],fid);
		char cf[BS]; int cp=snprintf(cf,BS,"((%s (*)(vdynamic*",ctype(rt));
		for(int k=1;k<n;k++){ char*c=ctype(rtype(args[k])); cp+=snprintf(cf+cp,BS-cp,",%s",c); }
		cp+=snprintf(cf+cp,BS-cp,"))%s)",meth);
		char callargs[BS]; int ap=snprintf(callargs,BS,"r%d->value",args[0]); for(int k=1;k<n;k++) ap+=snprintf(callargs+ap,BS-ap,",r%d",args[k]);
		LN("if( hl_vfields(r%d)[%d] ) %s%s(%s); else {",args[0],fid,rassign(r,rt),cf,callargs); tabs++;
		if(n>1){ char av[BS]; int p=0; for(int k=1;k<n;k++){ hl_type*pt=rtype(args[k]); p+=snprintf(av+p,BS-p,"%s%sr%d",(k>1)?",":"", is_ptr(pt)?"":"&", args[k]); } EX("void *args[] = {%s}",av); }
		const char*ret; char retbuf[64];
		if(rt->kind==HVOID) ret="";
		else if(is_ptr(rt)){ snprintf(retbuf,64,"r%d = (%s)",r,ctype(rt)); ret=retbuf; }
		else { EX("vdynamic ret"); ret=""; }
		EX("%shl_dyn_call_obj(r%d->value,%s,%d/*%s*/,%s,%s)", ret, args[0], tval(vf->t), myhash(vf->name?vf->name:(const uchar*)""), "", (n>1)?"args":"NULL", (is_ptr(rt)||rt->kind==HVOID)?"NULL":"&ret");
		if(rt->kind!=HVOID && !is_ptr(rt)) EX("r%d = (%s)ret.v.%s",r,ctype(rt),dyn_prefix(rt));
		tabs--; LN("}");
	} else EX("hl_fatal(\"mcall on non-obj\")");
}
static void dyn_call(int r,int clreg,int*args,int n){
	LN("{"); tabs++;
	if(n>0){ char av[BS]; int p=0; for(int k=0;k<n;k++){ hl_type*pt=rtype(args[k]);
		if(pt->kind==HDYN) p+=snprintf(av+p,BS-p,"%sr%d",k?",":"",args[k]);
		else if(is_dyn(pt)) p+=snprintf(av+p,BS-p,"%s(vdynamic*)r%d",k?",":"",args[k]);
		else p+=snprintf(av+p,BS-p,"%shl_make_dyn(&r%d,%s)",k?",":"",args[k],tval(pt)); }
		EX("vdynamic *args[] = {%s}",av); }
	hl_type*rt=rtype(r); const char*ret; char rb[64];
	if(rt->kind==HVOID) ret="";
	else if(is_dyn(rt)){ snprintf(rb,64,"r%d = (%s)",r,ctype(rt)); ret=rb; }
	else ret="vdynamic *ret = ";
	EX("%shl_dyn_call((vclosure*)r%d,%s,%d)",ret,clreg,n?"args":"NULL",n);
	if(rt->kind!=HVOID && !is_dyn(rt)) EX("r%d = (%s)hl_dyn_cast%s(&ret,%s%s)",r,ctype(rt),dyn_prefix(rt),dynt(),tvopt(rt));
	tabs--; LN("}");
}
static void get_field(int r,int obj,int fid){
	hl_type*ot=rtype(obj);
	if(ot->kind==HOBJ||ot->kind==HSTRUCT){ const uchar*nm; hl_type*t; resolve_field(ot,fid,&nm,&t);
		EX("%sr%d->%s",rassign(r,t),obj,objfield_name(nm,fid)); }
	else if(ot->kind==HVIRTUAL){ hl_obj_field*vf=&ot->virt->fields[fid]; hl_type*t=vf->t;
		char dg[BS]; snprintf(dg,BS,"(%s)hl_dyn_get%s(r%d->value,%d/*x*/%s)",ctype(t),dyn_prefix(t),obj,myhash(vf->name?vf->name:(const uchar*)""),tvopt(t));
		EX("%shl_vfields(r%d)[%d] ? (*(%s*)(hl_vfields(r%d)[%d])) : %s",rassign(r,t),obj,fid,ctype(t),obj,fid,dg); }
	else EX("hl_fatal(\"getfield\")");
}
static void set_field(int obj,int fid,int v){
	hl_type*ot=rtype(obj);
	if(ot->kind==HOBJ||ot->kind==HSTRUCT){ const uchar*nm; hl_type*t; resolve_field(ot,fid,&nm,&t);
		EX("r%d->%s = %s",obj,objfield_name(nm,fid),rcast(v,t)); }
	else if(ot->kind==HVIRTUAL){ hl_obj_field*vf=&ot->virt->fields[fid]; hl_type*t=vf->t;
		EX("if( hl_vfields(r%d)[%d] ) *(%s*)(hl_vfields(r%d)[%d]) = (%s)r%d; else hl_dyn_set%s(r%d->value,%d/*x*/%s,r%d)",
			obj,fid,ctype(t),obj,fid,ctype(t),v,dyn_prefix(rtype(v)),obj,myhash(vf->name?vf->name:(const uchar*)""),tvopt(rtype(v)),v); }
	else EX("hl_fatal(\"setfield\")");
}
// find a proto method by name in the obj's super chain (most-derived first)
static int find_proto(hl_type*ot,const char*mname){
	for(hl_type*c=ot;c;c=c->obj->super){ hl_type_obj*ob=c->obj;
		for(int k=0;k<ob->nproto;k++){ char nm[256]; us2a(ob->proto[k].name,nm); if(!strcmp(nm,mname)) return ob->proto[k].findex; } }
	return -1;
}
static void compare_op(int cop,int a,int b,int d){
	hl_type*ta=rtype(a),*tb=rtype(b);
	#define PHYS() EX("if( r%d %s %s ) goto %s",a,scomp(cop),rcast(b,ta),lbl(d))
	int na = (ta->kind>=HUI8&&ta->kind<=HBOOL)||ta->kind==HI64;
	int nb = (tb->kind>=HUI8&&tb->kind<=HBOOL)||tb->kind==HI64;
	if(na&&nb){ PHYS(); return; }
	if(ta->kind==HTYPE&&tb->kind==HTYPE){ EX("if( hl_same_type(r%d,r%d) %s 0 ) {} else goto %s",a,b,scomp(cop),lbl(d)); return; }
	if(ta->kind==HNULL&&tb->kind==HNULL){ const char*fld=dynfield(ta->tparam);
		if(cop==C_EQ) EX("if( r%d == r%d || (r%d && r%d && r%d%s %s r%d%s) ) goto %s",a,b,a,b,a,fld,scomp(cop),b,fld,lbl(d));
		else if(cop==C_NEQ) EX("if( r%d != r%d && (!r%d || !r%d || r%d%s %s r%d%s) ) goto %s",a,b,a,b,a,fld,scomp(cop),b,fld,lbl(d));
		else EX("if( r%d && r%d && r%d%s %s r%d%s ) goto %s",a,b,a,fld,scomp(cop),b,fld,lbl(d));
		return; }
	if(ta->kind==HDYN||ta->kind==HFUN||tb->kind==HDYN||tb->kind==HFUN){
		const char*inv=(cop==C_GT||cop==C_GTE)?"&& i != hl_invalid_comparison ":"";
		EX("{ int i = hl_dyn_compare((vdynamic*)r%d,(vdynamic*)r%d); if( i %s 0 %s) goto %s; }",a,b,scomp(cop),inv,lbl(d)); return; }
	if(ta->kind==HOBJ&&tb->kind==HOBJ){ int cf=find_proto(ta,"__compare");
		if(cf>=0){
			if(cop==C_EQ) EX("if( r%d == r%d || (r%d && r%d && %s(r%d,(vdynamic*)r%d) == 0) ) goto %s",a,b,a,b,fname[cf],a,b,lbl(d));
			else if(cop==C_NEQ) EX("if( r%d != r%d && (!r%d || !r%d || %s(r%d,(vdynamic*)r%d) != 0) ) goto %s",a,b,a,b,fname[cf],a,b,lbl(d));
			else EX("if( r%d && r%d && %s(r%d,(vdynamic*)r%d) %s 0 ) goto %s",a,b,fname[cf],a,b,scomp(cop),lbl(d));
			return; }
	}
	PHYS();
	#undef PHYS
}

static void generate_function(hl_function*f){
	CF=f; static int fidx_counter=0; CFI=fidx_counter++;
	hl_type_fun*fu=f->type->fun; hl_type*fret=fu->ret;
	int nops=f->nops;
	label_at=calloc(nops+1,sizeof(int));
	for(int i=0;i<nops;i++){ hl_opcode*op=&f->ops[i]; int t;
		switch(op->op){
		case OJTrue: case OJFalse: case OJNull: case OJNotNull: case OTrap: t=i+1+op->p2; label_at[t]=1; break;
		case OJSLt: case OJSGte: case OJSGt: case OJSLte: case OJULt: case OJUGte: case OJNotLt: case OJNotGte: case OJEq: case OJNotEq: t=i+1+op->p3; label_at[t]=1; break;
		case OJAlways: t=i+1+op->p1; label_at[t]=1; break;
		case OLabel: label_at[i]=1; break;
		case OSwitch: for(int k=0;k<op->p2;k++){ t=i+1+op->extra[k]; label_at[t]=1; } break;
		default: break;
		}
	}
	{ char as[BS]; int p=0; as[0]=0; for(int k=0;k<fu->nargs;k++){ char*c=ctype(fu->args[k]); p+=snprintf(as+p,BS-p,"%s%s r%d",k?",":"",c,k); } if(fu->nargs==0) snprintf(as,BS,"void");
	  LN("%s f%d(%s) {",ctype(fret),f->findex,as); }
	tabs++;
	for(int i=fu->nargs;i<f->nregs;i++){ if(f->regs[i]->kind==HVOID) continue; EX("%s r%d",ctype(f->regs[i]),i); }
#ifdef DC_GC_ROOTS
	/* Force every GC-managed pointer register (args + locals) onto the linear-memory
	   shadow stack by taking its address, so HashLink's conservative GC scan can see
	   it. Cheaper than Binaryen --spill-pointers (no per-call save/restore explosion). */
	{ int __np=0; for(int i=0;i<f->nregs;i++) if(f->regs[i]->kind!=HVOID && is_ptr(f->regs[i])) __np++;
	  if(__np){ EX("void * volatile __gcs = 0; (void)__gcs");
	    for(int i=0;i<f->nregs;i++) if(f->regs[i]->kind!=HVOID && is_ptr(f->regs[i])) EX("__gcs = (void*)&r%d",i); } }
#endif
	int maxtrap=0,cur=0; for(int i=0;i<nops;i++){ if(f->ops[i].op==OTrap){cur++; if(cur>maxtrap)maxtrap=cur;} else if(f->ops[i].op==OEndTrap && f->ops[i].p1) cur--; }
	for(int i=0;i<maxtrap;i++) EX("hl_trap_ctx trap$%d",i);
	g_clid=0; for(int i=0;i<nops;i++){ if(f->ops[i].op==OStaticClosure){ int fid=f->ops[i].p2; EX("static vclosure cl$%d = { %s, (void*)%s, 0 }",g_clid,tval(ftype[fid]),fname[fid]); g_clid++; } }
	g_clid=0; g_trapd=0;

	for(int i=0;i<nops;i++){
		if(label_at[i]) LN("L%d_%d:",CFI,i);
		hl_opcode*op=&f->ops[i];
		int r=op->p1;
		switch(op->op){
		case OMov: if(rtype(op->p1)->kind!=HVOID) EX("r%d = %s",op->p1,rcast(op->p2,rtype(op->p1))); break;
		case OInt: { int v=code->ints[op->p2]; if((unsigned)v==0x80000000u) EX("r%d = 0x80000000",r); else EX("r%d = %d",r,v); } break;
		case OFloat: { char fs[64]; snprintf(fs,64,"%.19g",code->floats[op->p2]); if(!strchr(fs,'.')&&!strchr(fs,'e')&&!strchr(fs,'n')&&!strchr(fs,'i')) strcat(fs,"."); EX("r%d = %s",r,fs); } break;
		case OBool: EX("r%d = %s",r,op->p2?"true":"false"); break;
		case OString: EX("r%d = (vbyte*)%s",r,sstr(op->p2)); break;
		case OBytes: EX("r%d = bytes$%d",r,op->p2); break;
		case ONull: EX("r%d = NULL",r); break;
		case OAdd: EX("r%d = r%d + r%d",r,op->p2,op->p3); break;
		case OSub: EX("r%d = r%d - r%d",r,op->p2,op->p3); break;
		case OMul: EX("r%d = r%d * r%d",r,op->p2,op->p3); break;
		case OSDiv: { hl_type_kind k=rtype(r)->kind; if(k==HUI8||k==HUI16||k==HI32) EX("r%d = r%d == 0 ? 0 : r%d / r%d",r,op->p3,op->p2,op->p3); else EX("r%d = r%d / r%d",r,op->p2,op->p3);} break;
		case OUDiv: EX("r%d = r%d == 0 ? 0 : ((unsigned)r%d) / ((unsigned)r%d)",r,op->p3,op->p2,op->p3); break;
		case OSMod: { hl_type_kind k=rtype(r)->kind; if(k==HUI8||k==HUI16||k==HI32||k==HI64) EX("r%d = r%d == 0 ? 0 : r%d %% r%d",r,op->p3,op->p2,op->p3); else if(k==HF32) EX("r%d = fmodf(r%d,r%d)",r,op->p2,op->p3); else EX("r%d = fmod(r%d,r%d)",r,op->p2,op->p3);} break;
		case OUMod: EX("r%d = r%d == 0 ? 0 : ((unsigned)r%d) %% ((unsigned)r%d)",r,op->p3,op->p2,op->p3); break;
		case OShl: EX("r%d = r%d << r%d",r,op->p2,op->p3); break;
		case OSShr: EX("r%d = r%d >> r%d",r,op->p2,op->p3); break;
		case OUShr: { if(rtype(r)->kind==HI64) EX("r%d = ((uint64)r%d) >> r%d",r,op->p2,op->p3); else EX("r%d = ((unsigned)r%d) >> r%d",r,op->p2,op->p3);} break;
		case OAnd: EX("r%d = r%d & r%d",r,op->p2,op->p3); break;
		case OOr: EX("r%d = r%d | r%d",r,op->p2,op->p3); break;
		case OXor: EX("r%d = r%d ^ r%d",r,op->p2,op->p3); break;
		case ONeg: EX("r%d = -r%d",r,op->p2); break;
		case ONot: EX("r%d = !r%d",r,op->p2); break;
		case OIncr: EX("++r%d",r); break;
		case ODecr: EX("--r%d",r); break;
		case OCall0: ocall(r,op->p2,NULL,0); break;
		case OCall1: { int a[1]={op->p3}; ocall(r,op->p2,a,1);} break;
		case OCall2: { int a[2]={op->p3,(int)(int_val)op->extra}; ocall(r,op->p2,a,2);} break;
		case OCall3: { int a[3]={op->p3,op->extra[0],op->extra[1]}; ocall(r,op->p2,a,3);} break;
		case OCall4: { int a[4]={op->p3,op->extra[0],op->extra[1],op->extra[2]}; ocall(r,op->p2,a,4);} break;
		case OCallN: ocall(r,op->p2,op->extra,op->p3); break;
		case OCallMethod: mcall(r,op->p2,op->extra,op->p3); break;
		case OCallThis: { int a[260]; a[0]=0; for(int k=0;k<op->p3;k++) a[k+1]=op->extra[k]; mcall(r,op->p2,a,op->p3+1);} break;
		case OCallClosure: { hl_type*ct=rtype(op->p2);
			if(ct->kind==HDYN){ dyn_call(r,op->p2,op->extra,op->p3); }
			else { hl_type_fun*cf=ct->fun; char sa[BS]; int p=0; sa[0]=0; for(int k=0;k<op->p3;k++){ char*c=rcast(op->extra[k],cf->args[k]); p+=snprintf(sa+p,BS-p,"%s%s",k?",":"",c);}
				char cfv[BS]; int q=snprintf(cfv,BS,"((%s (*)(vdynamic*",ctype(cf->ret)); for(int k=0;k<cf->nargs;k++){char*c=ctype(cf->args[k]); q+=snprintf(cfv+q,BS-q,",%s",c);} snprintf(cfv+q,BS-q,"))r%d->fun)",op->p2);
				char cfn[BS]; q=snprintf(cfn,BS,"((%s (*)(",ctype(cf->ret)); if(cf->nargs==0) q+=snprintf(cfn+q,BS-q,"void"); else for(int k=0;k<cf->nargs;k++){char*c=ctype(cf->args[k]); q+=snprintf(cfn+q,BS-q,"%s%s",k?",":"",c);} snprintf(cfn+q,BS-q,"))r%d->fun)",op->p2);
				char vca[BS]; if(sa[0]) snprintf(vca,BS,",%s",sa); else vca[0]=0;
				EX("%sr%d->hasValue ? %s((vdynamic*)r%d->value%s) : %s(%s)",rassign(r,cf->ret),op->p2,cfv,op->p2,vca,cfn,sa);
			} } break;
		case OStaticClosure: EX("r%d = &cl$%d",r,g_clid++); break;
		case OVirtualClosure: { hl_type*ot=rtype(op->p2); int m=op->p3; int fid=-1;
			for(hl_type*c=ot;c&&fid<0;c=c->obj->super){ for(int k=0;k<c->obj->nproto;k++) if(c->obj->proto[k].pindex==m){ fid=c->obj->proto[k].findex; break; } }
			if(fid>=0) EX("r%d = hl_alloc_closure_ptr(%s,r%d->$type->vobj_proto[%d],r%d)",r,tval(ftype[fid]),op->p2,m,op->p2);
			else EX("hl_fatal(\"vclosure\")"); } break;
		case OInstanceClosure: EX("r%d = hl_alloc_closure_ptr(%s,(void*)%s,r%d)",r,tval(ftype[op->p2]),fname[op->p2],op->p3); break;
		case OGetGlobal: EX("r%d = (%s)g%d",r,ctype(rtype(r)),op->p2); break;
		case OSetGlobal: EX("g%d = (%s)r%d",op->p1,ctype(code->globals[op->p1]),op->p2); break;
		case OField: get_field(r,op->p2,op->p3); break;
		case OSetField: set_field(op->p1,op->p2,op->p3); break;
		case OGetThis: get_field(r,0,op->p2); break;
		case OSetThis: set_field(0,op->p1,op->p2); break;
		case ODynGet: { hl_type*t=rtype(r); const uchar*nm=hl_get_ustring(code,op->p3); EX("r%d = (%s)hl_dyn_get%s((vdynamic*)r%d,%d/*x*/%s)",r,ctype(t),dyn_prefix(t),op->p2,myhash(nm),tvopt(t)); } break;
		case ODynSet: { const uchar*nm=hl_get_ustring(code,op->p2); EX("hl_dyn_set%s((vdynamic*)r%d,%d/*x*/%s,r%d)",dyn_prefix(rtype(op->p3)),op->p1,myhash(nm),tvopt(rtype(op->p3)),op->p3); } break;
		case OJTrue: EX("if( r%d ) goto %s",op->p1,lbl(i+1+op->p2)); break;
		case OJNotNull: EX("if( r%d ) goto %s",op->p1,lbl(i+1+op->p2)); break;
		case OJFalse: EX("if( !r%d ) goto %s",op->p1,lbl(i+1+op->p2)); break;
		case OJNull: EX("if( !r%d ) goto %s",op->p1,lbl(i+1+op->p2)); break;
		case OJSLt: compare_op(C_LT,op->p1,op->p2,i+1+op->p3); break;
		case OJSGte: compare_op(C_GTE,op->p1,op->p2,i+1+op->p3); break;
		case OJSGt: compare_op(C_GT,op->p1,op->p2,i+1+op->p3); break;
		case OJSLte: compare_op(C_LTE,op->p1,op->p2,i+1+op->p3); break;
		case OJULt: EX("if( ((unsigned)r%d) < ((unsigned)r%d) ) goto %s",op->p1,op->p2,lbl(i+1+op->p3)); break;
		case OJUGte: EX("if( ((unsigned)r%d) >= ((unsigned)r%d) ) goto %s",op->p1,op->p2,lbl(i+1+op->p3)); break;
		case OJNotLt: EX("if( !(r%d < r%d) ) goto %s",op->p1,op->p2,lbl(i+1+op->p3)); break;
		case OJNotGte: EX("if( !(r%d >= r%d) ) goto %s",op->p1,op->p2,lbl(i+1+op->p3)); break;
		case OJEq: compare_op(C_EQ,op->p1,op->p2,i+1+op->p3); break;
		case OJNotEq: compare_op(C_NEQ,op->p1,op->p2,i+1+op->p3); break;
		case OJAlways: EX("goto %s",lbl(i+1+op->p1)); break;
		case OLabel: break;
		case OToDyn: { hl_type*tv=rtype(op->p2);
			if(tv->kind==HBOOL){ EX("r%d = hl_alloc_dynbool(r%d)",r,op->p2); break; }
			if(is_ptr(tv)){ LN("if( r%d == NULL ) r%d = NULL; else {",op->p2,r); tabs++; }
			EX("r%d = hl_alloc_dynamic(%s)",r,tval(tv));
			switch(tv->kind){ case HUI8: case HUI16: case HI32: case HBOOL: EX("r%d->v.i = r%d",r,op->p2); break; case HI64: EX("r%d->v.i64 = r%d",r,op->p2); break; case HF32: EX("r%d->v.f = r%d",r,op->p2); break; case HF64: EX("r%d->v.d = r%d",r,op->p2); break; default: EX("r%d->v.ptr = r%d",r,op->p2); }
			if(is_ptr(tv)){ tabs--; LN("}"); } } break;
		case OToSFloat: EX("r%d = (%s)r%d",r,ctype(rtype(r)),op->p2); break;
		case OToUFloat: EX("r%d = (%s)(unsigned)r%d",r,ctype(rtype(r)),op->p2); break;
		case OToInt: EX("r%d = (int)r%d",r,op->p2); break;
		case OSafeCast: { hl_type*ts=rtype(op->p2),*t=rtype(r);
			if(ts->kind==HNULL && ts->tparam==t) EX("r%d = r%d ? r%d%s : 0",r,op->p2,op->p2,dynfield(t));
			else EX("r%d = (%s)hl_dyn_cast%s(&r%d,%s%s)",r,ctype(t),dyn_prefix(t),op->p2,tval(rtype(op->p2)),tvopt(t)); } break;
		case OUnsafeCast: EX("r%d = (%s)r%d",r,ctype(rtype(r)),op->p2); break;
		case OToVirtual: EX("r%d = hl_to_virtual(%s,(vdynamic*)r%d)",r,tval(rtype(r)),op->p2); break;
		case ORet: if(rtype(op->p1)->kind==HVOID) EX("return"); else EX("return %s",rcast(op->p1,fret)); break;
		case OThrow: EX("hl_throw((vdynamic*)r%d)",op->p1); break;
		case ORethrow: EX("hl_rethrow((vdynamic*)r%d)",op->p1); break;
		case OSwitch: { LN("switch(r%d) {",op->p1); tabs++; for(int k=0;k<op->p2;k++) EX("case %d: goto %s",k,lbl(i+1+op->extra[k])); tabs--; LN("}"); } break;
		case ONullCheck: EX("if( r%d == NULL ) hl_null_access()",op->p1); break;
		case OTrap: EX("hl_trap(trap$%d,r%d,%s)",g_trapd,op->p1,lbl(i+1+op->p2)); g_trapd++; break;
		case OEndTrap: EX("hl_endtrap(trap$%d)",g_trapd-1); if(op->p1) g_trapd--; break;
		case OGetI8: EX("r%d = *(unsigned char*)(r%d + r%d)",r,op->p2,op->p3); break;
		case OGetI16: EX("r%d = *(unsigned short*)(r%d + r%d)",r,op->p2,op->p3); break;
		case OGetMem: EX("r%d = *(%s*)(r%d + r%d)",r,ctype(rtype(r)),op->p2,op->p3); break;
		case OGetArray: EX("r%d = ((%s*)(r%d + 1))[r%d]",r,ctype(rtype(r)),op->p2,op->p3); break;
		case OSetI8: EX("*(unsigned char*)(r%d + r%d) = (unsigned char)r%d",op->p1,op->p2,op->p3); break;
		case OSetI16: EX("*(unsigned short*)(r%d + r%d) = (unsigned short)r%d",op->p1,op->p2,op->p3); break;
		case OSetMem: EX("*(%s*)(r%d + r%d) = r%d",ctype(rtype(op->p3)),op->p1,op->p2,op->p3); break;
		case OSetArray: EX("((%s*)(r%d + 1))[r%d] = r%d",ctype(rtype(op->p3)),op->p1,op->p2,op->p3); break;
		case ONew: { hl_type*t=rtype(r); if(t->kind==HOBJ||t->kind==HSTRUCT) EX("r%d = (%s)hl_alloc_obj(%s)",r,ctype(t),tval(t)); else if(t->kind==HDYNOBJ) EX("r%d = hl_alloc_dynobj()",r); else EX("r%d = hl_alloc_virtual(%s)",r,tval(t)); } break;
		case OArraySize: EX("r%d = r%d->size",r,op->p2); break;
		case OType: EX("r%d = &T%d",r,op->p2); break;
		case OGetType: EX("r%d = r%d ? ((vdynamic*)r%d)->t : &hlt_void",r,op->p2,op->p2); break;
		case OGetTID: EX("r%d = r%d->kind",r,op->p2); break;
		case ORef: EX("r%d = &r%d",r,op->p2); break;
		case OUnref: EX("r%d = *r%d",r,op->p2); break;
		case OSetref: EX("*r%d = r%d",op->p1,op->p2); break;
		case OMakeEnum: { hl_type*et=rtype(r); int cid=op->p2; char*ecn=enum_cname(et,cid); hl_enum_construct*ec=&et->tenum->constructs[cid];
			int need_tmp=0; for(int k=0;k<op->p3;k++) if(op->extra[k]==r) need_tmp=1;
			const char*tmp; if(need_tmp){ LN("{ venum *tmp;"); tabs++; tmp="tmp"; } else tmp=NULL;
			if(tmp) EX("tmp = hl_alloc_enum(%s,%d)",tval(et),cid); else EX("r%d = hl_alloc_enum(%s,%d)",r,tval(et),cid);
			for(int k=0;k<op->p3;k++){ if(tmp) EX("((%s*)tmp)->p%d = %s",ecn,k,rcast(op->extra[k],ec->params[k])); else EX("((%s*)r%d)->p%d = %s",ecn,r,k,rcast(op->extra[k],ec->params[k])); }
			if(need_tmp){ EX("r%d = tmp",r); tabs--; LN("}"); } } break;
		case OEnumAlloc: EX("r%d = hl_alloc_enum(%s,%d)",r,tval(rtype(r)),op->p2); break;
		case OEnumIndex: EX("r%d = HL__ENUM_INDEX__(r%d)",r,op->p2); break;
		case OEnumField: { hl_type*et=rtype(op->p2); int cid=op->p3; int pid=(int)(int_val)op->extra; char*ecn=enum_cname(et,cid); hl_enum_construct*ec=&et->tenum->constructs[cid]; EX("%s((%s*)r%d)->p%d",rassign(r,ec->params[pid]),ecn,op->p2,pid); } break;
		case OSetEnumField: { hl_type*et=rtype(op->p1); int cid=0; int pid=op->p2; char*ecn=enum_cname(et,cid); hl_enum_construct*ec=&et->tenum->constructs[cid]; EX("((%s*)r%d)->p%d = (%s)r%d",ecn,op->p1,pid,ctype(ec->params[pid]),op->p3); } break;
		case ORefData: EX("r%d = (%s)hl_aptr(r%d,void*)",r,ctype(rtype(r)),op->p2); break;
		case ORefOffset: EX("r%d = r%d + r%d",r,op->p2,op->p3); break;
		case ONop: break;
		default: EX("hl_fatal(\"opcode %d not implemented\")",op->op); break;
		}
	}
	if(label_at[nops]) LN("L%d_%d: ;",CFI,nops);
	tabs--; LN("}");
	free(label_at);
}
