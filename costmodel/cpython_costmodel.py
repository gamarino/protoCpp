# cpython_costmodel.py — the CPython *interpreter* twin of kernel_costmodel.cpp:
# the same operations as Python code, timed in-process. It measures what a
# Python program pays (bytecode dispatch included); the fair comparison with
# the kernel's C++ API is capi_costmodel.c. Read both.
#
# Every operation runs inside a function with its data in locals (no global
# lookups), one warm-up batch and BATCHES measured batches; wall clock and
# process CPU time per batch. Each operation checks its result: a mismatch
# exits with status 2.
#
# Lines: RES op=<key> ns=<median> min= max= cpu=<median> cpu_max= net_ns=<median minus empty loop> cells=0
import sys, time

BATCHES = 7
N = 100000


def fail(what):
    print(f"VERIFY FAILED: {what}", file=sys.stderr)
    sys.exit(2)


def median(v):
    v = sorted(v)
    n = len(v)
    return v[n // 2] if n % 2 else 0.5 * (v[n // 2 - 1] + v[n // 2])


def run(f, reps):
    walls, cpus = [], []
    for batch in range(BATCHES + 1):
        w0 = time.perf_counter_ns(); c0 = time.process_time_ns()
        for _ in range(reps):
            f()
        w = time.perf_counter_ns() - w0; c = time.process_time_ns() - c0
        if batch > 0:
            walls.append(w); cpus.append(c)
    return walls, cpus


def report(key, f, reps, ops, base=None):
    walls, cpus = run(f, reps)
    per = reps * ops
    walls = sorted(w / per for w in walls); cpus = sorted(c / per for c in cpus)
    m = median(walls)
    net = m - base if base is not None else m
    print(f"RES op={key} ns={m:.2f} min={walls[0]:.2f} max={walls[-1]:.2f} "
          f"cpu={median(cpus):.2f} cpu_max={cpus[-1]:.2f} net_ns={net:.2f} cells=0", flush=True)
    return m


def empty_loop():
    for i in range(N):
        pass


def int_add():
    t = 0
    for i in range(N):
        t = t + i
    if t != N * (N - 1) // 2: fail("int_add")


def double_add():
    t = 0.0
    for i in range(10000):
        t = t + 0.5
    if t != 5000.0: fail("double_add")


def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)


def fib_calls(n):
    return 1 if n < 2 else 1 + fib_calls(n - 1) + fib_calls(n - 2)


def fib20():
    if fib(20) != 6765: fail("fib(20)")


class O:
    pass


def main():
    base = report("empty_loop_iteration", empty_loop, 200, N)
    report("int_add", int_add, 200, N, base)
    report("double_add", double_add, 200, 10000, base)
    report("fib20_step", fib20, 20, fib_calls(20))   # a real Python call per step

    o = O(); o.a0 = 0; o.a1 = 1; o.a2 = 2

    def getattr3(o=o):
        for i in range(10000):
            x = o.a0; x = o.a1; x = o.a2
        if x != 2: fail("getattr")
    report("getattr", getattr3, 200, 30000)

    def setattr4(o=o):
        for i in range(0, 10000, 4):
            o.a0 = i; o.a1 = i + 1; o.a2 = i + 2; o.a3 = i + 3
        if o.a3 != 9999: fail("setattr")
    report("setattr_mutable", setattr4, 200, 10000)

    def new_object(O=O):
        for i in range(2000):
            x = O(); x.a0 = 0; x.a1 = 1; x.a2 = 2; x.a3 = 3
        if x.a3 != 3: fail("new object")
    report("new_object_4_attrs", new_object, 200, 2000)

    for n in (1000, 10000, 100000):
        def append(n=n):
            l = []
            for i in range(n):
                l.append(i)          # the plain form: 3.14 specialises it (CALL_LIST_APPEND)
            if len(l) != n or l[-1] != n - 1: fail("list append")
        report(f"list_append_to_{n}", append, max(2, 2000000 // n), n, base)

    big = list(range(100000))

    # The indices are computed once: the kernel and C-API twins compute them
    # with a native multiply and modulo that costs ~1 ns, whereas
    # `(i * 7919) % 100000` here would allocate two ints per access.
    idx = [(i * 7919) % 100000 for i in range(N)]

    def getat_random(L=big, idx=idx):
        for j in idx:
            x = L[j]
        if x != (99999 * 7919) % 100000: fail("getAt random")
    report("list_getat_random_100k", getat_random, 20, N, base)

    def getat_seq(L=big):
        for i in range(N):
            x = L[i]
        if x != 99999: fail("getAt sequential")
    report("list_getat_seq_100k", getat_seq, 20, N, base)

    for n in (2000, 10000):
        def sappend(n=n):
            s = ""
            for i in range(n):
                s += "x"
            if len(s) != n: fail("string append")
        report(f"str_append_to_{n}", sappend, max(2, 400000 // n), n, base)

    gil = sys._is_gil_enabled() if hasattr(sys, "_is_gil_enabled") else True
    print(f"INFO gil_enabled={int(gil)} version={sys.version.split()[0]}"
          f" {'free-threading' if 'free-threading' in sys.version else 'gil'}")


main()
