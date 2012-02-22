#include <cstring>
#include <cmath>
#include <stdint.h>
#include <iostream>
#include <iomanip>
#include <sys/time.h>
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

    struct timeval t0, t1;
    gettimeofday(&t0, NULL);
    test.run(n);
    gettimeofday(&t1, NULL);

    return t1.tv_sec - t0.tv_sec + 1e-6 * (t1.tv_usec - t0.tv_usec);
}

const double min_run_seconds = 0.1;
const double max_run_seconds = 1.0;
const int trials = 10; // can't be 1

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
    for (int i = 0; i < trials; i++) {
        double target_dt = min_run_seconds + double(i) / (trials - 1) * (max_run_seconds - min_run_seconds);
        size_t n = size_t(ceil(estimated_speed * target_dt));
        double dt = measure_single_run<Test>(n);
        cout << "\t\t[" << n << ", " << dt << (i < trials - 1 ? "]," : "]") << endl;
    }
}


// === Tests

template <class Table>
struct InsertLargeTest {
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
struct InsertSmallTest {
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
struct LookupHitTest {
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
struct LookupMissTest {
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

template <template <class> class Test>
void run_test(const char *name)
{
    cout << '"' << name << "\": {" << endl;
    
    cout << "\t\"DenseTable\": [" << endl;
    run_time_trials<Test<DenseTable> >();
    cout << "\t]," << endl;

    cout << "\t\"OpenTable\": [" << endl;
    run_time_trials<Test<OpenTable> >();
    cout << "\t]," << endl;

    cout << "\t\"CloseTable\": [" << endl;
    run_time_trials<Test<CloseTable> >();
    cout << "\t]" << endl;

    cout << "}" << endl;
}

int main(int argc, const char **argv) {
    if (argc > 1 && (strcmp(argv[1], "-m") == 0 || strcmp(argv[1], "-w") == 0)) {
        ByteSizeOption opt = (argv[1][1] == 'm' ? BytesAllocated : BytesWritten);
        DenseTable ht0;
        OpenTable ht1;
        CloseTable ht2;

        for (int i = 0; i < 100000; i++) {
            cout << i << '\t' << ht0.byte_size(opt) << '\t' << ht1.byte_size(opt) << '\t' << ht2.byte_size(opt) << endl;
            ht0.set(i + 1, i);
            ht1.set(i + 1, i);
            ht2.set(i + 1, i);
        }
        return 0;
    }

    cout << "{" << endl;
    run_test<InsertLargeTest>("InsertLargeTest");
    cout << "," << endl;
    run_test<InsertSmallTest>("InsertSmallTest");
    cout << "," << endl;
    run_test<LookupHitTest>("LookupHitTest");
    cout << "," << endl;
    run_test<LookupHitTest>("LookupMissTest");
    cout << "}" << endl;

    return 0;
}
