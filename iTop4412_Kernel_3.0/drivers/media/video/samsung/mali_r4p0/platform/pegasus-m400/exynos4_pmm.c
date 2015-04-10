/* drivers/gpu/mali400/mali/platform/pegasus-m400/exynos4_pmm.c
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC Mali400 DVFS driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

/**
 * @file exynos4_pmm.c
 * Platform specific Mali driver functions for the exynos 4XXX based platforms
 */


#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "exynos4_pmm.h"
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#if defined(CONFIG_MALI400_PROFILING)
#include "mali_osk_profiling.h"
#endif

#if defined(CONFIG_PM_RUNTIME)
#include <plat/pd.h>
#endif

#include <asm/io.h>
#include <mach/regs-pmu.h>

#include <linux/workqueue.h>

#define MALI_DVFS_STEPS 5
#define MALI_DVFS_WATING 10 /* msec */
#define MALI_DVFS_DEFAULT_STEP 1

#ifdef CONFIG_CPU_FREQ
#include <mach/asv.h>
#define EXYNOS4_ASV_ENABLED
#endif

#include <plat/cpu.h>

#define MALI_DVFS_CLK_DEBUG 0
#define SEC_THRESHOLD 1

#define CPUFREQ_LOCK_DURING_440 0

static int bMaliDvfsRun = 0;

typedef struct mali_dvfs_tableTag{
	unsigned int clock;
	unsigned int freq;
	unsigned int vol;
#if SEC_THRESHOLD
	unsigned int downthreshold;
	unsigned int upthreshold;
#endif
}mali_dvfs_table;

typedef struct mali_dvfs_statusTag{
	unsigned int currentStep;
	mali_dvfs_table * pCurrentDvfs;

} mali_dvfs_status_t;

/* dvfs status */
mali_dvfs_status_t maliDvfsStatus;
int mali_dvfs_control;

typedef struct mali_runtime_resumeTag{
	int clk;
	int vol;
	unsigned int step;
}mali_runtime_resume_table;

mali_runtime_resume_table mali_runtime_resume = {266, 900000, 1};

/* dvfs table */
mali_dvfs_table mali_dvfs[MALI_DVFS_STEPS]={
#if defined(CONFIG_CPU_EXYNOS4212) || defined(CONFIG_CPU_EXYNOS4412)
			/* step 0 */{160  ,1000000	,875000	, 0   , 70},
			/* step 1 */{266  ,1000000	,900000	,62   , 90},
			/* step 2 */{350  ,1000000	,950000	,85   , 90},
			/* step 3 */{440  ,1000000	,1025000   ,85   , 90},
			/* step 4 */{533  ,1000000	,1075000   ,85   ,100} };
#else
			/* step 0 */{134  ,1000000	, 950000   ,85   , 90},
			/* step 1 */{267  ,1000000	,1050000   ,85   ,100} };
#endif

#ifdef EXYNOS4_ASV_ENABLED
#define ASV_LEVEL	 12	/* ASV0, 1, 11 is reserved */
#define ASV_LEVEL_PRIME	 13  /* ASV0, 1, 12 is reserved */

static unsigned int asv_3d_volt_9_table_1ghz_type[MALI_DVFS_STEPS-1][ASV_LEVEL] = {
	{  975000,  950000,  950000,  950000,  925000,  925000,  925000,  900000,  900000,  900000,  900000,  875000},  /* L3(160Mhz) */
#if (MALI_DVFS_STEPS > 1)
	{ 1000000,  975000,  975000,  975000,  950000,  950000,  950000,  900000,  900000,  900000,  900000,  875000},  /* L2(266Mhz) */
#if (MALI_DVFS_STEPS > 2)
	{ 1075000, 1050000, 1050000, 1050000, 1000000, 1000000, 1000000,  975000,  975000,  975000,  975000,  925000},  /* L1(350Mhz) */
#if (MALI_DVFS_STEPS > 3)
	{ 1125000, 1100000, 1100000, 1100000, 1075000, 1075000, 1075000, 1025000, 1025000, 1025000, 1025000,  975000},  /* L0(440Mhz) */
#endif
#endif
#endif
};
static unsigned int asv_3d_volt_9_table[MALI_DVFS_STEPS-1][ASV_LEVEL] = {
	{  950000,  925000,  900000,  900000,  875000,  875000,  875000,  875000,  850000,  850000,  850000,  850000},  /* L3(160Mhz) */
#if (MALI_DVFS_STEPS > 1)
	{  975000,  950000,  925000,  925000,  925000,  900000,  900000,  875000,  875000,  875000,  875000,  850000},  /* L2(266Mhz) */
#if (MALI_DVFS_STEPS > 2)
	{ 1050000, 1025000, 1000000, 1000000,  975000,  950000,  950000,  950000,  925000,  925000,  925000,  900000},  /* L1(350Mhz) */
#if (MALI_DVFS_STEPS > 3)
	{ 1100000, 1075000, 1050000, 1050000, 1050000, 1025000, 1025000, 1000000, 1000000, 1000000,  975000,  950000},  /* L0(440Mhz) */
#endif
#endif
#endif
};

static unsigned int asv_3d_volt_9_table_for_prime[MALI_DVFS_STEPS][ASV_LEVEL_PRIME] = {
	{  962500,  937500,  925000,  912500,  900000,  887500,  875000,  862500,  875000,  862500,  850000,  850000,  850000},  /* L4(160Mhz) */
#if (MALI_DVFS_STEPS > 1)
	{  987500,  962500,  950000,  937500,  925000,  912500,  900000,  887500,  900000,  887500,  875000,  875000,  875000}, /* L3(266Mhz) */
#if (MALI_DVFS_STEPS > 2)
	{ 1037500, 1012500, 1000000,  987500,  975000,  962500,  950000,  937500,  950000,  937500,  912500,  900000,  887500}, /* L2(350Mhz) */
#if (MALI_DVFS_STEPS > 3)
	{ 1100000, 1075000, 1062500, 1050000, 1037500, 1025000, 1012500, 1000000, 1012500, 1000000,  975000,  962500,  950000}, /* L1(440Mhz) */
#if (MALI_DVFS_STEPS > 4)
	{ 1162500, 1137500, 1125000, 1112500, 1100000, 1087500, 1075000, 1062500, 1075000, 1062500, 1037500, 1025000, 1012500}, /* L0(533Mhz) */
#endif
#endif
#endif
#endif
};
#endif /* ASV_LEVEL */

#define EXTXTALCLK_NAME  "ext_xtal"
#define VPLLSRCCLK_NAME  "vpll_src"
#define FOUTVPLLCLK_NAME "fout_vpll"
#define SCLVPLLCLK_NAME  "sclk_vpll"
#define GPUMOUT1CLK_NAME "mout_g3d1"

#define MPLLCLK_NAME	 "mout_mpll"
#define GPUMOUT0CLK_NAME "mout_g3d0"
#define GPUCLK_NAME      "sclk_g3d"
#define CLK_DIV_STAT_G3D 0x1003C62C
#define CLK_DESC		 "clk-divider-status"

static struct clk *ext_xtal_clock = NULL;
static struct clk *vpll_src_clock = NULL;
static struct clk *fout_vpll_clock = NULL;
static struct clk *sclk_vpll_clock = NULL;

static struct clk *mpll_clock = NULL;
static struct clk *mali_parent_clock = NULL;
static struct clk *mali_clock		= NULL;

#if defined(CONFIG_CPU_EXYNOS4412) || defined(CONFIG_CPU_EXYNOS4212)
/* Pegasus */
static const mali_bool bis_vpll = MALI_TRUE;
int mali_gpu_clk = 440;
int mali_gpu_vol = 1025000;
#else
/* Orion */
static const mali_bool bis_vpll = MALI_FALSE;
int mali_gpu_clk = 267;
int mali_gpu_vol = 1050000;
#endif

static unsigned int GPU_MHZ	= 1000000;

int  gpu_power_state;
static int bPoweroff;
static atomic_t clk_active;

#ifdef CONFIG_REGULATOR
//struct regulator *g3d_regulator = NULL;
#endif

mali_io_address clk_register_map = 0;

/* DVFS */
static unsigned int mali_dvfs_utilization = 255;
static void mali_dvfs_work_handler(struct work_struct *w);
static struct workqueue_struct *mali_dvfs_wq = 0;
extern mali_io_address clk_register_map;
_mali_osk_mutex_t *mali_dvfs_lock;
int mali_runtime_resumed = -1;
static DECLARE_WORK(mali_dvfs_work, mali_dvfs_work_handler);

#ifdef CONFIG_REGULATOR
void mali_regulator_disable(void)
{
/*	if(IS_ERR_OR_NULL(g3d_regulator))
	{
		MALI_DEBUG_PRINT(1, ("error on mali_regulator_disable : g3d_regulator is null\n"));
		return;
	}
	regulator_disable(g3d_regulator);*/
}

void mali_regulator_enable(void)
{
/*	if(IS_ERR_OR_NULL(g3d_regulator))
	{
		MALI_DEBUG_PRINT(1, ("error on mali_regulator_enable : g3d_regulator is null\n"));
		return;
	}
	regulator_enable(g3d_regulator);*/
}

void mali_regulator_set_voltage(int min_uV, int max_uV)
{
	_mali_osk_mutex_wait(mali_dvfs_lock);
/*	if(IS_ERR_OR_NULL(g3d_regulator))
	{
		MALI_DEBUG_PRINT(1, ("error on mali_regulator_set_voltage : g3d_regulator is null\n"));
		return;
	}*/
	MALI_PRINT(("= regulator_set_voltage: %d, %d \n",min_uV, max_uV));
/*	regulator_set_voltage(g3d_regulator, min_uV, max_uV);
	mali_gpu_vol = regulator_get_voltage(g3d_regulator);
	MALI_DEBUG_PRINT(1, ("Mali voltage: %d\n", mali_gpu_vol));*/
	_mali_osk_mutex_signal(mali_dvfs_lock);
}
#endif

unsigned long mali_clk_get_rate(void)
{
	return clk_get_rate(mali_clock);
}


static unsigned int get_mali_dvfs_status(void)
{
	return maliDvfsStatus.currentStep;
}

mali_bool mali_clk_get(void)
{
	if (bis_vpll)
	{
		if (ext_xtal_clock == NULL)
		{
			ext_xtal_clock = clk_get(NULL, EXTXTALCLK_NAME);
			if (IS_ERR(ext_xtal_clock)) {
				MALI_PRINT(("MALI Error : failed to get source ext_xtal_clock\n"));
				return MALI_FALSE;
			}
		}

		if (vpll_src_clock == NULL)
		{
			vpll_src_clock = clk_get(NULL, VPLLSRCCLK_NAME);
			if (IS_ERR(vpll_src_clock)) {
				MALI_PRINT(("MALI Error : failed to get source vpll_src_clock\n"));
				return MALI_FALSE;
			}
		}

		if (fout_vpll_clock == NULL)
		{
			fout_vpll_clock = clk_get(NULL, FOUTVPLLCLK_NAME);
			if (IS_ERR(fout_vpll_clock)) {
				MALI_PRINT(("MALI Error : failed to get source fout_vpll_clock\n"));
				return MALI_FALSE;
			}
		}

		if (sclk_vpll_clock == NULL)
		{
			sclk_vpll_clock = clk_get(NULL, SCLVPLLCLK_NAME);
			if (IS_ERR(sclk_vpll_clock)) {
				MALI_PRINT(("MALI Error : failed to get source sclk_vpll_clock\n"));
				return MALI_FALSE;
			}
		}

		if (mali_parent_clock == NULL)
		{
			mali_parent_clock = clk_get(NULL, GPUMOUT1CLK_NAME);

			if (IS_ERR(mali_parent_clock)) {
				MALI_PRINT(( "MALI Error : failed to get source mali parent clock\n"));
				return MALI_FALSE;
			}
		}
	}
	else /* mpll */
	{
		if (mpll_clock == NULL)
		{
			mpll_clock = clk_get(NULL, MPLLCLK_NAME);

			if (IS_ERR(mpll_clock)) {
				MALI_PRINT(("MALI Error : failed to get source mpll clock\n"));
				return MALI_FALSE;
			}
		}

		if (mali_parent_clock == NULL)
		{
			mali_parent_clock = clk_get(NULL, GPUMOUT0CLK_NAME);

			if (IS_ERR(mali_parent_clock)) {
				MALI_PRINT(( "MALI Error : failed to get source mali parent clock\n"));
				return MALI_FALSE;
			}
		}
	}

	/* mali clock get always. */
	if (mali_clock == NULL)
	{
		mali_clock = clk_get(NULL, GPUCLK_NAME);

		if (IS_ERR(mali_clock)) {
			MALI_PRINT(("MALI Error : failed to get source mali clock\n"));
			return MALI_FALSE;
		}
	}

	return MALI_TRUE;
}

void mali_clk_put(mali_bool binc_mali_clock)
{
	if (mali_parent_clock)
	{
		clk_put(mali_parent_clock);
		mali_parent_clock = NULL;
	}

	if (mpll_clock)
	{
		clk_put(mpll_clock);
		mpll_clock = NULL;
	}

	if (sclk_vpll_clock)
	{
		clk_put(sclk_vpll_clock);
		sclk_vpll_clock = NULL;
	}

	if (binc_mali_clock && fout_vpll_clock)
	{
		clk_put(fout_vpll_clock);
		fout_vpll_clock = NULL;
	}

	if (vpll_src_clock)
	{
		clk_put(vpll_src_clock);
		vpll_src_clock = NULL;
	}

	if (ext_xtal_clock)
	{
		clk_put(ext_xtal_clock);
		ext_xtal_clock = NULL;
	}

	if (binc_mali_clock && mali_clock)
	{
		clk_put(mali_clock);
		mali_clock = NULL;
	}
}

void mali_clk_set_rate(unsigned int clk, unsigned int mhz)
{
	int err;
	unsigned long rate = (unsigned long)clk * (unsigned long)mhz;

	_mali_osk_mutex_wait(mali_dvfs_lock);
	MALI_DEBUG_PRINT(3, ("Mali platform: Setting frequency to %d mhz\n", clk));

	if (mali_clk_get() == MALI_FALSE)
		return;

	if (bis_vpll)
	{
		clk_set_rate(fout_vpll_clock, (unsigned int)clk * GPU_MHZ);
		clk_set_parent(vpll_src_clock, ext_xtal_clock);
		clk_set_parent(sclk_vpll_clock, fout_vpll_clock);

		clk_set_parent(mali_parent_clock, sclk_vpll_clock);
		clk_set_parent(mali_clock, mali_parent_clock);
	}
	else
	{
		clk_set_parent(mali_parent_clock, mpll_clock);
		clk_set_parent(mali_clock, mali_parent_clock);
	}

	if (!atomic_read(&clk_active)) {
		if (clk_enable(mali_clock) < 0)
			return;
		atomic_set(&clk_active, 1);
	}

	err = clk_set_rate(mali_clock, rate);
	if (err > 0)
		MALI_PRINT_ERROR(("Failed to set Mali clock: %d\n", err));

	rate = mali_clk_get_rate();

	MALI_DEBUG_PRINT(3, ("Mali frequency %d\n", rate / mhz));
	GPU_MHZ = mhz;
	mali_gpu_clk = (int)(rate / mhz);

	mali_clk_put(MALI_FALSE);

	_mali_osk_mutex_signal(mali_dvfs_lock);
}

int get_mali_dvfs_control_status(void)
{
	return mali_dvfs_control;
}

mali_bool set_mali_dvfs_current_step(unsigned int step)
{
	_mali_osk_mutex_wait(mali_dvfs_lock);
	maliDvfsStatus.currentStep = step % MALI_DVFS_STEPS;
	if (step >= MALI_DVFS_STEPS)
		mali_runtime_resumed = maliDvfsStatus.currentStep;

	_mali_osk_mutex_signal(mali_dvfs_lock);
	return MALI_TRUE;
}


static mali_bool set_mali_dvfs_status(u32 step,mali_bool boostup)
{
	u32 validatedStep=step;
#if MALI_DVFS_CLK_DEBUG
	unsigned int *pRegMaliClkDiv;
	unsigned int *pRegMaliMpll;
#endif

	if(boostup)	{
#ifdef CONFIG_REGULATOR
		/* change the voltage */
//		mali_regulator_set_voltage(mali_dvfs[step].vol, mali_dvfs[step].vol);
#endif
		/* change the clock */
		mali_clk_set_rate(mali_dvfs[step].clock, mali_dvfs[step].freq);
	} else {
		/* change the clock */
		mali_clk_set_rate(mali_dvfs[step].clock, mali_dvfs[step].freq);
#ifdef CONFIG_REGULATOR
		/* change the voltage */
//		mali_regulator_set_voltage(mali_dvfs[step].vol, mali_dvfs[step].vol);
#endif
	}

#if defined(CONFIG_MALI400_PROFILING)
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE|
			MALI_PROFILING_EVENT_CHANNEL_GPU|
			MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
			mali_gpu_clk, mali_gpu_vol/1000, 0, 0, 0);
#endif
	mali_clk_put(MALI_FALSE);

#if MALI_DVFS_CLK_DEBUG
	pRegMaliClkDiv = ioremap(0x1003c52c,32);
	pRegMaliMpll = ioremap(0x1003c22c,32);
	MALI_PRINT(("Mali MPLL reg:%d, CLK DIV: %d \n",*pRegMaliMpll, *pRegMaliClkDiv));
#endif

#ifdef EXYNOS4_ASV_ENABLED
	if (samsung_rev() < EXYNOS4412_REV_2_0) {
		if (mali_dvfs[step].clock == 160)
			exynos4x12_set_abb_member(ABB_G3D, ABB_MODE_100V);
		else
			exynos4x12_set_abb_member(ABB_G3D, ABB_MODE_130V);
	}
#endif

	set_mali_dvfs_current_step(validatedStep);
	/* for future use */
	maliDvfsStatus.pCurrentDvfs = &mali_dvfs[validatedStep];

#if CPUFREQ_LOCK_DURING_440
	/* lock/unlock CPU freq by Mali */
	if (mali_dvfs[step].clock == 440)
		err = cpufreq_lock_by_mali(1200);
	else
		cpufreq_unlock_by_mali();
#endif


	return MALI_TRUE;
}

static void mali_platform_wating(u32 msec)
{
	/*
	* sample wating
	* change this in the future with proper check routine.
	*/
	unsigned int read_val;
	while(1)
	{
		read_val = _mali_osk_mem_ioread32(clk_register_map, 0x00);
		if ((read_val & 0x8000)==0x0000) break;

		_mali_osk_time_ubusydelay(100); /* 1000 -> 100 : 20101218 */
	}
}

static mali_bool change_mali_dvfs_status(u32 step, mali_bool boostup )
{
	MALI_DEBUG_PRINT(4, ("> change_mali_dvfs_status: %d, %d \n",step, boostup));

	if(!set_mali_dvfs_status(step, boostup))
	{
		MALI_DEBUG_PRINT(1, ("error on set_mali_dvfs_status: %d, %d \n",step, boostup));
		return MALI_FALSE;
	}

	/* wait until clock and voltage is stablized */
	mali_platform_wating(MALI_DVFS_WATING); /* msec */

	return MALI_TRUE;
}

#ifdef EXYNOS4_ASV_ENABLED
extern unsigned int exynos_result_of_asv;

static mali_bool mali_dvfs_table_update(void)
{
	unsigned int i;
	unsigned int step_num = MALI_DVFS_STEPS;

	if(samsung_rev() < EXYNOS4412_REV_2_0)
		step_num = MALI_DVFS_STEPS - 1;

	if(soc_is_exynos4412()) {
		/*if (exynos_armclk_max == 1000000) {
			MALI_PRINT(("::C::exynos_result_of_asv : %d\n", exynos_result_of_asv));
			for (i = 0; i < step_num; i++) {
				mali_dvfs[i].vol = asv_3d_volt_9_table_1ghz_type[i][exynos_result_of_asv];
				MALI_PRINT(("mali_dvfs[%d].vol = %d \n", i, mali_dvfs[i].vol));
			}
		} else */
		/* modify by cym 20141204 */
#if 0
		if(((is_special_flag() >> G3D_LOCK_FLAG) & 0x1) && (samsung_rev() >= EXYNOS4412_REV_2_0)) {
#else
#ifdef CONFIG_CPU_TYPE_SCP
		if(((is_special_flag() >> G3D_LOCK_FLAG) & 0x1) && (samsung_rev() >= EXYNOS4412_REV_2_0)) {
#else
		if((samsung_rev() >= EXYNOS4412_REV_2_0)) {
#endif
#endif
		/* end modify */
			MALI_PRINT(("::L::exynos_result_of_asv : %d\n", exynos_result_of_asv));
			for (i = 0; i < step_num; i++) {
				mali_dvfs[i].vol = asv_3d_volt_9_table_for_prime[i][exynos_result_of_asv] + 25000;
				MALI_PRINT(("mali_dvfs[%d].vol = %d \n ", i, mali_dvfs[i].vol));
			}
		} else if (samsung_rev() >= EXYNOS4412_REV_2_0) {
			MALI_PRINT(("::P::exynos_result_of_asv : %d\n", exynos_result_of_asv));
			for (i = 0; i < step_num; i++) {
				mali_dvfs[i].vol = asv_3d_volt_9_table_for_prime[i][exynos_result_of_asv];
				MALI_PRINT(("mali_dvfs[%d].vol = %d \n", i, mali_dvfs[i].vol));
			}
		} else {
			MALI_PRINT(("::Q::exynos_result_of_asv : %d\n", exynos_result_of_asv));
			for (i = 0; i < step_num; i++) {
				mali_dvfs[i].vol = asv_3d_volt_9_table[i][exynos_result_of_asv];
				MALI_PRINT(("mali_dvfs[%d].vol = %d \n", i, mali_dvfs[i].vol));
			}
		}
	}

	return MALI_TRUE;
}
#endif


static unsigned int decideNextStatus(unsigned int utilization)
{
	static unsigned int level = 0;
	int iStepCount = 0;
	if (mali_runtime_resumed >= 0) {
		level = mali_runtime_resumed;
		mali_runtime_resumed = -1;
	}

	if (mali_dvfs_control == 0 && level == get_mali_dvfs_status()) {
		if (utilization > (int)(255 * mali_dvfs[maliDvfsStatus.currentStep].upthreshold / 100) &&
				level < MALI_DVFS_STEPS - 1) {
			level++;
			if ((samsung_rev() < EXYNOS4412_REV_2_0) && 3 == get_mali_dvfs_status()) {
				level=get_mali_dvfs_status();
			}
		}
		else if (utilization < (int)(255 * mali_dvfs[maliDvfsStatus.currentStep].downthreshold / 100) &&
				level > 0) {
			level--;
		}
	} else {
		for (iStepCount = MALI_DVFS_STEPS-1; iStepCount >= 0; iStepCount--) {
			if ( mali_dvfs_control >= mali_dvfs[iStepCount].clock ) {
				maliDvfsStatus.currentStep = iStepCount;
				level = iStepCount;
				break;
			}
		}
	}

	return level;
}

static void update_time_in_state(int level);
static mali_bool mali_dvfs_status(unsigned int utilization)
{
	unsigned int nextStatus = 0;
	unsigned int curStatus = 0;
	mali_bool boostup = MALI_FALSE;
#ifdef EXYNOS4_ASV_ENABLED
	static mali_bool asv_applied = MALI_FALSE;
#endif

#ifdef EXYNOS4_ASV_ENABLED
	if (asv_applied == MALI_FALSE) {
		mali_dvfs_table_update();
		change_mali_dvfs_status(1, 0);
		asv_applied = MALI_TRUE;

		return MALI_TRUE;
	}
#endif

	MALI_DEBUG_PRINT(4, ("> mali_dvfs_status: %d \n",utilization));

	/* decide next step */
	curStatus = get_mali_dvfs_status();
	nextStatus = decideNextStatus(utilization);

	MALI_DEBUG_PRINT(4, ("= curStatus %d, nextStatus %d, maliDvfsStatus.currentStep %d \n", curStatus, nextStatus, maliDvfsStatus.currentStep));
	/* if next status is same with current status, don't change anything */
	if(curStatus!=nextStatus)
	{
		/* check if boost up or not */
		if(nextStatus > maliDvfsStatus.currentStep) boostup = 1;

		update_time_in_state(curStatus);
		/* change mali dvfs status */
		if(!change_mali_dvfs_status(nextStatus,boostup))
		{
			MALI_DEBUG_PRINT(1, ("error on change_mali_dvfs_status \n"));
			return MALI_FALSE;
		}
	}
	return MALI_TRUE;
}


int mali_dvfs_is_running(void)
{
	return bMaliDvfsRun;
}


static void mali_dvfs_work_handler(struct work_struct *w)
{
	bMaliDvfsRun=1;

	MALI_DEBUG_PRINT(3, ("=== mali_dvfs_work_handler\n"));

	if(!mali_dvfs_status(mali_dvfs_utilization))
	MALI_DEBUG_PRINT(1, ( "error on mali dvfs status in mali_dvfs_work_handler"));

	bMaliDvfsRun=0;
}


mali_bool init_mali_dvfs_status(void)
{
	/*
	* default status
	* add here with the right function to get initilization value.
	*/

	if (!mali_dvfs_wq)
	{
		mali_dvfs_wq = create_singlethread_workqueue("mali_dvfs");
	}

	/* add a error handling here */
	maliDvfsStatus.currentStep = MALI_DVFS_DEFAULT_STEP;

	return MALI_TRUE;
}

void deinit_mali_dvfs_status(void)
{
	if (mali_dvfs_wq)
	{
		destroy_workqueue(mali_dvfs_wq);
		mali_dvfs_wq = NULL;
	}
}

mali_bool mali_dvfs_handler(unsigned int utilization)
{
	mali_dvfs_utilization = utilization;
	queue_work_on(0, mali_dvfs_wq, &mali_dvfs_work);

	return MALI_TRUE;
}

static mali_bool init_mali_clock(void)
{
	mali_bool ret = MALI_TRUE;
	gpu_power_state = 0;
	bPoweroff = 1;

	if (mali_clock != 0)
		return ret; /* already initialized */

	mali_dvfs_lock = _mali_osk_mutex_init(_MALI_OSK_LOCKFLAG_UNORDERED, 0);
	if (mali_dvfs_lock == NULL)
		return _MALI_OSK_ERR_FAULT;

	if (!mali_clk_get())
	{
		MALI_PRINT(("Error: Failed to get Mali clock\n"));
		goto err_clk;
	}

	mali_clk_set_rate((unsigned int)mali_gpu_clk, GPU_MHZ);

	MALI_PRINT(("init_mali_clock mali_clock %x\n", mali_clock));

#ifdef CONFIG_REGULATOR
/*	g3d_regulator = regulator_get(NULL, "vdd_g3d");

	if (IS_ERR(g3d_regulator))
	{
		MALI_PRINT(("MALI Error : failed to get vdd_g3d\n"));
		ret = MALI_FALSE;
		goto err_regulator;
	}

	regulator_enable(g3d_regulator);
	mali_regulator_set_voltage(mali_gpu_vol, mali_gpu_vol);*/
#endif

#if defined(CONFIG_MALI400_PROFILING)
	_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE|
			MALI_PROFILING_EVENT_CHANNEL_GPU|
			MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
			mali_gpu_clk, mali_gpu_vol/1000, 0, 0, 0);
#endif

	mali_clk_put(MALI_FALSE);

	return MALI_TRUE;

#ifdef CONFIG_REGULATOR
/*err_regulator:
	regulator_put(g3d_regulator);*/
#endif
err_clk:
	mali_clk_put(MALI_TRUE);

	return ret;
}

static mali_bool deinit_mali_clock(void)
{
	if (mali_clock == 0)
		return MALI_TRUE;

#ifdef CONFIG_REGULATOR
/*	if (g3d_regulator)
	{
		regulator_put(g3d_regulator);
		g3d_regulator = NULL;
	}*/
#endif

	mali_clk_put(MALI_TRUE);

	return MALI_TRUE;
}


static _mali_osk_errcode_t enable_mali_clocks(void)
{
	int err;

	if (!atomic_read(&clk_active)) {
		err = clk_enable(mali_clock);
		MALI_DEBUG_PRINT(3,("enable_mali_clocks mali_clock %p error %d \n", mali_clock, err));
		atomic_set(&clk_active, 1);
	}

	/* set clock rate */
#ifdef CONFIG_MALI_DVFS
	if (get_mali_dvfs_control_status() != 0 || mali_gpu_clk >= mali_runtime_resume.clk) {
		mali_clk_set_rate(mali_gpu_clk, GPU_MHZ);
	} else {
#ifdef CONFIG_REGULATOR
//		mali_regulator_set_voltage(mali_runtime_resume.vol, mali_runtime_resume.vol);
#endif
		mali_clk_set_rate(mali_runtime_resume.clk, GPU_MHZ);
		set_mali_dvfs_current_step(mali_runtime_resume.step);
	}
#else
	mali_clk_set_rate((unsigned int)mali_gpu_clk, GPU_MHZ);
	maliDvfsStatus.currentStep = MALI_DVFS_DEFAULT_STEP;
#endif

	MALI_SUCCESS;
}

static _mali_osk_errcode_t disable_mali_clocks(void)
{
	if (atomic_read(&clk_active)) {
		clk_disable(mali_clock);
		atomic_set(&clk_active, 0);
	}
	MALI_DEBUG_PRINT(3,("disable_mali_clocks mali_clock %p \n", mali_clock));
	MALI_SUCCESS;
}

/* Some defines changed names in later Odroid-A kernels. Make sure it works for both. */
#ifndef S5P_G3D_CONFIGURATION
#define S5P_G3D_CONFIGURATION S5P_PMU_G3D_CONF
#endif
#ifndef S5P_G3D_STATUS
#define S5P_G3D_STATUS S5P_PMU_G3D_CONF + 0x4
#endif

_mali_osk_errcode_t g3d_power_domain_control(int bpower_on)
{
	if (bpower_on)
	{
		void __iomem *status;
		u32 timeout;
		__raw_writel(S5P_INT_LOCAL_PWR_EN, S5P_G3D_CONFIGURATION);
		status = S5P_G3D_STATUS;

		timeout = 10;
		while ((__raw_readl(status) & S5P_INT_LOCAL_PWR_EN)
			!= S5P_INT_LOCAL_PWR_EN) {
			if (timeout == 0) {
				MALI_PRINTF(("Power domain  enable failed.\n"));
				return -ETIMEDOUT;
			}
			timeout--;
			_mali_osk_time_ubusydelay(100);
		}
	}
	else
	{
		void __iomem *status;
		u32 timeout;
		__raw_writel(0, S5P_G3D_CONFIGURATION);

		status = S5P_G3D_STATUS;
		/* Wait max 1ms */
		timeout = 10;
		while (__raw_readl(status) & S5P_INT_LOCAL_PWR_EN)
		{
			if (timeout == 0) {
				MALI_PRINTF(("Power domain  disable failed.\n" ));
				return -ETIMEDOUT;
			}
			timeout--;
			_mali_osk_time_ubusydelay(100);
		}
	}

	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_init(struct device *dev)
{
	MALI_CHECK(init_mali_clock(), _MALI_OSK_ERR_FAULT);

#ifdef CONFIG_MALI_DVFS
	if (!clk_register_map) clk_register_map = _mali_osk_mem_mapioregion( CLK_DIV_STAT_G3D, 0x20, CLK_DESC );
	if(!init_mali_dvfs_status())
		MALI_DEBUG_PRINT(1, ("mali_platform_init failed\n"));
#endif

	mali_platform_power_mode_change(dev, MALI_POWER_MODE_ON);

	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_deinit(struct device *dev)
{

	mali_platform_power_mode_change(dev, MALI_POWER_MODE_DEEP_SLEEP);
	deinit_mali_clock();

#ifdef CONFIG_MALI_DVFS
	deinit_mali_dvfs_status();
	if (clk_register_map )
	{
		_mali_osk_mem_unmapioregion(CLK_DIV_STAT_G3D, 0x20, clk_register_map);
		clk_register_map = NULL;
	}
#endif

	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_power_mode_change(struct device *dev, mali_power_mode power_mode)
{
	switch (power_mode)
	{
		case MALI_POWER_MODE_ON:
			MALI_DEBUG_PRINT(3, ("Mali platform: Got MALI_POWER_MODE_ON event, %s\n",
								 bPoweroff ? "powering on" : "already on"));
			if (bPoweroff == 1)
			{
#if !defined(CONFIG_PM_RUNTIME)
				g3d_power_domain_control(1);
#endif
				MALI_DEBUG_PRINT(4, ("enable clock \n"));
				enable_mali_clocks();
#if defined(CONFIG_MALI400_PROFILING)
				_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
						MALI_PROFILING_EVENT_CHANNEL_GPU |
						MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
						mali_gpu_clk, mali_gpu_vol/1000, 0, 0, 0);

#endif
				bPoweroff=0;
			}
			break;
		case MALI_POWER_MODE_LIGHT_SLEEP:
		case MALI_POWER_MODE_DEEP_SLEEP:
			MALI_DEBUG_PRINT(3, ("Mali platform: Got %s event, %s\n", power_mode ==
						MALI_POWER_MODE_LIGHT_SLEEP ?  "MALI_POWER_MODE_LIGHT_SLEEP" :
						"MALI_POWER_MODE_DEEP_SLEEP", bPoweroff ? "already off" : "powering off"));
			if (bPoweroff == 0)
			{
				disable_mali_clocks();
#if defined(CONFIG_MALI400_PROFILING)
				_mali_osk_profiling_add_event(MALI_PROFILING_EVENT_TYPE_SINGLE |
						MALI_PROFILING_EVENT_CHANNEL_GPU |
						MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE,
						0, 0, 0, 0, 0);
#endif

#if !defined(CONFIG_PM_RUNTIME)
				g3d_power_domain_control(0);
#endif
				bPoweroff=1;
			}

			break;
	}
	MALI_SUCCESS;
}

mali_gpu_utilization_handler(struct mali_gpu_utilization_data *data)
{
	if (bPoweroff==0)
	{
#ifdef CONFIG_MALI_DVFS
		if(data!=NULL && !mali_dvfs_handler(data->utilization_gpu))
			MALI_DEBUG_PRINT(1, ("error on mali dvfs status in utilization\n"));
#endif
	}
}

u64 mali_dvfs_time[MALI_DVFS_STEPS];
static void update_time_in_state(int level)
{
	u64 current_time;
	static u64 prev_time=0;

	if (prev_time ==0)
		prev_time=get_jiffies_64();

	current_time = get_jiffies_64();
	mali_dvfs_time[level] += current_time-prev_time;
	prev_time = current_time;
}

ssize_t show_time_in_state(struct device *dev, struct device_attribute *attr, char *buf)
{
	//struct device *kbdev;
	ssize_t ret = 0;
	int i;

	update_time_in_state(maliDvfsStatus.currentStep);

	for (i = 0; i < MALI_DVFS_STEPS; i++) {
		ret += snprintf(buf+ret, PAGE_SIZE-ret, "%d %llu\n",
				mali_dvfs[i].clock,
				mali_dvfs_time[i]);
	}

	if (ret < PAGE_SIZE - 1) {
		ret += snprintf(buf+ret, PAGE_SIZE-ret, "\n");
	} else {
		buf[PAGE_SIZE-2] = '\n';
		buf[PAGE_SIZE-1] = '\0';
		ret = PAGE_SIZE-1;
	}

	return ret;
}

ssize_t set_time_in_state(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int i;

	for (i = 0; i < MALI_DVFS_STEPS; i++) {
		mali_dvfs_time[i] = 0;
	}

	return count;
}