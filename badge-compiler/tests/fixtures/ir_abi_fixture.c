struct one { signed char x; };
struct two { unsigned short x; };
struct three { unsigned char x[3]; };
struct pair { int x,y; };
struct wide { long long x; int y; };
struct big { int x[20]; };
struct one abi_one(struct one x);
struct two abi_two(struct two x);
struct three abi_three(struct three x);
struct pair abi_pair(int prefix,struct pair x,int suffix);
struct wide abi_wide(struct wide x);
struct big abi_big(struct big x);
signed char abi_signed(signed char x);
unsigned short abi_unsigned(unsigned short x);
_Bool abi_bool(_Bool x);
unsigned abi_zero_from_signed(signed char x);
unsigned char abi_reinterpret8(signed char x);
#ifdef CALLER
int main(void) {
    if(abi_one((struct one){-7}).x!=-5) return 1;
    if(abi_two((struct two){65530}).x!=65532) return 2;
    struct three t=abi_three((struct three){{1,2,3}});
    if(t.x[0]!=1 || t.x[1]!=4 || t.x[2]!=3) return 3;
    struct pair p=abi_pair(3,(struct pair){5,7},11);
    if(p.x!=8 || p.y!=18) return 4;
    struct wide w=abi_wide((struct wide){0x1122334455667788LL,9});
    if(w.x!=0x112233445566778aLL || w.y!=12) return 5;
    struct big b=abi_big((struct big){{1,2,3}});
    if(b.x[0]!=3 || b.x[1]!=2 || b.x[2]!=3 || b.x[19]!=7) return 7;
    if(abi_signed(-128)!=-126 || abi_unsigned(65530)!=65532 || abi_bool(3)!=0) return 8;
    if(abi_zero_from_signed(-4)!=252 || abi_reinterpret8(-4)!=252) return 9;
    return 0;
}
#else
struct one abi_one(struct one x) { x.x+=2; return x; }
struct two abi_two(struct two x) { x.x+=2; return x; }
struct three abi_three(struct three x) { x.x[1]+=2; return x; }
struct pair abi_pair(int prefix,struct pair x,int suffix) { x.x+=prefix;x.y+=suffix;return x; }
struct wide abi_wide(struct wide x) { x.x+=2;x.y+=3;return x; }
struct big abi_big(struct big x) { x.x[0]+=2;x.x[19]=7;return x; }
signed char abi_signed(signed char x) { return x+2; }
unsigned short abi_unsigned(unsigned short x) { return x+2; }
_Bool abi_bool(_Bool x) { return !x; }
unsigned abi_zero_from_signed(signed char x) { return (unsigned char)x; }
unsigned char abi_reinterpret8(signed char x) { return x; }
#endif
