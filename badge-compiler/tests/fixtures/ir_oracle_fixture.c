typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
struct pair { int x; int y; };
struct triple { int x; int y; int z; };
struct small { u8 x[3]; };
union word { u32 x; u8 b[4]; };
static volatile u32 reads;
static int values[7] = { [1]=7, [4]=13 };
static struct pair origin={.y=4,.x=-3};
static const char text[]="abc";
static u64 large=0xfedcba9876543210ULL;
static int step(int x) { reads++; return x+1; }
static struct pair shifted(struct pair a,struct triple b) {
    a.x+=b.x; a.y+=b.y+b.z; return a;
}
static struct small tiny(struct small x) { x.x[1]++; return x; }
static int arithmetic(u32 x,int y) {
    u8 a=(u8)x; short b=(short)y;
    a+=3; b*=2;
    return (int)a+b+(int)(x>>28)+(y>>3);
}
static int control(int n) {
    int sum=0;
    for(int k=0;k<n;k++) {
        if(k==2) continue;
        switch(k%4) {
            case 0:sum+=3;break;
            case 1:sum-=2;break;
            case 2:sum+=5;
            default:sum++;
        }
        if(k>9) break;
    }
    int k=3;
    do { sum+=k; } while(--k);
    while(n>0) { n--; if(n==2) goto done; sum++; }
done:
    return sum;
}
static int locals(int input) {
    int array[4]={input,2,3,4};
    struct pair p={.y=input,.x=3};
    struct pair q=p;
    q=(struct pair){7,8};
    struct pair r=input?p:q;
    static int count;
    count++;
    return array[0]+array[3]+r.x+r.y+count+(int)(&array[3]-array);
}
static int indirect(int (*fn)(int),int x) { return fn(x); }
static double floating(int x) { float a=x; a/=2; ++a; return (double)a+3; }
static void assembly(void) { __asm__ volatile(".Lbadge$label: nop":::"memory"); }
int main(void) {
    assembly();
    struct pair p=shifted(origin,(struct triple){2,3,4});
    if(p.x!=-1 || p.y!=11) return 1;
    struct small s=tiny((struct small){{2,3,4}});
    if(s.x[0]!=2 || s.x[1]!=4 || s.x[2]!=4) return 2;
    if(values[0]!=0 || values[1]!=7 || values[4]!=13) return 3;
    if(text[2]!='c' || sizeof(text)!=4 || large!=0xfedcba9876543210ULL) return 4;
    if(arithmetic(0xf12345ffu,-123)!=-245) return 5;
    if(control(12)!=32 || locals(5)!=21 || locals(0)!=24) return 6;
    reads=0;
    if((0 && step(1)) || (1 || step(2))!=1 || reads!=0) return 7;
    int a=(step(0),step(1));
    if(a!=2 || reads!=2 || (a?step(3):step(4))!=4 || reads!=3) return 8;
    reads=255;
    if(reads++!=255 || ++reads!=257 || reads!=257) return 9;
    _Bool flag=3;
    if(!flag || (_Bool)0 || indirect(step,2)!=3 || reads!=258) return 10;
    union word w={.x=0x12345678u};
    if(w.b[0]!=0x78 || w.b[3]!=0x12) return 11;
    if(_Generic(values,int*:5,default:7)!=5) return 12;
    if(floating(6)!=7) return 13;
    return 0;
}
