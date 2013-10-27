/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *   x86_64/clock.c
 *   Author: Naredula Janardhana Reddy  (naredula.jana@gmail.com, naredula.jana@yahoo.com)
 *
 */
#include "common.h"
#include "mach_dep.h"

#define KVM_CPUID_SIGNATURE 0x40000000
#define KVM_CPUID_FEATURES 0x40000001

#define MSR_KVM_WALL_CLOCK_NEW  0x4b564d00
#define MSR_KVM_SYSTEM_TIME_NEW 0x4b564d01
struct pvclock_vcpu_time_info {
	uint32_t version;
	uint32_t pad0;
	uint64_t tsc_timestamp;
	uint64_t system_time;
	uint32_t tsc_to_system_mul;
	signed char tsc_shift;
	unsigned char flags;
	unsigned char pad[2];
}__attribute__((__packed__));
/* 32 bytes */

struct pvclock_wall_clock {
	uint32_t version;
	uint32_t sec;
	uint32_t nsec;
}__attribute__((__packed__));

static struct pvclock_wall_clock wall_clock;
static struct pvclock_vcpu_time_info vpcu_time;
static int kvm_clock_available =0 ;
unsigned long get_kvm_clock() /* return 10ms units */
{
	if (kvm_clock_available == 0) return 0;
	msr_write(MSR_KVM_SYSTEM_TIME_NEW, __pa(&vpcu_time) | 1);
	return vpcu_time.system_time/10000000 ;
}
extern unsigned long g_jiffie_tick,g_jiffie_errors;
static inline int kvm_para_available(void) {

	uint32_t cpuid_ret[4], *p;
	char signature[13];


		do_cpuid(KVM_CPUID_SIGNATURE, &cpuid_ret[0]);

		p = &signature[0];
		*p = cpuid_ret[1];
		p = &signature[4];
		*p = cpuid_ret[2];
		p = &signature[8];
		*p = cpuid_ret[3];

		signature[12] = 0;

		if (ut_strcmp(signature, "KVMKVMKVM") != 0){
			ut_log("	FAILED : KVM Clock Signature :%s: \n", signature);
			return JFAIL;
		}

		do_cpuid(KVM_CPUID_FEATURES, &cpuid_ret[0]);

		msr_write(MSR_KVM_WALL_CLOCK_NEW, __pa(&wall_clock));

		msr_write(MSR_KVM_SYSTEM_TIME_NEW, __pa(&vpcu_time) | 1);
		g_jiffie_tick = get_kvm_clock();

		ut_log("	Succeded: KVM Clock Signature :%s: cpuid: %x \n", signature,cpuid_ret[0]);
		return JSUCCESS;

}
int ut_get_wallclock(unsigned long *sec, unsigned long *usec){
	unsigned long tmp_usec;

	if (kvm_clock_available == 1) {
		msr_write(MSR_KVM_WALL_CLOCK_NEW, __pa(&wall_clock));
		if (sec != 0)
			tmp_usec = vpcu_time.system_time / 1000; /* so many usec from start */
		*sec = wall_clock.sec + tmp_usec / 1000000;
		if (usec != 0)
			*usec = tmp_usec % 1000000;
	} else {
		if (sec != 0){
			*sec = g_jiffies / 100 ;
		}
		if (usec != 0){
			usec = (g_jiffies % 100) * 1000 ; /* actuall resoultion at the best is 10 ms*/
		}
	}
	return 1;
}
int Jcmd_clock() {
	unsigned long sec,usec,msec;

	msr_write(MSR_KVM_SYSTEM_TIME_NEW, __pa(&vpcu_time) | 1);
	ut_get_wallclock(&sec,&usec);
	msec=sec*1000+(usec/1000);
	ut_printf(" msec: %d sec:%d usec:%d\n",msec,sec,usec);

	ut_printf("system time  ts :%x system time :%x:%d  jiffies :%d sec version:%x errors:%d \n",
			vpcu_time.tsc_timestamp, vpcu_time.system_time,vpcu_time.system_time/1000000000, g_jiffies/100,vpcu_time.version);
	ut_printf(" jiifies :%d  clock:%d errors:%d \n",g_jiffie_tick,get_kvm_clock(),g_jiffie_errors);
	return 1;
}
int g_conf_kvmclock_enable =0 ;
int init_clock() {

	vpcu_time.system_time=0;
	if (kvm_para_available() == JFAIL){
		return 0;
	}
	if (g_conf_kvmclock_enable == 0) {
		ut_log(" Kvm clock is diabled by Jiny config\n");
		return 0;
	}
	kvm_clock_available = 1;
	return 0;
}
