/* Molecular: the MVsilicon SDK's startup/init-default.c, changed only where marked "Molecular". */
/*
 * init-default.c
 *
 *  Created on: Mar 8, 2017
 *      Author: peter
 */

#include <nds32_intrinsic.h>
#include "debug.h"
#include "trace.h"		/* Molecular build 74: CPU_FOUND_* */

/* It will use Default_Handler if you don't have one */
//#pragma weak ExceptionCommHandler = Default_Handler

//#pragma weak SystickInterrup   =  Default_Handler
#pragma weak WakeupInterrupt   =  Default_Handler
#pragma weak GpioInterrupt     =  Default_Handler
#pragma weak RtcInterrupt      =  Default_Handler
#pragma weak SpdifInterrupt    =  Default_Handler
#pragma weak SWInterrupt       =  Default_Handler
#pragma weak I2C_Interrupt     =  Default_Handler
#pragma weak UART0_Interrupt   =  Default_Handler
#pragma weak Timer2Interrupt   =  Default_Handler
#pragma weak DMA0_Interrupt    =  Default_Handler
#pragma weak DMA1_Interrupt    =  Default_Handler
#pragma weak DMA2_Interrupt    =  Default_Handler
#pragma weak DMA3_Interrupt    =  Default_Handler
#pragma weak DMA4_Interrupt    =  Default_Handler
#pragma weak DMA5_Interrupt    =  Default_Handler
#pragma weak DMA6_Interrupt    =  Default_Handler
#pragma weak DMA7_Interrupt    =  Default_Handler
#pragma weak BT_Interrupt      =  Default_Handler
#pragma weak BLE_Interrupt      =  Default_Handler
#pragma weak I2sInterrupt      =  Default_Handler
#pragma weak Timer3Interrupt   =  Default_Handler
#pragma weak Timer4Interrupt   =  Default_Handler
#pragma weak Timer5Interrupt   =  Default_Handler
#pragma weak Timer6Interrupt   =  Default_Handler
#pragma weak SDIO0_Interrupt   =  Default_Handler
#pragma weak UsbInterrupt      =  Default_Handler
#pragma weak SPIM_Interrupt    =  Default_Handler
#pragma weak SPIS_Interrupt    =  Default_Handler
#pragma weak FFTInterrupt      =  Default_Handler
#pragma weak IRInterrupt      =  Default_Handler
#pragma weak OS_Trap_Interrupt_SWI =  Default_Handler

__attribute__((unused))
extern void hal_crashed(unsigned num) __attribute__((noreturn));
static void Default_Handler()
{
	hal_crashed(0xFFu);	/* Molecular: an unexpected interrupt: 50 s, then flash_boot (hal.c) */
}

__attribute__((unused))
void ExceptionCommHandler(unsigned stack, unsigned exception_num)
{
	hal_crashed(exception_num);	/* Molecular: a crash, recorded (hal.c): 50 s, then flash_boot */

	unsigned int mask_itype,mask_ipc;
	unsigned int *pstack;

	  pstack = (unsigned int *)stack;
	  DBG("Error exception happened\r\n");

	  mask_itype = __nds32__mfsr(NDS32_SR_ITYPE);
	  mask_ipc = __nds32__mfsr(NDS32_SR_IPC);
	  mask_itype &= 0x0F;

	  if(exception_num == 7)
	  {
		  DBG("Error Type:");
		  if(mask_itype == 0)
		  {
			  DBG("Alignment check (+I/D bit) or branch target alignment\r\n");
		  }
		  else if(mask_itype == 1)
		  {
			  DBG("Reserved instruction\r\n");
		  }
		  else if(mask_itype == 2)
		  {
			  DBG("Trap\r\n");
		  }
		  else if(mask_itype == 3)
		  {
			  DBG("Arithmetic\r\n");
		  }
		  else if(mask_itype == 4)
		  {
			  DBG("Precise bus error\r\n");
		  }
		  else if(mask_itype == 5)
		  {
			  DBG("Imprecise bus error\r\n");
		  }
		  else if(mask_itype == 6)
		  {
			  DBG("Coprocessor\r\n");
		  }
		  else if(mask_itype == 7)
		  {
			  DBG("Privileged instruction\r\n");
		  }
		  else if(mask_itype == 8)
		  {
			  DBG("Reserved value\r\n");
		  }
		  else if(mask_itype == 9)
		  {
			  DBG("Nonexistent memory address (+I/D bit)\r\n");
		  }
		  else if(mask_itype == 10)
		  {
			  DBG("MPZIU Control (+I/D bit)\r\n");
		  }
		  else if(mask_itype == 11)
		  {
			  DBG("Next precise stack overflow\r\n");
		  }
		  else
		  {
			  DBG("Unknown\r\n");
		  }
	  }
	  else if(exception_num == 1)
	  {

	  }
	  else if(exception_num == 2)
	  {

	  }
	  else if(exception_num == 3)
	  {

	  }
	  else if(exception_num == 4)
	  {

	  }
	  else if(exception_num == 5)
	  {

	  }
	  else if(exception_num == 6)
	  {

	  }
	  else if(exception_num == 8)
	  {

	  }
	  else
	  {

	  }

	  DBG("PC  = 0x%08x\r\n",mask_ipc);
	  DBG("R0  = 0x%08x\r\n",*pstack++);
	  DBG("R1  = 0x%08x\r\n",*pstack++);
	  DBG("R2  = 0x%08x\r\n",*pstack++);
	  DBG("R3  = 0x%08x\r\n",*pstack++);
	  DBG("R4  = 0x%08x\r\n",*pstack++);
	  DBG("R5  = 0x%08x\r\n",*pstack++);
	  DBG("R6  = 0x%08x\r\n",*pstack++);
	  DBG("R7  = 0x%08x\r\n",*pstack++);
	  DBG("R8  = 0x%08x\r\n",*pstack++);
	  DBG("R9  = 0x%08x\r\n",*pstack++);
	  DBG("R10 = 0x%08x\r\n",*pstack++);
	  DBG("R11 = 0x%08x\r\n",*pstack++);
	  DBG("R12 = 0x%08x\r\n",*pstack++);
	  DBG("R13 = 0x%08x\r\n",*pstack++);
	  DBG("R14 = 0x%08x\r\n",*pstack++);
	  DBG("R15 = 0x%08x\r\n",*pstack++);
	  DBG("R16 = 0x%08x\r\n",*pstack++);
	  DBG("R17 = 0x%08x\r\n",*pstack++);
	  DBG("R18 = 0x%08x\r\n",*pstack++);
	  DBG("R19 = 0x%08x\r\n",*pstack++);
	  DBG("R20 = 0x%08x\r\n",*pstack++);
	  DBG("lp  = 0x%08x\r\n",*pstack++);
	  DBG("sp  = 0x%08x\r\n",*pstack++);
	  DBG("exception num = %d\n", exception_num);
	  DBG("Error type num: %d\r\n", mask_itype);
	//DBG("Error exception happened\r\n");
	//NVIC_SystemReset();
	while(1) ;
}

void __c_init()
{

/* Use compiler builtin memcpy and memset */
#define MEMCPY(des, src, n) __builtin_memcpy ((des), (src), (n))
#define MEMSET(s, c, n) __builtin_memset ((s), (c), (n))

	extern char _end;
	extern char __bss_start;
	int size;

	/* data section will be copied before we remap.
	 * We don't need to copy data section here. */
	extern char __data_lmastart;
	extern char __data_start;
	extern char _edata;

	/* Copy data section to RAM */
	size = &_edata - &__data_start;
	MEMCPY(&__data_start, &__data_lmastart, size);

	/* Clear bss section */
	size = &_end - &__bss_start;
	MEMSET(&__bss_start, 0, size);
	return;
}

/* Molecular build 74: a byte copy the compiler makes a hardware loop; how many of 16 bytes arrived */
__attribute__((noinline)) static void zol_copy(unsigned char *d, const unsigned char *s, unsigned int n)
{
	unsigned int i;
	for (i = 0; i < n; i++)
		d[i] = s[i];
}
__attribute__((noinline)) static unsigned int zol_test(void)
{
	unsigned char a[16], b[16];
	unsigned int i, ok = 0;
	for (i = 0; i < 16; i++) {
		a[i] = (unsigned char)(i + 1);
		b[i] = 0;
	}
	zol_copy(b, a, 16);
	for (i = 0; i < 16; i++)
		ok += b[i] == a[i];
	return ok;
}

void __cpu_init()
{
	unsigned int tmp;

	/* turn on BTB */
//	tmp = 0x0;
//	__nds32__mtsr(tmp, NDS32_SR_MISC_CTL);

	tmp = __nds32__mfsr(NDS32_SR_MMU_CTL);
	tmp |= 0x800000;
	__nds32__mtsr(tmp,NDS32_SR_MMU_CTL);
	
	/* disable all hardware interrupts */
	__nds32__mtsr(0x0, NDS32_SR_INT_MASK);
#if (defined(__NDS32_ISA_V3M__) || defined(__NDS32_ISA_V3__))
	if (__nds32__mfsr(NDS32_SR_IVB) & 0x01)
		__nds32__mtsr(0x0, NDS32_SR_INT_MASK);
#endif

#if defined(CFG_EVIC)
	/* set EVIC, vector size: 4 bytes, base: 0x0 */
	__nds32__mtsr(0x1<<13, NDS32_SR_IVB);
#else
# if defined(USE_C_EXT)
	/* If we use v3/v3m toolchain and want to use
	 * C extension please use USE_C_EXT in CFLAGS
	 */
#ifdef __NDS32_ISA_V3__
	/* set IVIC, vector size: 4 bytes, base: 0x0 */
	__nds32__mtsr(0x0, NDS32_SR_IVB);
#else
	/* set IVIC, vector size: 16 bytes, base: 0x0 */
	__nds32__mtsr(0x1<<14, NDS32_SR_IVB);
#endif
# else
	/* set IVIC, vector size: 4 bytes, base: 0x0
	 * If we use v3/v3m toolchain and want to use
	 * assembly version please don't use USE_C_EXT
	 * in CFLAGS */
	/* Molecular: base 0x10000, our own vector table, as the stock firmware (0x526FC). The SDK's 0x0000 is right only
	 * with no bootloader: here 0x0 is flash_boot's table, and every interrupt went into flash_boot's code. */
	__nds32__mtsr(0x10000, NDS32_SR_IVB);
# endif
#endif
	/* Molecular build 74: the status word as found, and a hardware loop (zero-overhead loop) tried, both kept for the
	 * log past __c_init (free RAM above _end, which nothing clears): then PSW.AEN (bit 13, the audio extension, which
	 * the zero-overhead loops belong to) set. Nothing set it before: it was whatever flash_boot left, and after an
	 * install that was off: every loop the compiler made a hardware loop ran once (the screen's rows, the sound, the
	 * log's digits: all scrambled) */
	CPU_FOUND_PSW = __nds32__mfsr(NDS32_SR_PSW);
	CPU_FOUND_ZOL = zol_test();
	__nds32__mtsr_dsb(__nds32__mfsr(NDS32_SR_PSW) | (1u << 13), NDS32_SR_PSW);
	__nds32__isb();
	CPU_AEN_ZOL = zol_test();
	/* Set PSW INTL to 0 */
	tmp = __nds32__mfsr(NDS32_SR_PSW);
	tmp = tmp & 0xfffffff9;
#if (defined(__NDS32_ISA_V3M__) || defined(__NDS32_ISA_V3__))
	/* Set PSW CPL to 7 to allow any priority */
	tmp = tmp | 0x70008;
#endif
	__nds32__mtsr_dsb(tmp, NDS32_SR_PSW);
#if (defined(__NDS32_ISA_V3M__) || defined(__NDS32_ISA_V3__))
	/* Check interrupt priority programmable*
	* IVB.PROG_PRI_LVL
	*      0: Fixed priority       -- no exist ir18 1r19
	*      1: Programmable priority
	*/
	if (__nds32__mfsr(NDS32_SR_IVB) & 0x01) {
		/* Set PPL2FIX_EN to 0 to enable Programmable
	 	* Priority Level */
		__nds32__mtsr(0x0, NDS32_SR_INT_CTRL);
		/* Check IVIC numbers (IVB.NIVIC) */
		if ((__nds32__mfsr(NDS32_SR_IVB) & 0x0E)>>1 == 5) {	// 32IVIC
			/* set priority HW9: 0, HW13: 1, HW19: 2,
			* HW#-: 0 */
			__nds32__mtsr(~0x0, NDS32_SR_INT_PRI);
			__nds32__mtsr(~0x0, NDS32_SR_INT_PRI2);
		} else {
			/* set priority HW0: 0, HW1: 1, HW2: 2, HW3: 3
			* HW4-: 0 */
			__nds32__mtsr(0x000000e4, NDS32_SR_INT_PRI);
		}
	}
#endif
	/* enable FPU if the CPU support FPU */
#if defined(__NDS32_EXT_FPU_DP__) || defined(__NDS32_EXT_FPU_SP__)
	tmp = __nds32__mfsr(NDS32_SR_FUCOP_EXIST);
	if ((tmp & 0x80000001) == 0x80000001) {
		tmp = __nds32__mfsr(NDS32_SR_FUCOP_CTL);
		__nds32__mtsr_dsb((tmp | 0x1), NDS32_SR_FUCOP_CTL);

		/* Denormalized flush-to-Zero mode on. Molecular build 71: the whole register set, not or-ed into what the
		 * last program (flash_boot) left: round to nearest, no flags, every exception trap off (with one left on,
		 * an odd value in the physical models' set-up, core/pm.c, was a coprocessor exception: a crash) */
		tmp = (1 << 12);
		__nds32__fmtcsr(tmp);
		__nds32__dsb();
	}
#endif

	__nds32__mtsr(__nds32__mfsr(NDS32_SR_INT_PEND2), NDS32_SR_INT_PEND2);  //清除pending

	return;
}

#define HSP_CTL_offHSPEN        0
#define HSP_CTL_offSCHM         1
#define HSP_CTL_offSUSER        2
#define HSP_CTL_offUSER         3
#define HSP_ENABLE              (1 << HSP_CTL_offHSPEN)
#define HSP_DISABLE             (0 << HSP_CTL_offHSPEN)
#define HSP_SCHM_OVERFLOW       (0 << HSP_CTL_offSCHM)
#define HSP_SCHM_TOPRECORD      (1 << HSP_CTL_offSCHM)
#define HSP_SUPERUSER           (1 << HSP_CTL_offSUSER)
#define HSP_USER                (1 << HSP_CTL_offUSER)
void HardwareStackProtectEnable(void)
{
	if (!(__nds32__mfsr(NDS32_SR_MSC_CFG) & (1 << 27)))
	{
		//DBG("CPU does NOT support HW Stack protection/recording.\n");
	}
	else
	{
		__nds32__mtsr(__nds32__mfsr(NDS32_SR_HSP_CTL) & ~0x0f, NDS32_SR_HSP_CTL);

		__nds32__mtsr(0x20003000, NDS32_SR_SP_BOUND);

		__nds32__mtsr(HSP_ENABLE | HSP_SCHM_OVERFLOW | HSP_SUPERUSER, NDS32_SR_HSP_CTL);

		//DBG("CPU support HW Stack protection/recording.\n");
	}
}

void EnableIDCache(void);
void Chip_MemInit(void);
void __init()
{
/*----------------------------------------------------------
   !!  Users should NOT add any code before this comment  !!
------------------------------------------------------------*/
	__cpu_init();
	EnableIDCache(); //add by peter
	HardwareStackProtectEnable();
	Chip_MemInit();
	__c_init();     //copy data section, clean bss
}

__attribute__ ((section(".stub_section"),used)) __attribute__((naked))
void stub(void)
{
__asm__ __volatile__(

    		".long 0xFFFFFFFF \n\n"	//0xA4
    		".long 0xFFFFFFFF \n\n" //0xA8
    		".long 0xFFFFFFFF \n\n" //0xAC
    		".long 0x178000 \n\n"		//0xB0 constant data (Molecular: as the stock v1.3.3 image)
    		".long 0x1F0000 \n\n"		//0xB4 user data (as stock)
    		".byte 0xB1 \n\n"				//0xB8 (as stock: B1, SDK 0.0.1)
    		".byte 0 \n\n"					//0xB9
    		".byte 0 \n\n"					//0xBA
    		".byte 1 \n\n"					//0xBB
    		".long 0x00000001 \n\n"	//0xBC code crc (as stock)
    		".long 0xB0BEBDC9 \n\n"	//0xC0 magic number
    		".long 0x00000706 \n\n"	//0xC4 32KHz external oscillator input/output capacitance calibration value
    		".long 0xFFFFFFFF \n\n"	//0xC8 fast code crc	@ 0xA4
    		".long 0xFFFFFFFF \n\n"	//0xCC fast code crc	@ 0xA4
    		".long 0xFFFFFFFF \n\n"	//0xD0 fast code crc	@ 0xA4
    		".long 0xFFFFFFFF \n\n"	//0xD4 (Molecular: FFFFFFFF as stock, not the SDK's 0x03444846)
		    ".rept (0x100-0xD8)/4 \n\n"	/* Molecular: to 0x100, as the stock image (the SDK has 0xFC) */
    		".long 0xFFFFFFFF \n\n"
		    ".endr \n\n"
			".long 0x00FFFFFF \n\n"
		    ".short 0xFFFF \n\n"

    );
}

#include "core_d1088.h"
const uint32_t MPUConfigTable[8][7] =
{
	{MPU_ENTRY_ENABLE, 0,  		     0x1FFFFFFF,	0, 				CACHEABILITY_WRITE_BACK, 		EXECUTABLE_USER,	ACCESS_READ 	},
	{MPU_ENTRY_ENABLE, 0x20000000, 	 0x48000,		0x20000000,		CACHEABILITY_WRITE_THROUGH, 	EXECUTABLE_USER,	ACCESS_RW		},
	{MPU_ENTRY_ENABLE, 0x40000000, 	 0x1FFFFFFF,	0x40000000,		CACHEABILITY_DEVICE,			EXECUTABLE_USER,	ACCESS_RW		},
	{MPU_ENTRY_ENABLE, 0x60000000, 	 0x1FFFFFFF,	0x60000000,		CACHEABILITY_WRITE_THROUGH,		EXECUTABLE_USER,	ACCESS_RW		},
	{MPU_ENTRY_ENABLE, 0x80000000,	 0x8000,		0x20048000, 	CACHEABILITY_DEVICE,			EXECUTABLE_USER,	ACCESS_RW		},
	{MPU_ENTRY_DISABLE},
	{MPU_ENTRY_DISABLE},
	{MPU_ENTRY_DISABLE},
};
