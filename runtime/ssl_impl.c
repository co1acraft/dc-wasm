// ssl_impl.c : a real ssl_dgst_make (SHA-256 / SHA-1) so Dead Cells' pak stamp
// verifies, without pulling in mbedtls. Link this instead of the ssl_dgst_make stub.
#include <hl.h>
#include <stdint.h>
#include <string.h>

/* ---------------- SHA-256 ---------------- */
typedef struct { uint32_t s[8]; uint64_t bits; uint8_t buf[64]; uint32_t n; } sha256_ctx;
static const uint32_t K256[64] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x,n) (((x)>>(n))|((x)<<(32-(n))))
static void sha256_blk(sha256_ctx*c,const uint8_t*p){
	uint32_t w[64],a,b,cc,d,e,f,g,h,t1,t2; int i;
	for(i=0;i<16;i++) w[i]=(p[i*4]<<24)|(p[i*4+1]<<16)|(p[i*4+2]<<8)|p[i*4+3];
	for(i=16;i<64;i++){ uint32_t s0=ROR(w[i-15],7)^ROR(w[i-15],18)^(w[i-15]>>3);
		uint32_t s1=ROR(w[i-2],17)^ROR(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1; }
	a=c->s[0];b=c->s[1];cc=c->s[2];d=c->s[3];e=c->s[4];f=c->s[5];g=c->s[6];h=c->s[7];
	for(i=0;i<64;i++){ uint32_t S1=ROR(e,6)^ROR(e,11)^ROR(e,25); uint32_t ch=(e&f)^((~e)&g);
		t1=h+S1+ch+K256[i]+w[i]; uint32_t S0=ROR(a,2)^ROR(a,13)^ROR(a,22); uint32_t mj=(a&b)^(a&cc)^(b&cc);
		t2=S0+mj; h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2; }
	c->s[0]+=a;c->s[1]+=b;c->s[2]+=cc;c->s[3]+=d;c->s[4]+=e;c->s[5]+=f;c->s[6]+=g;c->s[7]+=h;
}
static void sha256_init(sha256_ctx*c){ c->s[0]=0x6a09e667;c->s[1]=0xbb67ae85;c->s[2]=0x3c6ef372;c->s[3]=0xa54ff53a;
	c->s[4]=0x510e527f;c->s[5]=0x9b05688c;c->s[6]=0x1f83d9ab;c->s[7]=0x5be0cd19;c->bits=0;c->n=0; }
static void sha256_update(sha256_ctx*c,const uint8_t*p,size_t len){ c->bits+=(uint64_t)len*8;
	while(len){ uint32_t k=64-c->n; if(k>len)k=(uint32_t)len; memcpy(c->buf+c->n,p,k); c->n+=k; p+=k; len-=k;
		if(c->n==64){ sha256_blk(c,c->buf); c->n=0; } } }
static void sha256_final(sha256_ctx*c,uint8_t*out){ uint64_t b=c->bits; uint8_t pad=0x80; sha256_update(c,&pad,1);
	uint8_t z=0; while(c->n!=56) sha256_update(c,&z,1);
	uint8_t L[8]; for(int i=0;i<8;i++) L[i]=(uint8_t)(b>>(56-8*i)); sha256_update(c,L,8);
	for(int i=0;i<8;i++){ out[i*4]=c->s[i]>>24; out[i*4+1]=c->s[i]>>16; out[i*4+2]=c->s[i]>>8; out[i*4+3]=c->s[i]; } }

/* ---------------- SHA-1 ---------------- */
typedef struct { uint32_t s[5]; uint64_t bits; uint8_t buf[64]; uint32_t n; } sha1_ctx;
static void sha1_blk(sha1_ctx*c,const uint8_t*p){ uint32_t w[80],a,b,cc,d,e,t; int i;
	for(i=0;i<16;i++) w[i]=(p[i*4]<<24)|(p[i*4+1]<<16)|(p[i*4+2]<<8)|p[i*4+3];
	for(i=16;i<80;i++){ t=w[i-3]^w[i-8]^w[i-14]^w[i-16]; w[i]=(t<<1)|(t>>31); }
	a=c->s[0];b=c->s[1];cc=c->s[2];d=c->s[3];e=c->s[4];
	for(i=0;i<80;i++){ uint32_t f,k; if(i<20){f=(b&cc)|((~b)&d);k=0x5a827999;} else if(i<40){f=b^cc^d;k=0x6ed9eba1;}
		else if(i<60){f=(b&cc)|(b&d)|(cc&d);k=0x8f1bbcdc;} else {f=b^cc^d;k=0xca62c1d6;}
		t=((a<<5)|(a>>27))+f+e+k+w[i]; e=d;d=cc;cc=(b<<30)|(b>>2);b=a;a=t; }
	c->s[0]+=a;c->s[1]+=b;c->s[2]+=cc;c->s[3]+=d;c->s[4]+=e; }
static void sha1_init(sha1_ctx*c){ c->s[0]=0x67452301;c->s[1]=0xefcdab89;c->s[2]=0x98badcfe;c->s[3]=0x10325476;c->s[4]=0xc3d2e1f0;c->bits=0;c->n=0; }
static void sha1_update(sha1_ctx*c,const uint8_t*p,size_t len){ c->bits+=(uint64_t)len*8;
	while(len){ uint32_t k=64-c->n; if(k>len)k=(uint32_t)len; memcpy(c->buf+c->n,p,k); c->n+=k; p+=k; len-=k;
		if(c->n==64){ sha1_blk(c,c->buf); c->n=0; } } }
static void sha1_final(sha1_ctx*c,uint8_t*out){ uint64_t b=c->bits; uint8_t pad=0x80; sha1_update(c,&pad,1);
	uint8_t z=0; while(c->n!=56) sha1_update(c,&z,1);
	uint8_t L[8]; for(int i=0;i<8;i++) L[i]=(uint8_t)(b>>(56-8*i)); sha1_update(c,L,8);
	for(int i=0;i<5;i++){ out[i*4]=c->s[i]>>24; out[i*4+1]=c->s[i]>>16; out[i*4+2]=c->s[i]>>8; out[i*4+3]=c->s[i]; } }

static int ieq(const char*a,const char*b){ while(*a&&*b){ char ca=*a,cb=*b; if(ca>='A'&&ca<='Z')ca+=32; if(cb>='A'&&cb<='Z')cb+=32; if(ca!=cb)return 0; a++;b++; } return *a==*b; }

/* matches ssl.hdll: vbyte *ssl_dgst_make(vbyte *data, int len, vbyte *alg, int *size) */
HL_PRIM vbyte *ssl_dgst_make(vbyte *data, int len, vbyte *alg, int *size){
	const char*a=(const char*)alg;
	if(ieq(a,"sha256")){ vbyte*o=hl_gc_alloc_noptr(32); sha256_ctx c; sha256_init(&c); sha256_update(&c,(const uint8_t*)data,(size_t)len); sha256_final(&c,(uint8_t*)o); *size=32; return o; }
	if(ieq(a,"sha1")){ vbyte*o=hl_gc_alloc_noptr(20); sha1_ctx c; sha1_init(&c); sha1_update(&c,(const uint8_t*)data,(size_t)len); sha1_final(&c,(uint8_t*)o); *size=20; return o; }
	hl_error("Unsupported hash algorithm in ssl_impl"); return NULL;
}
