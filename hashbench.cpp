#include <cstring>
#include <cmath>
#include <stdint.h>
#include <iostream>
#include <iomanip>
#ifdef HAVE_GETTIMEOFDAY
#include <sys/time.h>
#else
#include <windows.h>
#endif
#include "tables.h"

using namespace std;

// === Code for measuring speed
//
// Instead of producing a single number, we want to produce several data
// points. Then we'll plot them, and we'll be able to see noise, nonlinearity,
// and any other nonobvious weirdness.

// Run a Test of size n once. Return the elapsed time in seconds.
template <class Test>
double measure_single_run(size_t n)
{
    Test test;
    test.setup(n);

#ifdef HAVE_GETTIMEOFDAY
    struct timeval t0, t1;
    gettimeofday(&t0, NULL);
#else
    LARGE_INTEGER f, t0, t1;
    if (!QueryPerformanceFrequency(&f))
        abort();
    if (!QueryPerformanceCounter(&t0))
        abort();
#endif

    test.run(n);

#ifdef HAVE_GETTIMEOFDAY
    gettimeofday(&t1, NULL);
    return t1.tv_sec - t0.tv_sec + 1e-6 * (t1.tv_usec - t0.tv_usec);
#else
    if (!QueryPerformanceCounter(&t1))
        abort();
    return double(t1.QuadPart - t0.QuadPart) / double(f.QuadPart);
#endif
}

const double min_run_seconds = 0.1;
const double max_run_seconds = 1.0;

// Run several Tests of different sizes. Write results to stdout.
//
// We intentionally don't scale the test size exponentially, because hash
// tables can have nonlinear performance-falls-off-a-cliff points (table
// resizes) that occur at exponentially spaced intervals. We want to make sure
// we don't miss those.
//
template <class Test>
void run_time_trials()
{
    cout << "[\n";

    // Estimate how many iterations per second we can do.
    double estimated_speed;
    for (size_t n = 1; ; n *= 2) {
        double dt = measure_single_run<Test>(n);
        if (dt >= min_run_seconds) {
            estimated_speed = n / dt;
            break;
        }
    }

    // Now run trials of increasing size and print the results.
    double total = 0;
    const int trials = Test::trials();
    for (int i = 0; i < trials; i++) {
        double target_dt = min_run_seconds + double(i) / (trials - 1) * (max_run_seconds - min_run_seconds);
        size_t n = size_t(ceil(estimated_speed * target_dt));
        double dt = measure_single_run<Test>(n);
        cout << "\t\t[" << n << ", " << dt << (i < trials - 1 ? "]," : "]") << endl;
    }

    cout << "\t]";
}


// === Tests

struct GoodTest {
    static int trials() { return 10; }
};

struct SquirrelyTest {
    static int trials() { return 25; }
};

template <class Table>
struct InsertLargeTest : SquirrelyTest {
    Table table;
    void setup(size_t) {}
    void run(size_t n) {
        Key k = 1;
        for (size_t i = 0; i < n; i++) {
            table.set(k, k);
            k = k * 1103515245 + 12345;
        }
    }
};

// This test repeatedly builds a table of pseudorandom size (an exponential
// distribution with median size 100), then discards the table and starts over.
// It stops when it has done n total inserts.
//
// For a given n, the workload is deterministic.
//
// It would be simpler to repeatedly build tables of a particular
// size. However, all the implementations have particular sizes at which they
// rehash, an expensive operation that is *meant* to be amortized across all
// the other inserts. The benchmark should not reward implementations for
// having any particular rehashing threshold; so we build tables of a variety
// of sizes.
//
template <class Table>
struct InsertSmallTest : GoodTest {
    void setup(size_t) {}
    void run(size_t n) {
        Key k = 1;
        while (n) {
            Table table;
            do {
                table.set(k, k);
                k = k * 1103515245 + 12345;
            } while (--n && k % 145 != 0);
        }
    }
};

template <class Table>
struct LookupHitTest : GoodTest {
    enum { M = 8675309 + 1 }; // jenny's number, a prime, plus 1
    Table table;
    size_t errors;

    void setup(size_t n) {
        Key k = 1;
        for (size_t i = 0; i < n; i++) {
            table.set(k, k);
            k = k * 31 % M;
            if (k == 1)
                break;
        }
        errors = 0;
    }

    void run(size_t n) {
        Key k = 1;
        for (size_t i = 0; i < n; i++) {
            if (table.get(k) != k)
                abort();
            k = k * 31 % M;
        }
    }
};

template <class Table>
struct LookupMissTest : GoodTest {
    enum { M = 8675309 + 1 }; // jenny's number, a prime, plus 1
    Table table;
    size_t errors;

    void setup(size_t n) {
        Key k = 1;
        for (size_t i = 0; i < n; i++) {
            table.set(k, k);
            k = k * 31 % M;
            if (k == 1)
                break;
        }
        errors = 0;
    }

    void run(size_t n) {
        Key k = 1;
        for (size_t i = 0; i < n; i++) {
            if (table.get(k + M) != 0)
                abort();
            k = k * 31 % M;
        }
    }
};

// This test adds and removes entries from a table in FIFO order.
template <class Table>
struct WorklistTest : GoodTest {
    Table table;
    Key r, w;

    void setup(size_t) {
        r = 1;
        w = 1;
        for (int i = 0; i < 700; i++) {
            table.set(w, w);
            w = w * 1103515245 + 12345;
        }
    }

    void run(size_t n) {
        for (size_t i = 0; i < n; i++) {
            table.set(w, w);
            w = w * 1103515245 + 12345;

            if (!table.remove(r))
                abort();
            r = r * 1103515245 + 12345;
        }
    }
};

template <class Table>
struct DeleteTest : SquirrelyTest {
    Table table;

    void setup(size_t n) {
        while (n % 7 == 0 || n % 11 == 0)
            n++;

        Key k = 0;
        for (size_t i = 0; i < n; i++) {
            table.set(k + 1, 0);
            k = (k + 7) % n;
        }
    }

    void run(size_t n) {
        while (n % 7 == 0 || n % 11 == 0)
            n++;

        Key k = 0;
        for (size_t i = 0; i < n; i++) {
            if (!table.remove(k + 1))
                abort();
            k = (k + 11) % n;
        }
    }
};

template <class Table>
struct LookupAfterDeleteTest : GoodTest {
    Table table;

    enum { Size = 50000 };

    void setup(size_t n) {
        for (size_t i = 1; i <= Size; i++)
            table.set(i, i);
        for (size_t i = 1; i <= Size; i++) {
            if ((i & 0xff) != 0)
                table.remove(i);
        }
    }

    void run(size_t n) {
        for (size_t i = 1; i <= n; i++) {
            Key k = i % Size;
            if (table.get(k) != ((k & 0xff) == 0 ? k : Value()))
                abort();
        }
    }
};

// This test deletes and reinserts always the same value
template <class Table>
struct InsertAfterDeleteTest : GoodTest {
    Table table;

    void setup(size_t n) {
        Key k = 1;
        for (size_t i = 0; i < n; i++) {
            table.set(k, k);
            k = k + 1;
        }
    }

    void run(size_t n) {
        Key k = 1;
        for (size_t i = 0; i < n; i++) {
            table.remove(k);
            table.set(k, k);
        }
    }
};

template <template <class> class Test>
void run_speed_test()
{
    cout << '{' << endl;

#ifdef HAVE_SPARSEHASH
    cout << "\t\"DenseTable\": ";
    run_time_trials<Test<DenseTable> >();
    cout << ',' << endl;
#endif

    cout << "\t\"OpenTable\": ";
    run_time_trials<Test<OpenTable> >();
    cout << ',' << endl;

    cout << "\t\"CloseTable\": ";
    run_time_trials<Test<CloseTable> >();
    cout << endl;

    cout << "}";
}

void run_one_speed_test(const char *name)
{
    if (strcmp(name, "InsertLargeTest") == 0)
        run_speed_test<InsertLargeTest>();
    else if (strcmp(name, "InsertSmallTest") == 0)
        run_speed_test<InsertSmallTest>();
    else if (strcmp(name, "LookupHitTest") == 0)
        run_speed_test<LookupHitTest>();
    else if (strcmp(name, "LookupMissTest") == 0)
        run_speed_test<LookupMissTest>();
    else if (strcmp(name, "WorklistTest") == 0)
        run_speed_test<WorklistTest>();
    else if (strcmp(name, "DeleteTest") == 0)
        run_speed_test<DeleteTest>();
    else if (strcmp(name, "LookupAfterDeleteTest") == 0)
        run_speed_test<LookupAfterDeleteTest>();
    else if (strcmp(name, "InsertAfterDeleteTest") == 0)
        run_speed_test<InsertAfterDeleteTest>();
    else {
        cerr << "No such test: " << name << endl;
        return;
    }
    cout << endl;
}

void run_all_speed_tests()
{
    cout << "{" << endl;

    cout << "\"InsertLargeTest\": ";
    run_speed_test<InsertLargeTest>();
    cout << "," << endl;

    cout << "\"InsertSmallTest\": ";
    run_speed_test<InsertSmallTest>();
    cout << "," << endl;

    cout << "\"LookupHitTest\": ";
    run_speed_test<LookupHitTest>();
    cout << "," << endl;

    cout << "\"LookupMissTest\": ";
    run_speed_test<LookupMissTest>();
    cout << "," << endl;

    cout << "\"WorklistTest\": ";
    run_speed_test<WorklistTest>();
    cout << "," << endl;

    cout << "\"DeleteTest\": ";
    run_speed_test<DeleteTest>();
    cout << "," << endl;

    cout << "\"LookupAfterDeleteTest\": ";
    run_speed_test<LookupAfterDeleteTest>();

    cout << "\"InsertAfterDeleteTest\": ";
    run_speed_test<InsertAfterDeleteTest>();

    cout << "}" << endl;
}

void measure_space(ByteSizeOption opt)
{
#ifdef HAVE_SPARSEHASH
    DenseTable ht0;
#endif
    OpenTable ht1;
    CloseTable ht2;

    for (int i = 0; i < 100000; i++) {
        cout << i << '\t'
#ifdef HAVE_SPARSEHASH
             << ht0.byte_size(opt) << '\t'
#else
             << 1 << '\t'
#endif
             << ht1.byte_size(opt) << '\t' << ht2.byte_size(opt) << endl;

#ifdef HAVE_SPARSEHASH
        ht0.set(i + 1, i);
#endif
        ht1.set(i + 1, i);
        ht2.set(i + 1, i);
    }
}

int main(int argc, const char **argv) {
    if (argc == 2 && (strcmp(argv[1], "-m") == 0 || strcmp(argv[1], "-w") == 0)) {
        measure_space(argv[1][1] == 'm' ? BytesAllocated : BytesWritten);
    } else if (argc == 1) {
        //cout << measure_single_run<LookupHitTest<OpenTable> >(1000000) << endl;
        run_all_speed_tests();
    } else if (argc == 2) {
        run_one_speed_test(argv[1]);
    } else {
        cerr << "usage:\n  " << argv[0] << "\n  " << argv[0] << " -m\n  " << argv[0] << " -w\n";
        return 1;
    }

    return 0;
}
