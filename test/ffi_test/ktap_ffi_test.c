#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>

void ffi_test_void(void)
{
}
EXPORT_SYMBOL(ffi_test_void);

int ffi_test_int1(unsigned char a, char b, unsigned short c, short d)
{
	return a + b + c + d;
}
EXPORT_SYMBOL(ffi_test_int1);

long long ffi_test_int2(unsigned int a, int b, unsigned long c, long d,
		unsigned long long e, long long f, long long g)
{
	return a + b + c + d + e + f + g;
}
EXPORT_SYMBOL(ffi_test_int2);

void *ffi_test_pointer1(char *a) {
	return a;
}
EXPORT_SYMBOL(ffi_test_pointer1);

long long ffi_test_var_arg(int n, ...) {
	va_list ap;
	int i;
	long long sum = 0;
	va_start(ap, n);
	for (i = 0; i < n; i++) {
		sum += va_arg(ap, long long);
	}
	va_end(ap);
	return sum;
}
EXPORT_SYMBOL(ffi_test_var_arg);

unsigned long long ffi_test_sched_clock(void)
{
	return sched_clock();
}
EXPORT_SYMBOL(ffi_test_sched_clock);

int ffi_test_array(int *arr, int idx)
{
	return arr[idx];
}
EXPORT_SYMBOL(ffi_test_array);

struct ffi_struct {
	int val;
};

int ffi_test_struct(struct ffi_struct *s)
{
	return s->val;
}
EXPORT_SYMBOL(ffi_test_struct);

struct ffi_struct_loop {
	int val;
	struct ffi_struct_loop *next;
};

int ffi_test_struct_loop(struct ffi_struct_loop *s)
{
	if (s == s->next)
		return s->val;
	return 0;
}
EXPORT_SYMBOL(ffi_test_struct_loop);

union ffi_union {
	int val;
	int val2;
	int val3;
};

int ffi_test_union(union ffi_union *s)
{
	return s->val;
}
EXPORT_SYMBOL(ffi_test_union);

struct ffi_struct_array {
	int val[20];
};

int ffi_test_struct_array(struct ffi_struct_array *s)
{
	int sum = 0, i;
	for (i = 0; i < 20; i++)
		sum += s->val[i];
	return sum;
}
EXPORT_SYMBOL(ffi_test_struct_array);

struct ffi_struct_noname {
	struct {
		int pad;
		int val;
	};
};

int ffi_test_struct_noname(struct ffi_struct_noname *s)
{
	return s->val;
}
EXPORT_SYMBOL(ffi_test_struct_noname);


static int __init ffi_test_init(void)
{
	return 0;
}

static void __exit ffi_test_exit(void)
{
}


MODULE_DESCRIPTION("ktap ffi test module");
MODULE_LICENSE("GPL");

module_init(ffi_test_init);
module_exit(ffi_test_exit);
