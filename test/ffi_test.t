# vi: ft= et tw=4 sw=4

our $SkipReason;

use lib 'test/lib';
use Test::ktap::features qw( has_ffi );

BEGIN {
    if (!has_ffi()) {
        $SkipReason = "missing ffi support in ktap";
    } else {
        `cd test/ffi_test && make --quiet --no-print-directory && \
         rmmod ktap_ffi_test > /dev/null 2>&1 || true && \
         insmod ktap_ffi_test.ko && cd - > /dev/null`
    }
}

END {
   if (has_ffi()) {
       `rmmod ktap_ffi_test > /dev/null 2>&1 && \
        cd test/ffi_test && make clean`
   }
}

use Test::ktap $SkipReason ? (skip_all => $SkipReason) : ('no_plan');

run_tests();

__DATA__

=== TEST 1: call void function
--- src
ffi.cdef[[
	void ffi_test_void();
]]
print(ffi.C.ffi_test_void() == nil)

--- out
true

--- err



=== TEST 2: call scalar type functions
--- src
ffi.cdef[[
	int ffi_test_int1(unsigned char a, char b, unsigned short c, short d);
	long long ffi_test_int2(unsigned int a, int b, unsigned long c, long d,
				unsigned long long e, long long f, long long g);
	void *ffi_test_pointer1(char *a);
]]
print(ffi.C.ffi_test_int1(1111, 1111, 1111, 1111) == 2396)

print(ffi.C.ffi_test_int2(90, 7800, 560000, 34000000, 1200000000,
			900000000000, 78000000000000) == 78901234567890)

print(ffi.C.ffi_test_pointer1("") != nil)


--- out
true
true
true

--- err



=== TEST 3: call vararg function
--- src
ffi.cdef[[
	long long ffi_test_var_arg(int n, ...);
]]
print(ffi.C.ffi_test_var_arg(7, 90, 7800, 560000, 34000000, 1200000000,
			900000000000, 78000000000000) == 78901234567890)
--- out
true

--- err



=== TEST 4: call kernel function wrapper
--- src
ffi.cdef[[
	unsigned long long ffi_test_sched_clock(void);
]]
print(ffi.C.ffi_test_sched_clock())

--- out_like
\d+\

--- err



=== TEST 5: new scalar type
--- src
newint = ffi.new("int")
newint = 1024
print(newint == 1024)

--- out
true

--- err



=== TEST 6: new record type
--- src
ffi.cdef[[
	struct ffi_struct {
		int val;
	};
	union ffi_union {
		int val;
		int val2;
		int val3;
	};
]]

struct = ffi.new("struct ffi_struct")
struct.val = 20
print(struct.val == 20)

union = ffi.new("union ffi_union")
union.val = 20
print(union.val == 20)

--- out
true
true

--- err



=== TEST 7: new array
--- src
ffi.cdef[[
	int ffi_test_array(int *arr, int idx);

	struct ffi_struct {
		int val;
	};
	int ffi_test_struct(struct ffi_struct *s);

	union ffi_union {
		int val;
		int val2;
		int val3;
	};
	int ffi_test_union(union ffi_union *s);

	struct ffi_struct_array {
		int val[20];
	};
	int ffi_test_struct_array(struct ffi_struct_array *s);
]]

arr = ffi.new("int[10]")
arr[9] = 10
arr[8] = arr[9]
print(ffi.C.ffi_test_array(arr, 8) == 10)

struct_arr = ffi.new("struct ffi_struct[1]")
struct_arr[0].val = 20
struct_arr[0].val = struct_arr[0].val
print(ffi.C.ffi_test_struct(struct_arr) == 20)

union_p = ffi.new("union ffi_union[1]")
union_p[0].val = 20
print(ffi.C.ffi_test_union(union_p) == 20)

union_p[0].val2 = 10
print(ffi.C.ffi_test_union(union_p) == 10)

struct_p3 = ffi.new("struct ffi_struct_array[1]")
struct_p3[0].val[4] = 20
struct_p3[0].val[9] = 40
struct_p3[0].val[14] = 60
struct_p3[0].val[19] = 80
print(ffi.C.ffi_test_struct_array(struct_p3) == 200)

--- out
true
true
true
true
true

--- err



=== TEST 8: struct loop
--- src
ffi.cdef [[
	struct ffi_struct_loop {
		int val;
		struct ffi_struct_loop *next;
	};
	int ffi_test_struct_loop(struct ffi_struct_loop *s);
]]
struct_arr2 = ffi.new("struct ffi_struct_loop[1]")
struct_arr2[0].val = 10
struct_arr2[0].next = struct_arr2
print(ffi.C.ffi_test_struct_loop(struct_arr2) == 10)

--- out
true

--- err



=== TEST 9: struct noname
--- src
ffi.cdef[[
	struct ffi_struct_noname {
		struct {
			int pad;
			int val;
		};
	};
	int ffi_test_struct_noname(struct ffi_struct_noname *s);
]]
struct_p4 = ffi.new("struct ffi_struct_noname[1]")
struct_p4[0].val = 20
print(ffi.C.ffi_test_struct_noname(struct_p4) == 20)

--- out
true

--- err
