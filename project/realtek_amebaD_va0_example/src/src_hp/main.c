
#include "ameba_soc.h"
#include "ftl_int.h"
#include "main.h"

#if defined(CONFIG_FTL_ENABLED)
extern const u8 ftl_phy_page_num;
extern const u32 ftl_phy_page_start_addr;

void app_ftl_init(void)
{
	ftl_init(ftl_phy_page_start_addr, ftl_phy_page_num);
}
#endif

#if defined(CONFIG_WIFI_NORMAL) && defined(CONFIG_NETWORK)
extern VOID wlan_network(VOID);
extern u32 GlobalDebugEnable;
#endif
void app_captouch_init(void);
void app_keyscan_init(u8 reset_status);

void app_init_debug(void)
{
	u32 debug[4];

	debug[LEVEL_ERROR] = BIT(MODULE_BOOT);
	debug[LEVEL_WARN]  = 0x0;
	debug[LEVEL_INFO]  = BIT(MODULE_BOOT);
	debug[LEVEL_TRACE] = 0x0;

	debug[LEVEL_ERROR] = 0xFFFFFFFF;

	#if 0
	/* enable debug SPI */
	debug[LEVEL_WARN]  |= BIT(MODULE_SPI);
	debug[LEVEL_INFO]  |= BIT(MODULE_SPI);
	debug[LEVEL_TRACE] |= BIT(MODULE_SPI);
	#endif

	LOG_MASK(LEVEL_ERROR, debug[LEVEL_ERROR]);
	LOG_MASK(LEVEL_WARN, debug[LEVEL_WARN]);
	LOG_MASK(LEVEL_INFO, debug[LEVEL_INFO]);
	LOG_MASK(LEVEL_TRACE, debug[LEVEL_TRACE]);
}

static void* app_mbedtls_calloc_func(size_t nelements, size_t elementSize)
{
	size_t size;
	void *ptr = NULL;

	size = nelements * elementSize;
	ptr = pvPortMalloc(size);

	if(ptr)
		_memset(ptr, 0, size);

	return ptr;
}

void app_mbedtls_rom_init(void)
{
	mbedtls_platform_set_calloc_free(app_mbedtls_calloc_func, vPortFree);
	//rom_ssl_ram_map.use_hw_crypto_func = 1;
	rtl_cryptoEngine_init();
}

VOID app_start_autoicg(VOID)
{
	u32 temp = 0;
	
	temp = HAL_READ32(SYSTEM_CTRL_BASE_HP, REG_HS_PLATFORM_PARA);
	temp |= BIT_HSYS_PLFM_AUTO_ICG_EN;
	HAL_WRITE32(SYSTEM_CTRL_BASE_HP, REG_HS_PLATFORM_PARA, temp);
}

VOID app_pmu_init(VOID)
{
	if (BKUP_Read(BKUP_REG0) & BIT_SW_SIM_RSVD){
		return;
	}

	pmu_set_sleep_type(SLEEP_PG);

	/* if wake from deepsleep, that means we have released wakelock last time */
	//if (SOCPS_DsleepWakeStatusGet() == TRUE) {
	//	pmu_set_sysactive_time(2);
	//	pmu_release_wakelock(PMU_OS);
	//	pmu_tickless_debug(ENABLE);
	//}	
}

/* enable or disable BT shared memory */
/* if enable, KM4 can access it as SRAM */
/* if disable, just BT can access it */
/* 0x100E_0000	0x100E_FFFF	64K */
VOID app_shared_btmem(u32 NewStatus)
{
	u32 temp = HAL_READ32(SYSTEM_CTRL_BASE_HP, REG_HS_PLATFORM_PARA);

	if (NewStatus == ENABLE) {
		temp |= BIT_HSYS_SHARE_BT_MEM;
	} else {
		temp &= ~BIT_HSYS_SHARE_BT_MEM;
	}	
	
	HAL_WRITE32(SYSTEM_CTRL_BASE_HP, REG_HS_PLATFORM_PARA, temp);
}

static void app_dslp_wake(void)
{
	u32 aon_wake_event = SOCPS_AONWakeReason();

	DBG_8195A("hs app_dslp_wake %x \n", aon_wake_event);

	if(BIT_GPIO_WAKE_STS & aon_wake_event) {
		DBG_8195A("DSLP AonWakepin wakeup, wakepin %x\n", SOCPS_WakePinCheck());
	}

	if(BIT_AON_WAKE_TIM0_STS & aon_wake_event) {
		SOCPS_AONTimerCmd(DISABLE);
		DBG_8195A("DSLP Aontimer wakeup \n");
	}

	if(BIT_RTC_WAKE_STS & aon_wake_event) {
		DBG_8195A("DSLP RTC wakeup \n");
	}

	if(BIT_DLPS_TSF_WAKE_STS & aon_wake_event) {
		DBG_8195A("DSLP TSF wakeup \n");
	}
	
	if(BIT_KEYSCAN_WAKE_STS & aon_wake_event) {
		DBG_8195A("DSLP KS wakeup\n");
	}

	if(BIT_CAPTOUCH_WAKE_STS & aon_wake_event) {
		DBG_8195A("DSLP Touch wakeup\n");
	}

	SOCPS_AONWakeClear(BIT_ALL_WAKE_STS);
}

//default main
int main(void)
{
	if (wifi_config.wifi_ultra_low_power &&
		wifi_config.wifi_app_ctrl_tdma == FALSE) {
		SystemSetCpuClk(CLK_KM4_100M);
	}	
	InterruptRegister(IPC_INTHandler, IPC_IRQ, (u32)IPCM0_DEV, 5);
	InterruptEn(IPC_IRQ, 5);

#ifdef CONFIG_MBED_TLS_ENABLED
	app_mbedtls_rom_init();
#endif
	//app_init_debug();

	/* init console */
	shell_recv_all_data_onetime = 1;
	shell_init_rom(0, 0);	
	shell_init_ram();
	ipc_table_init();

	/* Register Log Uart Callback function */
	InterruptRegister((IRQ_FUN) shell_uart_irq_rom, UART_LOG_IRQ, (u32)NULL, 5);
	InterruptEn(UART_LOG_IRQ,5);

	if(TRUE == SOCPS_DsleepWakeStatusGet()) {
		app_dslp_wake();
	}

#ifdef CONFIG_FTL_ENABLED
	app_ftl_init();
#endif

#if defined(CONFIG_WIFI_NORMAL) && defined(CONFIG_NETWORK)
	rtw_efuse_boot_write();

	/* pre-processor of application example */
	pre_example_entry();

	wlan_network();
	
	/* Execute application example */
	example_entry();
#endif

#if defined(CONFIG_EQC) && CONFIG_EQC
	//EQC_test_entry();
#endif
	app_start_autoicg();
	//app_shared_btmem(ENABLE);

	app_pmu_init();

	if ((BKUP_Read(0) & BIT_KEY_ENABLE))
		app_keyscan_init(FALSE); /* 5uA */
	if ((BKUP_Read(0) & BIT_CAPTOUCH_ENABLE))
		app_captouch_init(); /* 1uA */
	//if ((BKUP_Read(0) & BIT_GPIO_ENABLE))
	//	app_hp_jack_init(); 
	
	app_init_debug();

	//DBG_8195A("M4U:%d \n", RTIM_GetCount(TIMM05));
	/* Enable Schedule, Start Kernel */
	vTaskStartScheduler();
}

// #include "ameba_soc.h"
// #include "main.h"

// #if defined(CONFIG_WIFI_NORMAL) && defined(CONFIG_NETWORK)
// extern VOID wlan_network(VOID);
// #endif

// static u32 app_loguart_suspend(u32 expected_idle_time, void *param)
// {
// 	// For peripherals that need turned off before sleep, call disable or deinit peripheral here
// 	printf("app_loguart_suspend\n");

// 	return TRUE;
// }

// static u32 app_loguart_resume(u32 expected_idle_time, void *param)
// {
// 	// For peripherals that are turned off before sleep, call enable or init peripheral here
// 	printf("app_loguart_resume\n");
// 	/*set km4 active time*/
// 	pmu_set_sysactive_time(5000);
// 	return TRUE;
// }

// /**
//   * @brief  Main program.
//   * @param  None
//   * @retval None
//   */
// int main(void)
// {
// 	/* init console */
// 	shell_recv_all_data_onetime = 1;

// 	InterruptRegister(IPC_INTHandler, IPC_IRQ, (u32)IPCM0_DEV, 10);
// 	InterruptEn(IPC_IRQ, 10);

// 	shell_init_rom(0, 0);	
// 	shell_init_ram();
// 	ipc_table_init();

// 	/* Register Log Uart Callback function */
// 	InterruptRegister((IRQ_FUN) shell_uart_irq_rom, UART_LOG_IRQ, (u32)NULL, 10);
// 	InterruptEn(UART_LOG_IRQ,10);

// #if defined(CONFIG_WIFI_NORMAL) && defined(CONFIG_NETWORK)
// 	rtw_efuse_boot_write();

// 	wlan_network();
// #endif
// 	/*set KM4 sleep type*/
// 	pmu_set_sleep_type(SLEEP_PG);
// 	/*acquire wakelock to avoid KM4 enter sleep mode*/
// 	pmu_acquire_wakelock(PMU_OS);
	
// 	pmu_register_sleep_callback(PMU_LOGUART_DEVICE, (PSM_HOOK_FUN)app_loguart_suspend, NULL, (PSM_HOOK_FUN)app_loguart_resume, NULL);

// 	/*release wakelock to allow KM4 enter sleep mode*/
// 	pmu_release_wakelock(PMU_OS);
// 	/* Enable Schedule, Start Kernel */
// 	vTaskStartScheduler();
// }
