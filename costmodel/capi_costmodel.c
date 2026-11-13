// capi_costmodel — the fair CPython twin of kernel_costmodel.cpp.
//
// kernel_costmodel calls protoCore's C++ API from compiled code, so its figures
// carry no interpreter. Comparing them with CPython *interpreted* code
// (cpython_costmodel.py) charges CPython for bytecode dispatch that the kernel
// side never pays. This program calls CPython's C API from compiled C instead,
// with the same operations, sizes, repetitions and result checks as
// kernel_costmodel: a library primitive against a library primitive.
//
// Built by costmodel/run.sh against the interpreter being measured (its own
// headers and libpython), so the twin and the interpreted run use the same
// CPython. Output format: bench_common.h (cells= is not applicable and is 0).
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "bench_common.h"

typedef void (*cm_body)(void);

static void measure(const char* key, long reps, long opsPerRep, cm_body body) {
    cm_samples s;
    memset(&s, 0, sizeof s);
    for (int batch = 0; batch <= COSTMODEL_BATCHES; ++batch) {
        double w0 = cm_wall_ns(), c0 = cm_cpu_ns();
        for (long r = 0; r < reps; ++r) body();
        double ops = (double)reps * (double)opsPerRep;
        double w = (cm_wall_ns() - w0) / ops, cpu = (cm_cpu_ns() - c0) / ops;
        if (batch > 0) cm_add(&s, w, cpu);
    }
    printf("RES op=%s", key);
    cm_print_times(&s, 1.0);
    printf(" cells=0\n");
    fflush(stdout);
}

static void check(int ok, const char* what) {
    if (PyErr_Occurred()) { PyErr_Print(); cm_fail(what); }
    if (!ok) cm_fail(what);
}

static PyObject *ONE, *TWO, *HALF, *X, *EMPTY;
static PyObject* NM[16];
static PyObject *CLS, *MOBJ, *TARGET, *BIG;
static long g_calls, g_n;

static void b_int_add(void) {
    PyObject* t = PyLong_FromLong(0);
    for (long i = 0; i < 100000; ++i) {
        PyObject* x = PyLong_FromLong(i);
        PyObject* nt = PyNumber_Add(t, x);
        Py_DECREF(x); Py_DECREF(t); t = nt;
    }
    check(PyLong_AsLongLong(t) == 100000LL * 99999 / 2, "int_add sum");
    Py_DECREF(t);
}

static void b_double_add(void) {
    PyObject* t = PyFloat_FromDouble(0.0);
    for (long i = 0; i < 10000; ++i) { PyObject* nt = PyNumber_Add(t, HALF); Py_DECREF(t); t = nt; }
    check(PyFloat_AsDouble(t) == 5000.0, "double_add sum");
    Py_DECREF(t);
}

// The same C recursion as the kernel's: compare, two subtractions, an addition.
static PyObject* fib(PyObject* n) {
    ++g_calls;
    if (PyObject_RichCompareBool(n, TWO, Py_LT)) return Py_NewRef(n);
    PyObject* a = PyNumber_Subtract(n, ONE);
    PyObject* fa = fib(a);
    PyObject* b = PyNumber_Subtract(n, TWO);
    PyObject* fb = fib(b);
    PyObject* r = PyNumber_Add(fa, fb);
    Py_DECREF(a); Py_DECREF(fa); Py_DECREF(b); Py_DECREF(fb);
    return r;
}
static void b_fib(void) {
    PyObject* n = PyLong_FromLong(20);
    PyObject* r = fib(n);
    check(PyLong_AsLong(r) == 6765, "fib(20)");
    Py_DECREF(r); Py_DECREF(n);
}

static void b_getattr(void) {
    PyObject* v = NULL;
    for (long i = 0; i < 10000; ++i)
        for (int k = 0; k < 3; ++k) { Py_XDECREF(v); v = PyObject_GetAttr(MOBJ, NM[k]); }
    check(PyLong_AsLong(v) == 2, "getattr value");
    Py_DECREF(v);
}

static void b_setattr(void) {
    for (long i = 0; i < 10000; ++i) {
        PyObject* x = PyLong_FromLong(i);
        PyObject_SetAttr(MOBJ, NM[i & 3], x);
        Py_DECREF(x);
    }
    PyObject* v = PyObject_GetAttr(MOBJ, NM[3]);
    check(PyLong_AsLong(v) == 9999, "setattr value");
    Py_DECREF(v);
}

static void b_new_object(void) {
    PyObject* o = NULL;
    for (long i = 0; i < 2000; ++i) {
        Py_XDECREF(o);
        o = PyObject_CallNoArgs(CLS);
        for (int k = 0; k < 4; ++k) {
            PyObject* x = PyLong_FromLong(k);
            PyObject_SetAttr(o, NM[k], x);
            Py_DECREF(x);
        }
    }
    PyObject* v = PyObject_GetAttr(o, NM[3]);
    check(PyLong_AsLong(v) == 3, "new object attrs");
    Py_DECREF(v); Py_DECREF(o);
}

static void b_mutable_write(void) {
    for (long i = 0; i < 5000; ++i) {
        PyObject* x = PyLong_FromLong(i);
        PyObject_SetAttr(TARGET, NM[i & 15], x);
        Py_DECREF(x);
    }
    PyObject* v = PyObject_GetAttr(TARGET, NM[4999 & 15]);
    check(PyLong_AsLong(v) == 4999, "mutable write value");
    Py_DECREF(v);
}

static void b_list_append(void) {
    PyObject* l = PyList_New(0);
    for (long i = 0; i < g_n; ++i) { PyObject* x = PyLong_FromLong(i); PyList_Append(l, x); Py_DECREF(x); }
    check(PyList_GET_SIZE(l) == g_n && PyLong_AsLong(PyList_GET_ITEM(l, g_n - 1)) == g_n - 1, "list append");
    Py_DECREF(l);
}

static void b_getat_random(void) {
    PyObject* v = NULL;
    for (long i = 0; i < 100000; ++i) { Py_XDECREF(v); v = PyList_GetItemRef(BIG, (i * 7919) % 100000); }
    check(PyLong_AsLong(v) == (99999L * 7919) % 100000, "getAt random");
    Py_DECREF(v);
}

static void b_getat_seq(void) {
    PyObject* v = NULL;
    for (long i = 0; i < 100000; ++i) { Py_XDECREF(v); v = PyList_GetItemRef(BIG, i); }
    check(PyLong_AsLong(v) == 99999, "getAt sequential");
    Py_DECREF(v);
}

// PyUnicode_Append is what the interpreter's `s += "x"` ends in: it resizes in
// place while the string has a single reference.
static void b_str_append(void) {
    PyObject* s = Py_NewRef(EMPTY);
    for (long i = 0; i < g_n; ++i) PyUnicode_Append(&s, X);
    check(PyUnicode_GET_LENGTH(s) == g_n, "string append size");
    Py_DECREF(s);
}

int main(void) {
    Py_Initialize();
    ONE = PyLong_FromLong(1); TWO = PyLong_FromLong(2); HALF = PyFloat_FromDouble(0.5);
    X = PyUnicode_FromString("x"); EMPTY = PyUnicode_FromString("");
    char nb[8];
    for (int i = 0; i < 16; ++i) { snprintf(nb, sizeof nb, "a%d", i); NM[i] = PyUnicode_InternFromString(nb); }
    PyObject* g = PyDict_New();
    PyDict_SetItemString(g, "__builtins__", PyEval_GetBuiltins());
    PyObject* r = PyRun_String("class O: pass\n", Py_file_input, g, g);
    check(r != NULL, "class O");
    Py_DECREF(r);
    CLS = PyDict_GetItemString(g, "O");

    measure("int_add", 200, 100000, b_int_add);
    measure("double_add", 200, 10000, b_double_add);
    g_calls = 0; b_fib(); long calls = g_calls;
    measure("fib20_step", 20, calls, b_fib);

    MOBJ = PyObject_CallNoArgs(CLS);
    for (int k = 0; k < 3; ++k) { PyObject* x = PyLong_FromLong(k); PyObject_SetAttr(MOBJ, NM[k], x); Py_DECREF(x); }
    measure("getattr", 200, 30000, b_getattr);
    measure("setattr_mutable", 200, 10000, b_setattr);
    measure("new_object_4_attrs", 200, 2000, b_new_object);

    long lives[3] = {1, 1000, 100000};
    for (int li = 0; li < 3; ++li) {
        long m = lives[li];
        PyObject* keep = PyList_New(0);
        for (long j = 0; j < m; ++j) {
            PyObject* x = PyObject_CallNoArgs(CLS); PyObject* v = PyLong_FromLong(j);
            PyObject_SetAttr(x, NM[0], v); PyList_Append(keep, x); Py_DECREF(v); Py_DECREF(x);
        }
        TARGET = PyObject_CallNoArgs(CLS);
        for (int k = 0; k < 16; ++k) { PyObject* x = PyLong_FromLong(k); PyObject_SetAttr(TARGET, NM[k], x); Py_DECREF(x); }
        char key[64]; snprintf(key, sizeof key, "mutable_write_16_attrs_%ld_live", m);
        measure(key, 100, 5000, b_mutable_write);
        Py_DECREF(TARGET); Py_DECREF(keep);
    }

    long sizes[3] = {1000, 10000, 100000};
    for (int si = 0; si < 3; ++si) {
        g_n = sizes[si];
        char key[64]; snprintf(key, sizeof key, "list_append_to_%ld", g_n);
        long reps = 2000000 / g_n; if (reps < 2) reps = 2;
        measure(key, reps, g_n, b_list_append);
    }
    BIG = PyList_New(0);
    for (long i = 0; i < 100000; ++i) { PyObject* x = PyLong_FromLong(i); PyList_Append(BIG, x); Py_DECREF(x); }
    measure("list_getat_random_100k", 20, 100000, b_getat_random);
    measure("list_getat_seq_100k", 20, 100000, b_getat_seq);

    long ssizes[2] = {2000, 10000};
    for (int si = 0; si < 2; ++si) {
        g_n = ssizes[si];
        char key[64]; snprintf(key, sizeof key, "str_append_to_%ld", g_n);
        long reps = 400000 / g_n; if (reps < 2) reps = 2;
        measure(key, reps, g_n, b_str_append);
    }
    r = PyRun_String("import sys\ngil = int(sys._is_gil_enabled()) if hasattr(sys, '_is_gil_enabled') else 1\n",
                     Py_file_input, g, g);
    check(r != NULL, "gil probe");
    Py_DECREF(r);
    printf("INFO gil_enabled=%ld version=%s\n", PyLong_AsLong(PyDict_GetItemString(g, "gil")), Py_GetVersion());
    Py_FinalizeEx();
    return 0;
}
