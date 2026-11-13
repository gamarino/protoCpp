# cpython_costmodel.py — CPython twin of kernel_costmodel.cpp (same operations,
# timed in-process, best of 5). Lines: RES op=<name> ns=<ns/op> net_ns=<minus loop>.
import time, random
def best(f, n, reps=5):
    b=1e30
    for _ in range(reps+1):
        t=time.perf_counter_ns(); f(); d=(time.perf_counter_ns()-t)/n; b=min(b,d)
    return b
N=100000
def loop():
    for i in range(N): pass
base=best(loop,N)
def r(name,f,n,sub=True):
    t=best(f,n); print(f"RES op={name.replace(' ','_')} ns={t:.1f} net_ns={t-base if sub else t:.1f}")
print(f"RES op=empty_for_loop_iteration ns={base:.1f} net_ns=0")
def ia():
    t=0
    for i in range(N): t=t+i
r("int add",ia,N)
def da():
    t=0.0
    for i in range(N): t=t+0.5
r("double add",da,N)
calls=0
def fib(n):
    if n<2: return n
    return fib(n-1)+fib(n-2)
def fc(n):
    return 1 if n<2 else 1+fc(n-1)+fc(n-2)
nc=fc(20)
r("fib(20): per call",lambda: fib(20),nc,False)
class O: pass
o=O(); o.a0=0;o.a1=1;o.a2=2;o.a3=3
def ga():
    for i in range(N//3):
        x=o.a0;x=o.a1;x=o.a2
r("getattr x3 (per read)",ga,(N//3)*3)
def sa():
    for i in range(N): o.a1=i
r("setattr",sa,N)
def ni():
    for i in range(N//10):
        x=O(); x.a0=0;x.a1=1;x.a2=2;x.a3=3
r("new object + 4 attrs",ni,N//10)
for n in (1000,10000,100000):
    def ap(n=n):
        l=[]
        for i in range(n): l.append(i)
    r(f"list append, build to {n}",ap,n)
L=list(range(100000))
def gr():
    for i in range(N): x=L[(i*7919)%100000]
r("list index random 100k (incl. mul/mod)",gr,N)
def gs():
    for i in range(N): x=L[i]
r("list index sequential 100k",gs,N)
for n in (2000,10000):
    def sc(n=n):
        s=""
        for i in range(n): s+="x"
    r(f"str += 'x', build to {n}",sc,n)