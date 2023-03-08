#include <bits/time.h>
#include <sched.h>
#include <fcntl.h>
#include <unistd.h>

#include <stdio.h>
#include <string.h>

#include <pool/gov.h>
#include <time.h>

#if defined(__x86_64__)
#include <cpuid.h>

#elif defined(__aarch64__)
#include <sys/auxv.h>
#include <arm_neon.h>

#endif

u8 super_getcores() {
	cpu_set_t cores = {};
	CPU_ZERO(&cores);

	sched_getaffinity(0, sizeof cores, &cores);

#if defined(CPU_COUNT)
	return CPU_COUNT(&cores);
#endif

#define MAX_POSSIBLE_CORES 32

	u8 cpu_cores = 0, cpu_idx = 0;
	for (; cpu_idx < MAX_POSSIBLE_CORES; cpu_idx++) {
		if (CPU_ISSET(cpu_idx, &cores)) cpu_idx++;
		else break;
	}

	return cpu_cores;
}

i32 scalar_cpuinfo(char* cpu_vendor, char* cpu_name,
		   u64 vesz, u64 namesz, 
		   char* cpu_features, u64 featuresz,
		   u8* cores_count, u8* threads_count) {
#if defined(__x86_64__)
	u32 gpr[0xb];
	__cpuid(0, gpr[0], gpr[1], gpr[2], gpr[3]);

	if (!cpu_vendor || vesz < 12) goto fetchcpuname;

	*(i32*)(cpu_vendor + 0) = gpr[1];
	*(i32*)(cpu_vendor + 4) = gpr[3];
	*(i32*)(cpu_vendor + 8) = gpr[2];
	*(cpu_vendor + 12) = '\0';

#define CPU_ID_NAME_PART_1 0x80000002
#define CPU_ID_NAME_PART_2 0x80000003
#define CPU_ID_NAME_PART_3 0x80000004

fetchcpuname: __attribute__((cold));
	__cpuid(CPU_ID_NAME_PART_1, gpr[0], gpr[1], gpr[2], gpr[3]);
	__cpuid(CPU_ID_NAME_PART_2, gpr[4], gpr[5], gpr[6], gpr[7]);
	__cpuid(CPU_ID_NAME_PART_3, gpr[8], gpr[9], gpr[10], gpr[11]);
	
	if (!cpu_name || namesz < 49) goto setcount;
	strncpy(cpu_name, (const char*)gpr, namesz);

setcount:
	__cpuid(1, gpr[8], gpr[9], gpr[10], gpr[11]);
	if (gpr[10] & bit_SSE4_1) strncat(cpu_features, "SSE41, ", featuresz);
	if (gpr[10] & bit_SSE4_2) strncat(cpu_features, "SSE42, ", featuresz);
	if (gpr[10] & bit_AES)    strncat(cpu_features, "AES, ",   featuresz);

#define CPU_ID_CORES 0x80000008

	__cpuid(CPU_ID_CORES, gpr[8], gpr[9], gpr[10], gpr[11]);
	if (cores_count)   *cores_count = (gpr[10] & 0xf) - 1;
	if (threads_count) *threads_count = *cores_count * 2;

#elif defined(__aarch64__)
	u64 hw_cap_level0 = getauxval(AT_HWCAP);
	u64 hw_cap_level1 = getauxval(AT_HWCAP2);

	if (!cpu_features) goto vendorstr;

#define TEST_CAP_NEON  (1 << 20)
#define TEST_CAP_ASIMD (1 << 1 )
#define TEST_CAP_FP    (1 << 22)
#define TEST_CAP_CRC32 (1 << 1 )
#define TEST_CAP_AES   (1 << 3 )
#define TEST_CAP_SHA1  (1 << 4 )
#define TEST_CAP_SHA2  (1 << 5 )

	if (hw_cap_level0 & TEST_CAP_NEON)  strncat(cpu_features, "NEON, ", featuresz);
	if (hw_cap_level1 & TEST_CAP_ASIMD) strncat(cpu_features, "ASIMD, ", featuresz);
	if (hw_cap_level0 & TEST_CAP_FP)    strncat(cpu_features, "FP, ", featuresz);
	if (hw_cap_level0 & TEST_CAP_CRC32) strncat(cpu_features, "CRC32, ", featuresz);
	if (hw_cap_level0 & TEST_CAP_AES)   strncat(cpu_features, "AES, ", featuresz);
	if (hw_cap_level0 & TEST_CAP_SHA1)  strncat(cpu_features, "SHA1, ", featuresz);
	if (hw_cap_level0 & TEST_CAP_SHA2)  strncat(cpu_features, "SHA2, ", featuresz);

	u32 impl, variant, part, rev;
	__asm__("mrs %0, MIDR_EL1" : "=r" (impl));
	__asm__("mrs %0, CTR_EL0" : "=r" (variant));
	__asm__("mrs %0, MIDR_EL1" : "=r" (part));
	__asm__("mrs %0, REVIDR_EL1" : "=r" (rev));

vendorstr:
	snprintf(cpu_vendor, vesz, "%c%c%c", impl, impl >> 8, impl >> 16);
	if (!cpu_name) goto findcores;
	
	snprintf(cpu_name, namesz, "ARMv8.%d.%d", (variant >> 16) & 0xf, variant & 0xf);

findcores:

#endif
	if (!cpu_features) return 0;

	char* strend = strrchr(cpu_features, ',');
	*strend = '\0';
	
	return 0;
}

// Fetch the actual cpu core id frequency!
double scalar_frequency(u8 core_id) {
	if (!core_id)
		core_id = sched_getcpu();

	char scalar_freq[0x45], fb_buffer[0x45];
	snprintf(scalar_freq, sizeof scalar_freq, "/sys/devices/system/cpu/"
		"cpu%d/cpufreq/scaling_cur_freq", core_id);
	
	i32 scalar_fd = open(scalar_freq, O_RDONLY);
	if (scalar_fd < 0) return -1;

	read(scalar_fd, fb_buffer, sizeof fb_buffer);
	double fhz;
	sscanf(fb_buffer, "%lf", &fhz);

	close(scalar_fd);

	return fhz;
}

static inline void super_workload(u64 inter) {
	u64 summation = 0, inloop;
	for (inloop = 0; inloop < inter; inloop++) {
#if defined(__x86_64__)
		volatile __m128i vec_paralel = _mm_set_epi32(0, 0, 0, summation);
		vec_paralel = _mm_add_epi32(vec_paralel, _mm_set_epi32(0, 0, 0, 1));
		summation = _mm_extract_epi32(vec_paralel, 0);
#elif defined(__aarch64__)
		volatile int32x4_t vec_paralel = vdupq_n_s32(summation);
		vec_paralel = vaddq_s32(vec_paralel, vdupq_n_s32(1));
		summation += vgetq_lane_s32(vec_paralel, 0);
#endif
	}
}

double super_corefreq_rate(char* rate_format, u64 rate_sz) {
#define SEC_TO_NANO(sec) sec * 1e+9
#define INTER_TIMES 1e+8
	const u64 inter_times = INTER_TIMES;

	u64 start_time, end_time, elapsed_nsec = 0;

	typedef struct timespec native_clock_t;
	native_clock_t real_clock = {};
	clock_gettime(CLOCK_MONOTONIC, &real_clock);

	start_time = (u64)SEC_TO_NANO(real_clock.tv_sec) + real_clock.tv_nsec;
	super_workload(inter_times);

	// Calculating the elapsed time
	clock_gettime(CLOCK_MONOTONIC, &real_clock);
	end_time = (u64)SEC_TO_NANO(real_clock.tv_sec) + real_clock.tv_nsec;
	elapsed_nsec = end_time - start_time;

	const double clock_hz = inter_times * 10e9 / elapsed_nsec;

	if (!rate_format) return clock_hz;

	snprintf(rate_format, rate_sz, "%lf\n", clock_hz);

	return clock_hz;
}


