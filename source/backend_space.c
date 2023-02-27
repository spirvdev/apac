#include <stddef.h>
#include <stdio.h>

#include <backend_space.h>
#include <ocl_hardware.h>
#include <memctrlext.h>

#include <storage/fio.h>

static i32 back_load_ocl(apac_ctx_t* apac_ctx) {
	backend_ctx_t* core = apac_ctx->core_backend;
	core->ocl_shared = apmalloc(sizeof(storage_fio_t));
	opencl_int_t* interface = core->ocl_interface;
	interface->ocl_driver = NULL;

	if (core->ocl_shared == NULL) return -1;

	char ocl_pathname[0x100];

	// Attempting to load an "OpenCL shared object" local reference
	if (fio_open("libOpenCL.so", "ref", core->ocl_shared) == 0)   goto load_now;
	if (fio_open("libOpenCL.so.1", "ref", core->ocl_shared) == 0) goto load_now;

	if (fio_open("/usr/lib/x86_64-linux-gnu/libOpenCL.so.1", "ref", core->ocl_shared) == 0)
		goto load_now;

	#if defined(__ANDROID__)
	const char* ocl_system = "/system/vendor/lib64";
	#elif defined(__linux__)
	const char* ocl_system = "/usr/lib";
	#endif

	snprintf(ocl_pathname, sizeof ocl_pathname, "%s/libOpenCL.so", ocl_system);
	if (fio_open(ocl_pathname, "ref", core->ocl_shared) == 0) goto load_now;

	/* Can't found a valid OpenCL reference! */
	return -1;

	load_now: __attribute__((hot));
	const i32 ocl_ret = ocl_init(apac_ctx);
	if (ocl_ret == 0) 
		return 0;

	fio_finish(core->ocl_shared);
	apfree(core->ocl_shared);
	return ocl_ret;
}

static i32 back_unload_ocl(apac_ctx_t* apac_ctx) {
	backend_ctx_t* core = apac_ctx->core_backend;
	if (core->ocl_shared == NULL) 
		return -1;

	ocl_deinit(apac_ctx);
	fio_finish(core->ocl_shared);
	apfree(core->ocl_shared);

	return 0;
}

i32 back_init(apac_ctx_t* apac_ctx) {
	backend_ctx_t* core = apac_ctx->core_backend;
	core->ocl_interface = apmalloc(sizeof(*core->ocl_interface));
	if (core->ocl_interface == NULL)
		return -1;

	const i32 back_ret = back_load_ocl(apac_ctx);

	if (back_ret != 0)
		return back_ret;
		
	return 0;
}

i32 back_deinit(apac_ctx_t* apac_ctx) {
	backend_ctx_t* back = apac_ctx->core_backend;
	if (back == NULL) return -1;
	if (back->ocl_interface != NULL) {
		back_unload_ocl(apac_ctx);
		apfree(back->ocl_interface);
	}

	return 0;
}
