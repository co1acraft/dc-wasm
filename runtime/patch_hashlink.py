#!/usr/bin/env python3
# Patch a freshly-cloned HashLink tree for the wasm/Dead-Cells build.
#
#   1. src/std/file.c : file reads via explicit-offset I/O + GC-root pinning of
#      open descriptors. Two backends, chosen at compile time:
#         (default, node)  -DDC nothing   -> NODEFS + node fs.readSync
#         (browser)        -DDC_BROWSER   -> synchronous XHR Range requests
#      The pinning fixes a real bug: HashLink's conservative GC can't see
#      pointers held only in wasm locals, so an in-use fd object could be
#      collected mid-read (its finalizer closing the file).
#
#   2. src/gc.c : disable automatic collection (temporary workaround for the
#      same wasm-locals limitation; replace with a precise/spilling GC for a
#      production build). Enabled by default via gc_flags.
#
# Targets HashLink master around commit 459601b (Haxe 4.x era). If upstream
# changes these files, the string anchors below may need updating.
import sys, os

def patch_file_c(p):
    s = open(p).read()
    if "DC_BROWSER" in s:
        print("file.c already patched"); return
    s = s.replace("#include <errno.h>\n#endif", """#include <errno.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#ifdef DC_BROWSER
EM_JS(double, js_xhr_size, (int pathPtr), {
  var p = UTF8ToString(pathPtr);
  if(p.indexOf('/data/')===0) p=p.substring(6); else if(p.charCodeAt(0)===47) p=p.substring(1);
  try { var x=new XMLHttpRequest(); x.open('HEAD','files/'+p,false); x.send();
    if(x.status>=200 && x.status<300){ var l=x.getResponseHeader('Content-Length'); return l?parseFloat(l):-1; }
    return -1; } catch(e){ return -1; }
});
EM_JS(int, js_xhr_read, (int pathPtr, int ptr, int len, double pos), {
  var p = UTF8ToString(pathPtr);
  if(p.indexOf('/data/')===0) p=p.substring(6); else if(p.charCodeAt(0)===47) p=p.substring(1);
  try { var x=new XMLHttpRequest(); x.open('GET','files/'+p,false); x.responseType='arraybuffer';
    x.setRequestHeader('Range','bytes='+pos+'-'+(pos+len-1)); x.send();
    if(x.status===206||x.status===200){ var a=new Uint8Array(x.response); var n=Math.min(a.length,len); HEAPU8.set(a.subarray(0,n),ptr); return n; }
    return -1; } catch(e){ return -1; }
});
#else
EM_JS(int, js_fread, (int emfd, int ptr, int len, double pos), {
  try { var st=FS.streams[emfd]; if(!st) return -1; var nfd=st.nfd; if(nfd===undefined) return -1;
    var fs=require('fs'); var nb=Buffer.allocUnsafe(len); var got=0;
    while(got<len){ var r=fs.readSync(nfd,nb,got,len-got,pos+got); if(r<=0) break; got+=r; }
    if(got>0) HEAPU8.set(new Uint8Array(nb.buffer,nb.byteOffset,got),ptr); return got;
  } catch(e){ return -1; }
});
#endif
#endif""", 1)
    s = s.replace("""struct _hl_fdesc {
	void (*finalize)( hl_fdesc * );
	FILE *f;
#	ifdef HL_WIN_DESKTOP
	bool is_std;
#	endif
};""", """struct _hl_fdesc {
	void (*finalize)( hl_fdesc * );
	FILE *f;
	int64 fpos;
	int64 fsize;
	char *url;
	int seekable;
#	ifdef HL_WIN_DESKTOP
	bool is_std;
#	endif
};""")
    s = s.replace("static void fdesc_finalize( hl_fdesc *f ) {\n\tif( f->f ) fclose(f->f);", """#ifdef __EMSCRIPTEN__
#ifdef DC_BROWSER
#define HL_PIN_MAX 4096
static hl_fdesc* hl_pinned[HL_PIN_MAX]; static int hl_pinned_n=0;
static void hl_pin_fd( hl_fdesc *fd ){ if(hl_pinned_n<HL_PIN_MAX){ hl_pinned[hl_pinned_n]=fd; hl_add_root(&hl_pinned[hl_pinned_n]); hl_pinned_n++; } }
#else
#include <pthread.h>
#define HL_PIN_MAX 4096
static hl_fdesc* hl_pinned[HL_PIN_MAX]; static int hl_pinned_n=0; static pthread_mutex_t hl_pin_mx=PTHREAD_MUTEX_INITIALIZER;
static void hl_pin_fd( hl_fdesc *fd ){ pthread_mutex_lock(&hl_pin_mx); if(hl_pinned_n<HL_PIN_MAX){ hl_pinned[hl_pinned_n]=fd; hl_add_root(&hl_pinned[hl_pinned_n]); hl_pinned_n++; } pthread_mutex_unlock(&hl_pin_mx); }
#endif
#endif
static void fdesc_finalize( hl_fdesc *f ) {
	if( f->f ) fclose(f->f);""")
    s = s.replace("""	hl_fdesc *fd;
	if( f == NULL ) return NULL;
	fd = (hl_fdesc*)hl_gc_alloc_finalizer(sizeof(hl_fdesc));
	fd->finalize = fdesc_finalize;
	fd->f = f;
	SET_IS_STD(fd, false);
	return fd;""", """	hl_fdesc *fd;
#if defined(__EMSCRIPTEN__) && defined(DC_BROWSER)
	{ char u[1024]; int i=0; for(;((char*)name)[i]&&i<1023;i++) u[i]=((char*)name)[i]; u[i]=0;
	  double zz = js_xhr_size((int)(intptr_t)u);
	  if( zz < 0 ){ if(f) fclose(f); return NULL; }
	  if(f) fclose(f);
	  fd = (hl_fdesc*)hl_gc_alloc_finalizer(sizeof(hl_fdesc));
	  fd->finalize = fdesc_finalize; fd->f = NULL; fd->fpos=0; fd->fsize=(int64)zz; fd->seekable=1;
	  fd->url = (char*)malloc(i+1); memcpy(fd->url,u,i+1);
	  hl_pin_fd(fd); return fd; }
#endif
	if( f == NULL ) return NULL;
	fd = (hl_fdesc*)hl_gc_alloc_finalizer(sizeof(hl_fdesc));
	fd->finalize = fdesc_finalize;
	fd->f = f;
	fd->fpos = 0; fd->seekable = 0; fd->url = NULL;
#if defined(__EMSCRIPTEN__) && !defined(DC_BROWSER)
	fd->seekable = 1; hl_pin_fd(fd);
#endif
	SET_IS_STD(fd, false);
	return fd;""")
    s = s.replace("""	if( !f ) return -1;
	hl_blocking(true);
	ret = (int)fread((char*)buf+pos,1,len,f->f);
	hl_blocking(false);
	return ret;""", """	if( !f ) return -1;
#if defined(__EMSCRIPTEN__) && defined(DC_BROWSER)
	if( f->seekable ){ ret = js_xhr_read((int)(intptr_t)f->url,(int)(intptr_t)((char*)buf+pos),len,(double)f->fpos); if(ret>0) f->fpos+=ret; return ret; }
#elif defined(__EMSCRIPTEN__)
	if( f->seekable ){ ret = js_fread(fileno(f->f),(int)(intptr_t)((char*)buf+pos),len,(double)f->fpos); if(ret>0) f->fpos+=ret; return ret; }
#endif
	ret = (int)fread((char*)buf+pos,1,len,f->f);
	return ret;""")
    s = s.replace("""	unsigned char cc;
	hl_blocking(true);
	if( !f || fread(&cc,1,1,f->f) != 1 ) {
		hl_blocking(false);
		return -2;
	}
	hl_blocking(false);
	return cc;""", """	unsigned char cc;
	if( !f ) return -2;
#if defined(__EMSCRIPTEN__) && defined(DC_BROWSER)
	if( f->seekable ){ if( js_xhr_read((int)(intptr_t)f->url,(int)(intptr_t)&cc,1,(double)f->fpos)!=1 ) return -2; f->fpos++; return cc; }
#elif defined(__EMSCRIPTEN__)
	if( f->seekable ){ if( js_fread(fileno(f->f),(int)(intptr_t)&cc,1,(double)f->fpos)!=1 ) return -2; f->fpos++; return cc; }
#endif
	if( fread(&cc,1,1,f->f) != 1 ) return -2;
	return cc;""")
    s = s.replace("""HL_PRIM bool hl_file_seek( hl_fdesc *f, int pos, int kind ) {
	if( !f ) return false;
	return fseek(f->f,pos,kind) == 0;
}""", """HL_PRIM bool hl_file_seek( hl_fdesc *f, int pos, int kind ) {
	if( !f ) return false;
#if defined(__EMSCRIPTEN__) && defined(DC_BROWSER)
	if( f->seekable ){ int64 np; if(kind==SEEK_SET)np=pos; else if(kind==SEEK_CUR)np=f->fpos+pos; else np=f->fsize+pos; f->fpos=np; return true; }
#elif defined(__EMSCRIPTEN__)
	if( f->seekable ){ struct stat st; int64 np; if(kind==SEEK_SET)np=pos; else if(kind==SEEK_CUR)np=f->fpos+pos; else {if(fstat(fileno(f->f),&st))return false; np=(int64)st.st_size+pos;} f->fpos=np; return true; }
#endif
	return fseek(f->f,pos,kind) == 0;
}""")
    s = s.replace("""HL_PRIM int hl_file_tell( hl_fdesc *f ) {
	if( !f ) return -1;
	return (int)ftell(f->f);
}""", """HL_PRIM int hl_file_tell( hl_fdesc *f ) {
	if( !f ) return -1;
#ifdef __EMSCRIPTEN__
	if( f->seekable ) return (int)f->fpos;
#endif
	return (int)ftell(f->f);
}""")
    s = s.replace("""	return fseek(f->f,(int64)pos,kind) == 0;
#	endif
}""", """#if defined(__EMSCRIPTEN__) && defined(DC_BROWSER)
	if( f->seekable ){ int64 np; if(kind==SEEK_SET)np=(int64)pos; else if(kind==SEEK_CUR)np=f->fpos+(int64)pos; else np=f->fsize+(int64)pos; f->fpos=np; return true; }
#elif defined(__EMSCRIPTEN__)
	if( f->seekable ){ struct stat st; int64 np; if(kind==SEEK_SET)np=(int64)pos; else if(kind==SEEK_CUR)np=f->fpos+(int64)pos; else {if(fstat(fileno(f->f),&st))return false; np=(int64)st.st_size+(int64)pos;} f->fpos=np; return true; }
#endif
	return fseeko(f->f,(off_t)(int64)pos,kind) == 0;
#	endif
}""")
    s = s.replace("""	return (double)ftell(f->f);
#	endif
}""", """	if( f->seekable ) return (double)f->fpos;
	return (double)ftello(f->f);
#	endif
}""")
    open(p, "w").write(s)
    print("patched", p)

def patch_gc_c(p):
    s = open(p).read()
    if "GC_DISABLE on" in s:
        print("gc.c already patched"); return
    s = s.replace("static int gc_flags = 0;",
                  "static int gc_flags = 64; /* GC_DISABLE on (temporary: wasm conservative-scan can't see wasm locals) */", 1)
    s = s.replace("static void gc_major() {",
                  "static void gc_major() {\n\tif( gc_flags & 64 ) return; /* GC_DISABLE */", 1)
    open(p, "w").write(s)
    print("patched", p)

if __name__ == "__main__":
    hl = sys.argv[1] if len(sys.argv) > 1 else "vendor/hashlink"
    patch_file_c(os.path.join(hl, "src/std/file.c"))
    patch_gc_c(os.path.join(hl, "src/gc.c"))
