/* Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"CHG: %s: " fmt, __func__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/spmi.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/power_supply.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/alarmtimer.h>
#include <linux/bitops.h>
#ifdef CONFIG_64BIT
#include <soc/qcom/lge/board_lge.h>
#else
#include <mach/board_lge.h>
#endif
#include <linux/wakelock.h>
#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
#ifdef CONFIG_64BIT
#include <soc/qcom/lge/lge_charging_scenario.h>
#else
#include <mach/lge_charging_scenario.h>
#endif
#endif
#ifdef CONFIG_LGE_PM_FACTORY_CABLE
#include <soc/qcom/smem.h>
#endif
#ifdef CONFIG_BATTERY_MAX17048
#include <linux/max17048_battery.h>
#endif

#ifdef CONFIG_LGE_PM_PWR_KEY_FOR_CHG_LOGO
#include <linux/input.h>
//#include <mach/board_lge.h>
#endif
#include <linux/leds.h>

#define CREATE_MASK(NUM_BITS, POS) \
	((unsigned char) (((1 << (NUM_BITS)) - 1) << (POS)))
#define LBC_MASK(MSB_BIT, LSB_BIT) \
	CREATE_MASK(MSB_BIT - LSB_BIT + 1, LSB_BIT)

/* Interrupt offsets */
#define INT_RT_STS_REG				0x10
#define FAST_CHG_ON_IRQ                         BIT(5)
#define OVERTEMP_ON_IRQ				BIT(4)
#define BAT_TEMP_OK_IRQ                         BIT(1)
#define BATT_PRES_IRQ                           BIT(0)

/* USB CHARGER PATH peripheral register offsets */
#define USB_IN_VALID_MASK			BIT(1)
#define USB_SUSP_REG				0x47
#define USB_SUSPEND_BIT				BIT(0)

/* CHARGER peripheral register offset */
#define CHG_OPTION_REG				0x08
#define CHG_OPTION_MASK				BIT(7)
#define CHG_STATUS_REG				0x09
#define CHG_VDD_LOOP_BIT			BIT(1)
#define CHG_VDD_MAX_REG				0x40
#define CHG_VDD_SAFE_REG			0x41
#define CHG_IBAT_MAX_REG			0x44
#define CHG_IBAT_SAFE_REG			0x45
#define CHG_VIN_MIN_REG				0x47
#define CHG_CTRL_REG				0x49
#define CHG_ENABLE				BIT(7)
#define CHG_FORCE_BATT_ON			BIT(0)
#define CHG_EN_MASK				(BIT(7) | BIT(0))
#define CHG_FAILED_REG				0x4A
#define CHG_FAILED_BIT				BIT(7)
#define CHG_VBAT_WEAK_REG			0x52
#define CHG_IBATTERM_EN_REG			0x5B
#define CHG_USB_ENUM_T_STOP_REG			0x4E
#define CHG_TCHG_MAX_EN_REG			0x60
#define CHG_TCHG_MAX_EN_BIT			BIT(7)
#define CHG_TCHG_MAX_MASK			LBC_MASK(6, 0)
#define CHG_TCHG_MAX_REG			0x61
#define CHG_WDOG_EN_REG				0x65
#define CHG_PERPH_RESET_CTRL3_REG		0xDA
#define CHG_COMP_OVR1				0xEE
#define CHG_VBAT_DET_OVR_MASK			LBC_MASK(1, 0)
#define OVERRIDE_0				0x2
#define OVERRIDE_NONE				0x0

/* BATTIF peripheral register offset */
#define BAT_IF_PRES_STATUS_REG			0x08
#define BATT_PRES_MASK				BIT(7)
#define BAT_IF_TEMP_STATUS_REG			0x09
#define BATT_TEMP_HOT_MASK			BIT(6)
#define BATT_TEMP_COLD_MASK			LBC_MASK(7, 6)
#define BATT_TEMP_OK_MASK			BIT(7)
#define BAT_IF_VREF_BAT_THM_CTRL_REG		0x4A
#define VREF_BATT_THERM_FORCE_ON		LBC_MASK(7, 6)
#define VREF_BAT_THM_ENABLED_FSM		BIT(7)
#define BAT_IF_BPD_CTRL_REG			0x48
#define BATT_BPD_CTRL_SEL_MASK			LBC_MASK(1, 0)
#define BATT_BPD_OFFMODE_EN			BIT(3)
#define BATT_THM_EN				BIT(1)
#define BATT_ID_EN				BIT(0)
#define BAT_IF_BTC_CTRL				0x49
#define BTC_COMP_EN_MASK			BIT(7)
#define BTC_COLD_MASK				BIT(1)
#define BTC_HOT_MASK				BIT(0)

/* MISC peripheral register offset */
#define MISC_REV2_REG				0x01
#define MISC_BOOT_DONE_REG			0x42
#define MISC_BOOT_DONE				BIT(7)
#define MISC_TRIM3_REG				0xF3
#define MISC_TRIM3_VDD_MASK			LBC_MASK(5, 4)
#define MISC_TRIM4_REG				0xF4
#define MISC_TRIM4_VDD_MASK			BIT(4)

#define PERP_SUBTYPE_REG			0x05
#define SEC_ACCESS                              0xD0

/* Linear peripheral subtype values */
#define LBC_CHGR_SUBTYPE			0x15
#define LBC_BAT_IF_SUBTYPE			0x16
#define LBC_USB_PTH_SUBTYPE			0x17
#define LBC_MISC_SUBTYPE			0x18

#define QPNP_CHG_I_MAX_MIN_90                   90

/* Feature flags */
#define VDD_TRIM_SUPPORTED			BIT(0)

#define QPNP_CHARGER_DEV_NAME	"qcom,qpnp-linear-charger"

#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO

#define MONITOR_BATTEMP_POLLING_PERIOD	(60 * HZ)
#define BATT_TEMP_OVERHEAT 57
#define BATT_TEMP_COLD (-10)
#endif

/* usb_interrupts */

struct qpnp_lbc_irq {
	int		irq;
	unsigned long	disabled;
	bool            is_wake;
};

enum {
	USBIN_VALID = 0,
	USB_OVER_TEMP,
	USB_CHG_GONE,
	BATT_PRES,
	BATT_TEMPOK,
	CHG_DONE,
	CHG_FAILED,
	CHG_FAST_CHG,
	CHG_VBAT_DET_LO,
	MAX_IRQS,
};

enum {
	USER	= BIT(0),
	THERMAL = BIT(1),
	CURRENT = BIT(2),
	SOC	= BIT(3),
#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
	LGE_CHARGING_TEMP_SCENARIO = BIT(4),
#endif
#ifdef CONFIG_LGE_PM
	CHG_FAIL_IRQ_HAPPEN = BIT(5),
#endif
#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE
	FACTORY_TESTMODE = BIT(6)
#endif

};

enum bpd_type {
	BPD_TYPE_BAT_ID,
	BPD_TYPE_BAT_THM,
	BPD_TYPE_BAT_THM_BAT_ID,
};

static const char * const bpd_label[] = {
	[BPD_TYPE_BAT_ID] = "bpd_id",
	[BPD_TYPE_BAT_THM] = "bpd_thm",
	[BPD_TYPE_BAT_THM_BAT_ID] = "bpd_thm_id",
};

enum btc_type {
	HOT_THD_25_PCT = 25,
	HOT_THD_35_PCT = 35,
	COLD_THD_70_PCT = 70,
	COLD_THD_80_PCT = 80,
};

static u8 btc_value[] = {
	[HOT_THD_25_PCT] = 0x0,
	[HOT_THD_35_PCT] = BIT(0),
	[COLD_THD_70_PCT] = 0x0,
	[COLD_THD_80_PCT] = BIT(1),
};

static inline int get_bpd(const char *name)
{
	int i = 0;
	for (i = 0; i < ARRAY_SIZE(bpd_label); i++) {
		if (strcmp(bpd_label[i], name) == 0)
			return i;
	}
	return -EINVAL;
}

#ifdef CONFIG_LGE_PM_AC_ONLINE
static char *pm_power_supplied_to[] = {
	"battery",
};
static enum power_supply_property pm_power_props_mains[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};
#endif

static enum power_supply_property msm_batt_power_props[] = {
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_COOL_TEMP,
	POWER_SUPPLY_PROP_WARM_TEMP,
#ifndef CONFIG_LGE_PM
	POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL,
#endif
#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
	POWER_SUPPLY_PROP_PSEUDO_BATT,
#endif
#ifdef CONFIG_LGE_PM
	POWER_SUPPLY_PROP_SAFETY_TIMER,
#endif
#ifdef CONFIG_LGE_PM_BATTERY_ID_CHECKER
	POWER_SUPPLY_PROP_BATTERY_ID_CHECKER,
	POWER_SUPPLY_PROP_VALID_BATT,
#endif
#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE
	POWER_SUPPLY_PROP_HW_REV,
#endif
#ifdef CONFIG_LGE_PM_BATTERY_RT9428_EOC_BY_SOC
	POWER_SUPPLY_PROP_STATUS_RAW,
#endif
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX
	POWER_SUPPLY_PROP_USB_CURRENT_MAX,
#endif
};

static char *pm_batt_supplied_to[] = {
#ifdef CONFIG_LGE_PM_BATTERY_EXTERNAL_FUELGAUGE
	"fuelgauge",
#else
	"bms",
#endif
};

struct vddtrim_map {
	int			trim_uv;
	int			trim_val;
};

/*
 * VDDTRIM is a 3 bit value which is split across two
 * register TRIM3(bit 5:4)	-> VDDTRIM bit(2:1)
 * register TRIM4(bit 4)	-> VDDTRIM bit(0)
 */
#define TRIM_CENTER			4
#define MAX_VDD_EA_TRIM_CFG		8
#define VDD_TRIM3_MASK			LBC_MASK(2, 1)
#define VDD_TRIM3_SHIFT			3
#define VDD_TRIM4_MASK			BIT(0)
#define VDD_TRIM4_SHIFT			4
#define AVG(VAL1, VAL2)			((VAL1 + VAL2) / 2)

/*
 * VDDTRIM table containing map of trim voltage and
 * corresponding trim value.
 */
struct vddtrim_map vddtrim_map[] = {
	{36700,		0x00},
	{28000,		0x01},
	{19800,		0x02},
	{10760,		0x03},
	{0,		0x04},
	{-8500,		0x05},
	{-16800,	0x06},
	{-25440,	0x07},
};

/*
 * struct qpnp_lbc_chip - device information
 * @dev:			device pointer to access the parent
 * @spmi:			spmi pointer to access spmi information
 * @chgr_base:			charger peripheral base address
 * @bat_if_base:		battery interface  peripheral base address
 * @usb_chgpth_base:		USB charge path peripheral base address
 * @misc_base:			misc peripheral base address
 * @bat_is_cool:		indicates that battery is cool
 * @bat_is_warm:		indicates that battery is warm
 * @chg_done:			indicates that charging is completed
 * @usb_present:		present status of USB
 * @batt_present:		present status of battery
 * @cfg_charging_disabled:	disable drawing current from USB.
 * @cfg_use_fake_battery:	flag to report default battery properties
 * @fastchg_on:			indicate charger in fast charge mode
 * @cfg_btc_disabled:		flag to disable btc (disables hot and cold
 *				irqs)
 * @cfg_max_voltage_mv:		the max volts the batt should be charged up to
 * @cfg_min_voltage_mv:		VIN_MIN configuration
 * @cfg_batt_weak_voltage_uv:	weak battery voltage threshold
 * @cfg_warm_bat_chg_ma:	warm battery maximum charge current in mA
 * @cfg_cool_bat_chg_ma:	cool battery maximum charge current in mA
 * @cfg_safe_voltage_mv:	safe voltage to which battery can charge
 * @cfg_warm_bat_mv:		warm temperature battery target voltage
 * @cfg_warm_bat_mv:		warm temperature battery target voltage
 * @cfg_cool_bat_mv:		cool temperature battery target voltage
 * @cfg_soc_resume_limit:	SOC at which battery resumes charging
 * @cfg_float_charge:		enable float charging
 * @charger_disabled:		maintain USB path state.
 * @cfg_charger_detect_eoc:	charger can detect end of charging
 * @cfg_disable_vbatdet_based_recharge:	keep VBATDET comparator overriden to
 *				low and VBATDET irq disabled.
 * @cfg_chgr_led_support:	support charger led work.
 * @cfg_safe_current:		battery safety current setting
 * @cfg_hot_batt_p:		hot battery threshold setting
 * @cfg_cold_batt_p:		eold battery threshold setting
 * @cfg_warm_bat_decidegc:	warm battery temperature in degree Celsius
 * @cfg_cool_bat_decidegc:	cool battery temperature in degree Celsius
 * @fake_battery_soc:		SOC value to be reported to userspace
 * @cfg_tchg_mins:		maximum allowed software initiated charge time
 * @chg_failed_count:		counter to maintained number of times charging
 *				failed
 * @cfg_bpd_detection:		battery present detection mechanism selection
 * @cfg_thermal_levels:		amount of thermal mitigation levels
 * @cfg_thermal_mitigation:	thermal mitigation level values
 * @therm_lvl_sel:		thermal mitigation level selection
 * @jeita_configure_lock:	lock to serialize jeita configuration request
 * @hw_access_lock:		lock to serialize access to charger registers
 * @ibat_change_lock:		lock to serialize ibat change requests from
 *				USB and thermal.
 * @irq_lock			lock to serialize enabling/disabling of irq
 * @supported_feature_flag	bitmask for all supported features
 * @vddtrim_alarm		alarm to schedule trim work at regular
 *				interval
 * @vddtrim_work		work to perform actual vddmax trimming
 * @init_trim_uv		initial trim voltage at bootup
 * @delta_vddmax_uv		current vddmax trim voltage
 * @chg_enable_lock:		lock to serialize charging enable/disable for
 *				SOC based resume charging
 * @usb_psy:			power supply to export information to
 *				userspace
 * @bms_psy:			power supply to export information to
 *				userspace
 * @batt_psy:			power supply to export information to
 *				userspace
 * @batt_temp:			Battery Temperature
 */
struct qpnp_lbc_chip {
	struct device			*dev;
	struct spmi_device		*spmi;
	u16				chgr_base;
	u16				bat_if_base;
	u16				usb_chgpth_base;
	u16				misc_base;
	bool				bat_is_cool;
	bool				bat_is_warm;
	bool				chg_done;
	bool				usb_present;
	bool				batt_present;
	bool				cfg_charging_disabled;
	bool				cfg_btc_disabled;
	bool				cfg_use_fake_battery;
	bool				fastchg_on;
	bool				cfg_use_external_charger;
	bool				cfg_chgr_led_support;
	unsigned int			cfg_warm_bat_chg_ma;
	unsigned int			cfg_cool_bat_chg_ma;
	unsigned int			cfg_safe_voltage_mv;
	unsigned int			cfg_max_voltage_mv;
	unsigned int			cfg_min_voltage_mv;
	unsigned int			cfg_charger_detect_eoc;
	unsigned int			cfg_disable_vbatdet_based_recharge;
	unsigned int			cfg_batt_weak_voltage_uv;
	unsigned int			cfg_warm_bat_mv;
	unsigned int			cfg_cool_bat_mv;
	unsigned int			cfg_hot_batt_p;
	unsigned int			cfg_cold_batt_p;
#ifndef CONFIG_LGE_PM
	unsigned int			cfg_thermal_levels;
	unsigned int			therm_lvl_sel;
	unsigned int			*thermal_mitigation;
#endif
	unsigned int			cfg_safe_current;
	unsigned int			cfg_tchg_mins;
	unsigned int			chg_failed_count;
	unsigned int			supported_feature_flag;
	int				cfg_bpd_detection;
	int				cfg_warm_bat_decidegc;
	int				cfg_cool_bat_decidegc;
	int				fake_battery_soc;
	int				cfg_soc_resume_limit;
	int				cfg_float_charge;
	int				charger_disabled;
	int				prev_max_ma;
	int				usb_psy_ma;
	int				delta_vddmax_uv;
	int				init_trim_uv;
	struct alarm			vddtrim_alarm;
	struct work_struct		vddtrim_work;
	struct qpnp_lbc_irq		irqs[MAX_IRQS];
	struct mutex			jeita_configure_lock;
	struct mutex			chg_enable_lock;
	spinlock_t			ibat_change_lock;
	spinlock_t			hw_access_lock;
	spinlock_t			irq_lock;
	struct power_supply		*usb_psy;
#ifdef CONFIG_LGE_PM_AC_ONLINE
	struct power_supply		ac_psy;
#endif
#ifdef CONFIG_LGE_PM_BATTERY_EXTERNAL_FUELGAUGE
	struct power_supply		*fuelgauge;
#else
	struct power_supply		*bms_psy;
#endif
	struct power_supply		batt_psy;
	struct qpnp_adc_tm_btm_param	adc_param;
	struct qpnp_vadc_chip		*vadc_dev;
	struct qpnp_adc_tm_chip		*adc_tm_dev;
#ifdef CONFIG_LGE_PM
	spinlock_t			chg_set_lock;
	bool chg_fail_irq_happen;
#endif
#ifdef CONFIG_LGE_PM_PWR_KEY_FOR_CHG_LOGO
	struct delayed_work     pwr_key_monitor_for_chg_logo;
#endif
#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
	struct delayed_work battemp_work;
	struct wake_lock lcs_wake_lock;
	struct wake_lock chg_wake_lock;
	enum lge_btm_states btm_state;
	int pseudo_ui_chg;
	int batt_temp;
	enum lge_charging_states battemp_chg_state;
	bool is_charger_changed_from_irq;
#ifdef CONFIG_LGE_THERMALE_CHG_CONTROL
	int chg_current_te;
	int chg_current_max;
	bool force_ichg_20pct;
#endif
#endif
#ifdef CONFIG_LGE_PM 
	struct wake_lock	uevent_wake_lock;
	struct wake_lock	chg_fail_irq_wake_lock;
#endif
#ifdef CONFIG_LGE_PM_AC_ONLINE
	unsigned int            ac_online;
	unsigned int            current_max;
#endif
	struct led_classdev		led_cdev;
};

#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX
unsigned int usb_current_max_enabled = 0;
#endif

#ifdef CONFIG_LGE_PM
	static struct qpnp_lbc_chip *qpnp_chg;
#endif

#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE
	bool	stpchg_factory_testmode;
	bool	start_chg_factory_testmode;
#endif


#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
extern struct pseudo_batt_info_type pseudo_batt_info;
#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY_CHARGING_MAX
#define PSEUDO_BATT_MAX 810
#else
#define PSEUDO_BATT_MAX 720
#endif
#endif

#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX
#define USB_CURRENT_MAX 810
#endif


#ifdef CONFIG_LGE_PM_AC_ONLINE

static int qpnp_power_get_property_mains(struct power_supply *psy,
                                       enum power_supply_property psp,
                                       union power_supply_propval *val)
{
        struct qpnp_lbc_chip *chip =
				container_of(psy, struct qpnp_lbc_chip, ac_psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 0;
#ifdef CONFIG_LGE_PM
#ifdef CONFIG_LGE_PM_VZW_FAST_CHG
		if (chg_state == VZW_NOT_CHARGING){
			val->intval = 1;
		}
		else
#endif
		val->intval = chip->ac_online;
#endif
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = chip->current_max;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int
qpnp_power_set_property_mains(struct power_supply *psy,
				  enum power_supply_property psp,
				  const union power_supply_propval *val)
{
        struct qpnp_lbc_chip *chip = container_of(psy, struct qpnp_lbc_chip,
					ac_psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		chip->ac_online = val->intval;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		chip->current_max = val->intval;
		break;
	default:
		return -EINVAL;
	}

	pr_debug("[LGE]  %s  online[%d]\n",
				__func__, chip->ac_online);
	power_supply_changed(&chip->ac_psy);
	return 0;
}

static int
qpnp_power_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	return 0;
}
#endif

static void qpnp_lbc_enable_irq(struct qpnp_lbc_chip *chip,
					struct qpnp_lbc_irq *irq)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->irq_lock, flags);
	if (__test_and_clear_bit(0, &irq->disabled)) {
		pr_debug("number = %d\n", irq->irq);
		enable_irq(irq->irq);
		if (irq->is_wake)
			enable_irq_wake(irq->irq);
	}
	spin_unlock_irqrestore(&chip->irq_lock, flags);
}

static void qpnp_lbc_disable_irq(struct qpnp_lbc_chip *chip,
					struct qpnp_lbc_irq *irq)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->irq_lock, flags);
	if (!__test_and_set_bit(0, &irq->disabled)) {
		pr_debug("number = %d\n", irq->irq);
		disable_irq_nosync(irq->irq);
		if (irq->is_wake)
			disable_irq_wake(irq->irq);
	}
	spin_unlock_irqrestore(&chip->irq_lock, flags);
}

static int __qpnp_lbc_read(struct spmi_device *spmi, u16 base,
			u8 *val, int count)
{
	int rc = 0;

	rc = spmi_ext_register_readl(spmi->ctrl, spmi->sid, base, val, count);
	if (rc)
		pr_err("SPMI read failed base=0x%02x sid=0x%02x rc=%d\n",
				base, spmi->sid, rc);

	return rc;
}

static int __qpnp_lbc_write(struct spmi_device *spmi, u16 base,
			u8 *val, int count)
{
	int rc;

	rc = spmi_ext_register_writel(spmi->ctrl, spmi->sid, base, val,
					count);
	if (rc)
		pr_err("SPMI write failed base=0x%02x sid=0x%02x rc=%d\n",
				base, spmi->sid, rc);

	return rc;
}

static int __qpnp_lbc_secure_write(struct spmi_device *spmi, u16 base,
				u16 offset, u8 *val, int count)
{
	int rc;
	u8 reg_val;

	reg_val = 0xA5;
	rc = __qpnp_lbc_write(spmi, base + SEC_ACCESS, &reg_val, 1);
	if (rc) {
		pr_err("SPMI read failed base=0x%02x sid=0x%02x rc=%d\n",
				base + SEC_ACCESS, spmi->sid, rc);
		return rc;
	}

	rc = __qpnp_lbc_write(spmi, base + offset, val, 1);
	if (rc)
		pr_err("SPMI write failed base=0x%02x sid=0x%02x rc=%d\n",
				base + SEC_ACCESS, spmi->sid, rc);

	return rc;
}

static int qpnp_lbc_read(struct qpnp_lbc_chip *chip, u16 base,
			u8 *val, int count)
{
	int rc = 0;
	struct spmi_device *spmi = chip->spmi;
	unsigned long flags;

	if (base == 0) {
		pr_err("base cannot be zero base=0x%02x sid=0x%02x rc=%d\n",
				base, spmi->sid, rc);
		return -EINVAL;
	}

	spin_lock_irqsave(&chip->hw_access_lock, flags);
	rc = __qpnp_lbc_read(spmi, base, val, count);
	spin_unlock_irqrestore(&chip->hw_access_lock, flags);

	return rc;
}

static int qpnp_lbc_write(struct qpnp_lbc_chip *chip, u16 base,
			u8 *val, int count)
{
	int rc = 0;
	struct spmi_device *spmi = chip->spmi;
	unsigned long flags;

	if (base == 0) {
		pr_err("base cannot be zero base=0x%02x sid=0x%02x rc=%d\n",
				base, spmi->sid, rc);
		return -EINVAL;
	}

	spin_lock_irqsave(&chip->hw_access_lock, flags);
	rc = __qpnp_lbc_write(spmi, base, val, count);
	spin_unlock_irqrestore(&chip->hw_access_lock, flags);

	return rc;
}

static int qpnp_lbc_masked_write(struct qpnp_lbc_chip *chip, u16 base,
				u8 mask, u8 val)
{
	int rc;
	u8 reg_val;
	struct spmi_device *spmi = chip->spmi;
	unsigned long flags;

	spin_lock_irqsave(&chip->hw_access_lock, flags);
	rc = __qpnp_lbc_read(spmi, base, &reg_val, 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n", base, rc);
		goto out;
	}
	pr_debug("addr = 0x%x read 0x%x\n", base, reg_val);

	reg_val &= ~mask;
	reg_val |= val & mask;

	pr_debug("writing to base=%x val=%x\n", base, reg_val);

	rc = __qpnp_lbc_write(spmi, base, &reg_val, 1);
	if (rc)
		pr_err("spmi write failed: addr=%03X, rc=%d\n", base, rc);

out:
	spin_unlock_irqrestore(&chip->hw_access_lock, flags);
	return rc;
}

static int __qpnp_lbc_secure_masked_write(struct spmi_device *spmi, u16 base,
				u16 offset, u8 mask, u8 val)
{
	int rc;
	u8 reg_val, reg_val1;

	rc = __qpnp_lbc_read(spmi, base + offset, &reg_val, 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n", base, rc);
		return rc;
	}
	pr_debug("addr = 0x%x read 0x%x\n", base, reg_val);

	reg_val &= ~mask;
	reg_val |= val & mask;
	pr_debug("writing to base=%x val=%x\n", base, reg_val);

	reg_val1 = 0xA5;
	rc = __qpnp_lbc_write(spmi, base + SEC_ACCESS, &reg_val1, 1);
	if (rc) {
		pr_err("SPMI read failed base=0x%02x sid=0x%02x rc=%d\n",
				base + SEC_ACCESS, spmi->sid, rc);
		return rc;
	}

	rc = __qpnp_lbc_write(spmi, base + offset, &reg_val, 1);
	if (rc) {
		pr_err("SPMI write failed base=0x%02x sid=0x%02x rc=%d\n",
				base + offset, spmi->sid, rc);
		return rc;
	}

	return rc;
}

static int qpnp_lbc_get_trim_voltage(u8 trim_reg)
{
	int i;

	for (i = 0; i < MAX_VDD_EA_TRIM_CFG; i++)
		if (trim_reg == vddtrim_map[i].trim_val)
			return vddtrim_map[i].trim_uv;

	pr_err("Invalid trim reg reg_val=%x\n", trim_reg);
	return -EINVAL;
}

static u8 qpnp_lbc_get_trim_val(struct qpnp_lbc_chip *chip)
{
	int i, sign;
	int delta_uv;

	sign = (chip->delta_vddmax_uv >= 0) ? -1 : 1;

	switch (sign) {
	case -1:
		for (i = TRIM_CENTER; i >= 0; i--) {
			if (vddtrim_map[i].trim_uv > chip->delta_vddmax_uv) {
				delta_uv = AVG(vddtrim_map[i].trim_uv,
						vddtrim_map[i + 1].trim_uv);
				if (chip->delta_vddmax_uv >= delta_uv)
					return vddtrim_map[i].trim_val;
				else
					return vddtrim_map[i + 1].trim_val;
			}
		}
		break;
	case 1:
		for (i = TRIM_CENTER; i <= 7; i++) {
			if (vddtrim_map[i].trim_uv < chip->delta_vddmax_uv) {
				delta_uv = AVG(vddtrim_map[i].trim_uv,
						vddtrim_map[i - 1].trim_uv);
				if (chip->delta_vddmax_uv >= delta_uv)
					return vddtrim_map[i - 1].trim_val;
				else
					return vddtrim_map[i].trim_val;
			}
		}
		break;
	}

	return vddtrim_map[i].trim_val;
}

static int qpnp_lbc_is_usb_chg_plugged_in(struct qpnp_lbc_chip *chip)
{
	u8 usbin_valid_rt_sts;
	int rc;

	rc = qpnp_lbc_read(chip, chip->usb_chgpth_base + INT_RT_STS_REG,
				&usbin_valid_rt_sts, 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				chip->usb_chgpth_base + INT_RT_STS_REG, rc);
		return rc;
	}

	pr_debug("rt_sts 0x%x\n", usbin_valid_rt_sts);

	return (usbin_valid_rt_sts & USB_IN_VALID_MASK) ? 1 : 0;
}

#ifdef CONFIG_LGE_PM_BATTERY_ID_CHECKER
bool external_qpnp_lbc_is_usb_chg_plugged_in(void)
{
	return qpnp_lbc_is_usb_chg_plugged_in(qpnp_chg);
}
#endif

#ifdef CONFIG_LGE_PM_FACTORY_CABLE
static bool is_factory_cable(void);
static int qpnp_lbc_is_batt_present(struct qpnp_lbc_chip *chip);
#endif
static int qpnp_lbc_charger_enable(struct qpnp_lbc_chip *chip, int reason,
					int enable)
{
#ifndef CONFIG_LGE_PM
	int disabled = chip->charger_disabled;
#else
	int disabled;
	int	chg_disabled;
#endif

#ifdef CONFIG_LGE_PM_FACTORY_CABLE
	int batt_present;
#endif
	u8 reg_val;
	int rc = 0;
#ifdef CONFIG_LGE_PM
	unsigned long flags;

	spin_lock_irqsave(&chip->chg_set_lock, flags);
	disabled = chip->charger_disabled;
	chg_disabled = chip->charger_disabled;
#endif
#ifdef CONFIG_LGE_PM_FACTORY_CABLE
	batt_present = qpnp_lbc_is_batt_present(chip);

	if (is_factory_cable() && !batt_present)
		enable = 1;
#endif

	pr_info("==== [qpnp_lbc_charger_enable] enable = %d, charger_disabled = %d, reason = %d =====\n",
		enable, chip->charger_disabled, reason);

	if (enable)
		disabled &= ~reason;
	else
		disabled |= reason;

#ifdef CONFIG_LGE_PM
	chip->charger_disabled = disabled;
	spin_unlock_irqrestore(&chip->chg_set_lock, flags);

	if (!!chg_disabled == !!disabled)
#else
	if (!!chip->charger_disabled == !!disabled)
#endif
		goto skip;

	reg_val = !!disabled ? CHG_FORCE_BATT_ON : CHG_ENABLE;

	rc = qpnp_lbc_masked_write(chip, chip->chgr_base + CHG_CTRL_REG,
				CHG_EN_MASK, reg_val);
	if (rc) {
		pr_err("Failed to %s charger rc=%d\n",
				reg_val ? "enable" : "disable", rc);
		return rc;
	}

skip:
#ifndef CONFIG_LGE_PM
	chip->charger_disabled = disabled;
#endif

	return rc;
}

static int qpnp_lbc_is_batt_present(struct qpnp_lbc_chip *chip)
{
	u8 batt_pres_rt_sts;
	int rc;

	rc = qpnp_lbc_read(chip, chip->bat_if_base + INT_RT_STS_REG,
				&batt_pres_rt_sts, 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				chip->bat_if_base + INT_RT_STS_REG, rc);
		return rc;
	}

	return (batt_pres_rt_sts & BATT_PRES_IRQ) ? 1 : 0;
}

static int qpnp_lbc_bat_if_configure_btc(struct qpnp_lbc_chip *chip)
{
	u8 btc_cfg = 0, mask = 0, rc;

	/* Do nothing if battery peripheral not present */
	if (!chip->bat_if_base)
		return 0;

	if ((chip->cfg_hot_batt_p == HOT_THD_25_PCT)
			|| (chip->cfg_hot_batt_p == HOT_THD_35_PCT)) {
		btc_cfg |= btc_value[chip->cfg_hot_batt_p];
		mask |= BTC_HOT_MASK;
	}

	if ((chip->cfg_cold_batt_p == COLD_THD_70_PCT) ||
			(chip->cfg_cold_batt_p == COLD_THD_80_PCT)) {
		btc_cfg |= btc_value[chip->cfg_cold_batt_p];
		mask |= BTC_COLD_MASK;
	}

	if (!chip->cfg_btc_disabled) {
		mask |= BTC_COMP_EN_MASK;
		btc_cfg |= BTC_COMP_EN_MASK;
	}

	pr_debug("BTC configuration mask=%x\n", btc_cfg);

	rc = qpnp_lbc_masked_write(chip,
			chip->bat_if_base + BAT_IF_BTC_CTRL,
			mask, btc_cfg);
	if (rc)
		pr_err("Failed to configure BTC rc=%d\n", rc);

	return rc;
}

#define QPNP_LBC_VBATWEAK_MIN_UV        3000000
#define QPNP_LBC_VBATWEAK_MAX_UV        3581250
#define QPNP_LBC_VBATWEAK_STEP_UV       18750
static int qpnp_lbc_vbatweak_set(struct qpnp_lbc_chip *chip, int voltage)
{
	u8 reg_val;
	int rc;

	if (voltage < QPNP_LBC_VBATWEAK_MIN_UV ||
			voltage > QPNP_LBC_VBATWEAK_MAX_UV) {
		rc = -EINVAL;
	} else {
		reg_val = (voltage - QPNP_LBC_VBATWEAK_MIN_UV) /
					QPNP_LBC_VBATWEAK_STEP_UV;
		pr_debug("VBAT_WEAK=%d setting %02x\n",
				chip->cfg_batt_weak_voltage_uv, reg_val);
		rc = qpnp_lbc_write(chip, chip->chgr_base + CHG_VBAT_WEAK_REG,
					&reg_val, 1);
		if (rc)
			pr_err("Failed to set VBAT_WEAK rc=%d\n", rc);
	}

	return rc;
}

#define QPNP_LBC_VBAT_MIN_MV		4000
#define QPNP_LBC_VBAT_MAX_MV		4775
#define QPNP_LBC_VBAT_STEP_MV		25
static int qpnp_lbc_vddsafe_set(struct qpnp_lbc_chip *chip, int voltage)
{
	u8 reg_val;
	int rc;

	if (voltage < QPNP_LBC_VBAT_MIN_MV
			|| voltage > QPNP_LBC_VBAT_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", voltage);
		return -EINVAL;
	}
	reg_val = (voltage - QPNP_LBC_VBAT_MIN_MV) / QPNP_LBC_VBAT_STEP_MV;
	pr_debug("voltage=%d setting %02x\n", voltage, reg_val);
	rc = qpnp_lbc_write(chip, chip->chgr_base + CHG_VDD_SAFE_REG,
				&reg_val, 1);
	if (rc)
		pr_err("Failed to set VDD_SAFE rc=%d\n", rc);

	return rc;
}

static int qpnp_lbc_vddmax_set(struct qpnp_lbc_chip *chip, int voltage)
{
	u8 reg_val;
	int rc, trim_val;
	unsigned long flags;

	if (voltage < QPNP_LBC_VBAT_MIN_MV
			|| voltage > QPNP_LBC_VBAT_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", voltage);
		return -EINVAL;
	}

	spin_lock_irqsave(&chip->hw_access_lock, flags);
	reg_val = (voltage - QPNP_LBC_VBAT_MIN_MV) / QPNP_LBC_VBAT_STEP_MV;
	pr_debug("voltage=%d setting %02x\n", voltage, reg_val);
	rc = __qpnp_lbc_write(chip->spmi, chip->chgr_base + CHG_VDD_MAX_REG,
				&reg_val, 1);
	if (rc) {
		pr_err("Failed to set VDD_MAX rc=%d\n", rc);
		goto out;
	}

	/* Update trim value */
	if (chip->supported_feature_flag & VDD_TRIM_SUPPORTED) {
		trim_val = qpnp_lbc_get_trim_val(chip);
		reg_val = (trim_val & VDD_TRIM3_MASK) << VDD_TRIM3_SHIFT;
		rc = __qpnp_lbc_secure_masked_write(chip->spmi,
				chip->misc_base, MISC_TRIM3_REG,
				MISC_TRIM3_VDD_MASK, reg_val);
		if (rc) {
			pr_err("Failed to set MISC_TRIM3_REG rc=%d\n", rc);
			goto out;
		}

		reg_val = (trim_val & VDD_TRIM4_MASK) << VDD_TRIM4_SHIFT;
		rc = __qpnp_lbc_secure_masked_write(chip->spmi,
				chip->misc_base, MISC_TRIM4_REG,
				MISC_TRIM4_VDD_MASK, reg_val);
		if (rc) {
			pr_err("Failed to set MISC_TRIM4_REG rc=%d\n", rc);
			goto out;
		}

		chip->delta_vddmax_uv = qpnp_lbc_get_trim_voltage(trim_val);
		if (chip->delta_vddmax_uv == -EINVAL) {
			pr_err("Invalid trim voltage=%d\n",
					chip->delta_vddmax_uv);
			rc = -EINVAL;
			goto out;
		}

		pr_debug("VDD_MAX delta=%d trim value=%x\n",
				chip->delta_vddmax_uv, trim_val);
	}

out:
	spin_unlock_irqrestore(&chip->hw_access_lock, flags);
	return rc;
}

static int qpnp_lbc_set_appropriate_vddmax(struct qpnp_lbc_chip *chip)
{
	int rc;

	if (chip->bat_is_cool)
		rc = qpnp_lbc_vddmax_set(chip, chip->cfg_cool_bat_mv);
	else if (chip->bat_is_warm)
		rc = qpnp_lbc_vddmax_set(chip, chip->cfg_warm_bat_mv);
	else
		rc = qpnp_lbc_vddmax_set(chip, chip->cfg_max_voltage_mv);
	if (rc)
		pr_err("Failed to set appropriate vddmax rc=%d\n", rc);

	return rc;
}

#define QPNP_LBC_MIN_DELTA_UV			13000
static void qpnp_lbc_adjust_vddmax(struct qpnp_lbc_chip *chip, int vbat_uv)
{
	int delta_uv, prev_delta_uv, rc;

	prev_delta_uv =  chip->delta_vddmax_uv;
	delta_uv = (int)(chip->cfg_max_voltage_mv * 1000) - vbat_uv;

	/*
	 * If delta_uv is positive, apply trim if delta_uv > 13mv
	 * If delta_uv is negative always apply trim.
	 */
	if (delta_uv > 0 && delta_uv < QPNP_LBC_MIN_DELTA_UV) {
		pr_debug("vbat is not low enough to increase vdd\n");
		return;
	}

	pr_debug("vbat=%d current delta_uv=%d prev delta_vddmax_uv=%d\n",
			vbat_uv, delta_uv, chip->delta_vddmax_uv);
	chip->delta_vddmax_uv = delta_uv + chip->delta_vddmax_uv;
	pr_debug("new delta_vddmax_uv  %d\n", chip->delta_vddmax_uv);
	rc = qpnp_lbc_set_appropriate_vddmax(chip);
	if (rc) {
		pr_err("Failed to set appropriate vddmax rc=%d\n", rc);
		chip->delta_vddmax_uv = prev_delta_uv;
	}
}

#define QPNP_LBC_VINMIN_MIN_MV		4200
#define QPNP_LBC_VINMIN_MAX_MV		5037
#define QPNP_LBC_VINMIN_STEP_MV		27
static int qpnp_lbc_vinmin_set(struct qpnp_lbc_chip *chip, int voltage)
{
	u8 reg_val;
	int rc;

	if ((voltage < QPNP_LBC_VINMIN_MIN_MV)
			|| (voltage > QPNP_LBC_VINMIN_MAX_MV)) {
		pr_err("bad mV=%d asked to set\n", voltage);
		return -EINVAL;
	}

	reg_val = (voltage - QPNP_LBC_VINMIN_MIN_MV) / QPNP_LBC_VINMIN_STEP_MV;
	pr_debug("VIN_MIN=%d setting %02x\n", voltage, reg_val);
	rc = qpnp_lbc_write(chip, chip->chgr_base + CHG_VIN_MIN_REG,
				&reg_val, 1);
	if (rc)
		pr_err("Failed to set VIN_MIN rc=%d\n", rc);

	return rc;
}

#ifdef CONFIG_LGE_PM_FACTORY_CABLE
#define LT_CABLE_56K		6
#define LT_CABLE_130K		7
#define LT_CABLE_910K		11

static unsigned int cable_type;

static bool is_factory_cable(void)
{
	unsigned int cable_info;
	cable_info = lge_pm_get_cable_type();

	if ((cable_info == CABLE_56K ||
		cable_info == CABLE_130K ||
		cable_info == CABLE_910K) ||
		(cable_type == LT_CABLE_56K ||
		cable_type == LT_CABLE_130K ||
		cable_type == LT_CABLE_910K))
		return true;
	else
		return false;
}

static bool is_factory_cable_130k(void)
{
	unsigned int cable_info;
	cable_info = lge_pm_get_cable_type();

	if (cable_info == CABLE_130K ||
		cable_type == LT_CABLE_130K)
		return true;
	else
		return false;
}
#endif

#define QPNP_LBC_IBATSAFE_MIN_MA	90
#define QPNP_LBC_IBATSAFE_MAX_MA	1440
#define QPNP_LBC_I_STEP_MA		90
static int qpnp_lbc_ibatsafe_set(struct qpnp_lbc_chip *chip, int safe_current)
{
	u8 reg_val;
	int rc;

	if (safe_current < QPNP_LBC_IBATSAFE_MIN_MA
			|| safe_current > QPNP_LBC_IBATSAFE_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", safe_current);
		return -EINVAL;
	}

	reg_val = (safe_current - QPNP_LBC_IBATSAFE_MIN_MA)
			/ QPNP_LBC_I_STEP_MA;
	pr_debug("Ibate_safe=%d setting %02x\n", safe_current, reg_val);

	rc = qpnp_lbc_write(chip, chip->chgr_base + CHG_IBAT_SAFE_REG,
				&reg_val, 1);
	if (rc)
		pr_err("Failed to set IBAT_SAFE rc=%d\n", rc);

	return rc;
}

#define QPNP_LBC_IBATMAX_MIN	90
#define QPNP_LBC_IBATMAX_MAX	1440
#ifdef CONFIG_LGE_PM_FACTORY_CABLE
#define INPUT_CURRENT_LIMIT_FACTORY 1500
#define INPUT_CURRENT_LIMIT_USB20   500
#define INPUT_CURRENT_LIMIT_USB30   900
#endif
/*
 * Set maximum current limit from charger
 * ibat =  System current + charging current
 */
static int qpnp_lbc_ibatmax_set(struct qpnp_lbc_chip *chip, int chg_current)
{
	u8 reg_val;
	int rc;

#ifdef CONFIG_LGE_PM_FACTORY_CABLE
	if (is_factory_cable()) {
		if (is_factory_cable_130k()) {
			pr_info("factory cable detected(130k) iBat 500mA\n");
#if defined (CONFIG_MACH_MSM8916_C70W_KR) || defined (CONFIG_MACH_MSM8916_YG_SKT_KR)
			chg_current = INPUT_CURRENT_LIMIT_USB30;
#else
			chg_current = INPUT_CURRENT_LIMIT_USB20;
#endif
		} else {
			pr_info("factory cable detected iBat 1500mA\n");
			chg_current = INPUT_CURRENT_LIMIT_FACTORY;
		}
	}
#endif

#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
		if((pseudo_batt_info.mode == 1)&&(!is_factory_cable())){
			pr_info("Battery Fake Mode set charging current to %d(mA)\n", PSEUDO_BATT_MAX);
			chg_current = PSEUDO_BATT_MAX;
		}
#endif

#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX
		if(usb_current_max_enabled){
			pr_info("USB Current Max set charging current to %d(mA)\n", PSEUDO_BATT_MAX);
			chg_current = USB_CURRENT_MAX;
		}
#endif

	if (chg_current > QPNP_LBC_IBATMAX_MAX)
		pr_debug("bad mA=%d clamping current\n", chg_current);

	chg_current = clamp(chg_current, QPNP_LBC_IBATMAX_MIN,
						QPNP_LBC_IBATMAX_MAX);
	reg_val = (chg_current - QPNP_LBC_IBATMAX_MIN) / QPNP_LBC_I_STEP_MA;

	rc = qpnp_lbc_write(chip, chip->chgr_base + CHG_IBAT_MAX_REG,
				&reg_val, 1);
	if (rc)
		pr_err("Failed to set IBAT_MAX rc=%d\n", rc);
	else
		chip->prev_max_ma = chg_current;

	pr_info("setting charging current %d(mA)\n", chg_current);
	return rc;
}

#define QPNP_LBC_TCHG_MIN	4
#define QPNP_LBC_TCHG_MAX	512
#define QPNP_LBC_TCHG_STEP	4
#ifdef CONFIG_LGE_PM
#define QPNP_CHG_TCHG_EN_MASK	BIT(7)
static bool is_safety_timer;
#endif
static int qpnp_lbc_tchg_max_set(struct qpnp_lbc_chip *chip, int minutes)
{
	u8 reg_val = 0;
	int rc;

	/* Disable timer */
	rc = qpnp_lbc_masked_write(chip, chip->chgr_base + CHG_TCHG_MAX_EN_REG,
						CHG_TCHG_MAX_EN_BIT, 0);
	if (rc) {
		pr_err("Failed to write tchg_max_en rc=%d\n", rc);
		return rc;
	}

	/* If minutes is 0, just disable timer */
	if (!minutes) {
		pr_debug("Charger safety timer disabled\n");
		return rc;
	}

	minutes = clamp(minutes, QPNP_LBC_TCHG_MIN, QPNP_LBC_TCHG_MAX);

	reg_val = (minutes / QPNP_LBC_TCHG_STEP) - 1;

	pr_debug("TCHG_MAX=%d mins setting %x\n", minutes, reg_val);
	rc = qpnp_lbc_masked_write(chip, chip->chgr_base + CHG_TCHG_MAX_REG,
						CHG_TCHG_MAX_MASK, reg_val);
	if (rc) {
		pr_err("Failed to write tchg_max_reg rc=%d\n", rc);
		return rc;
	}

	/* Enable timer */
#ifdef CONFIG_LGE_PM
	reg_val = QPNP_CHG_TCHG_EN_MASK;
#endif
    rc = qpnp_lbc_masked_write(chip, chip->chgr_base + CHG_TCHG_MAX_EN_REG,
                  CHG_TCHG_MAX_EN_BIT, CHG_TCHG_MAX_EN_BIT);
	if (rc) {
		pr_err("Failed to write tchg_max_en rc=%d\n", rc);
		return rc;
	}
#ifdef CONFIG_LGE_PM
	is_safety_timer = true;
#endif

	return rc;
}

#ifdef CONFIG_LGE_PM
static int set_safety_timer(struct qpnp_lbc_chip *chip, int value)
{
	int rc;

	is_safety_timer = value;

	if ( is_safety_timer ) {
		/* safety timer enable */
		rc = qpnp_lbc_masked_write(chip, chip->chgr_base + CHG_TCHG_MAX_EN_REG,
			CHG_TCHG_MAX_EN_BIT, CHG_TCHG_MAX_EN_BIT);

		if (rc) {
			pr_err("Failed to write tchg_max_en rc=%d\n", rc);
			return rc;
		}
	} else {
		/* safety timer disable */
		rc = qpnp_lbc_masked_write(chip, chip->chgr_base + CHG_TCHG_MAX_EN_REG,
			CHG_TCHG_MAX_EN_BIT, 0);

		if (rc) {
			pr_err("Failed to write tchg_max_en rc=%d\n", rc);
			return rc;
		}

	}

	return rc;
}
#endif

#define LBC_CHGR_LED	0x4D
#define CHGR_LED_ON	BIT(0)
#define CHGR_LED_OFF	0x0
#define CHGR_LED_STAT_MASK	LBC_MASK(1, 0)
static void qpnp_lbc_chgr_led_brightness_set(struct led_classdev *cdev,
		enum led_brightness value)
{
	struct qpnp_lbc_chip *chip = container_of(cdev, struct qpnp_lbc_chip,
			led_cdev);
	u8 reg;
	int rc;

	if (value > LED_FULL)
		value = LED_FULL;

	pr_debug("set the charger led brightness to value=%d\n", value);
	reg = (value > LED_OFF) ? CHGR_LED_ON : CHGR_LED_OFF;

	rc = qpnp_lbc_masked_write(chip, chip->chgr_base + LBC_CHGR_LED,
				CHGR_LED_STAT_MASK, reg);
	if (rc)
		pr_err("Failed to write charger led rc=%d\n", rc);
}

static enum
led_brightness qpnp_lbc_chgr_led_brightness_get(struct led_classdev *cdev)
{

	struct qpnp_lbc_chip *chip = container_of(cdev, struct qpnp_lbc_chip,
			led_cdev);
	u8 reg_val, chgr_led_sts;
	int rc;

	rc = qpnp_lbc_read(chip, chip->chgr_base + LBC_CHGR_LED,
				&reg_val, 1);
	if (rc) {
		pr_err("Failed to read charger led rc=%d\n", rc);
		return rc;
	}

	chgr_led_sts = reg_val & CHGR_LED_STAT_MASK;
	pr_debug("charger led brightness chgr_led_sts=%d\n", chgr_led_sts);

	return (chgr_led_sts == CHGR_LED_ON) ? LED_FULL : LED_OFF;
}

static int qpnp_lbc_register_chgr_led(struct qpnp_lbc_chip *chip)
{
	int rc;

	chip->led_cdev.name = "red";
	chip->led_cdev.brightness_set = qpnp_lbc_chgr_led_brightness_set;
	chip->led_cdev.brightness_get = qpnp_lbc_chgr_led_brightness_get;

	rc = led_classdev_register(chip->dev, &chip->led_cdev);
	if (rc)
		dev_err(chip->dev, "unable to register charger led, rc=%d\n",
				rc);

	return rc;
};

static int qpnp_lbc_vbatdet_override(struct qpnp_lbc_chip *chip, int ovr_val)
{
	int rc;
	u8 reg_val;
	struct spmi_device *spmi = chip->spmi;
	unsigned long flags;

	spin_lock_irqsave(&chip->hw_access_lock, flags);

	rc = __qpnp_lbc_read(spmi, chip->chgr_base + CHG_COMP_OVR1,
				&reg_val, 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
						chip->chgr_base, rc);
		goto out;
	}
	pr_debug("addr = 0x%x read 0x%x\n", chip->chgr_base, reg_val);

	reg_val &= ~CHG_VBAT_DET_OVR_MASK;
	reg_val |= ovr_val & CHG_VBAT_DET_OVR_MASK;

	pr_debug("writing to base=%x val=%x\n", chip->chgr_base, reg_val);

	rc = __qpnp_lbc_secure_write(spmi, chip->chgr_base, CHG_COMP_OVR1,
					&reg_val, 1);
	if (rc)
		pr_err("spmi write failed: addr=%03X, rc=%d\n",
						chip->chgr_base, rc);

out:
	spin_unlock_irqrestore(&chip->hw_access_lock, flags);
	return rc;
}
#ifdef CONFIG_LGE_PM_BATTERY_EXTERNAL_FUELGAUGE
#define DEFAULT_VOLTAGE		4000000
#endif
static int get_prop_battery_voltage_now(struct qpnp_lbc_chip *chip)
{
#ifdef CONFIG_BATTERY_MAX17048
	int voltage = 0;
	voltage = max17048_get_voltage() * 1000;
	return voltage;
#elif defined (CONFIG_LGE_PM_BATTERY_EXTERNAL_FUELGAUGE)
	int voltage = 0;
	union power_supply_propval ret = {0,};

	if (chip->fuelgauge == NULL)
		chip->fuelgauge = power_supply_get_by_name("fuelgauge");

	if (chip->fuelgauge != NULL) {
		chip->fuelgauge->get_property(chip->fuelgauge, POWER_SUPPLY_PROP_VOLTAGE_NOW, &ret);
		voltage = ret.intval * 1000;
	}

	if (voltage == 0)
		return DEFAULT_VOLTAGE;
	else
		return voltage;
#else
	int rc = 0;
	struct qpnp_vadc_result results;

	rc = qpnp_vadc_read(chip->vadc_dev, VBAT_SNS, &results);
	if (rc) {
		pr_err("Unable to read vbat rc=%d\n", rc);
		return 0;
	}

	return results.physical;
#endif
}

static int get_prop_batt_present(struct qpnp_lbc_chip *chip)
{
	u8 reg_val;
	int rc;

	rc = qpnp_lbc_read(chip, chip->bat_if_base + BAT_IF_PRES_STATUS_REG,
				&reg_val, 1);
	if (rc) {
		pr_err("Failed to read battery status read failed rc=%d\n",
				rc);
		return 0;
	}

	return (reg_val & BATT_PRES_MASK) ? 1 : 0;
}

static int get_prop_batt_health(struct qpnp_lbc_chip *chip)
{
#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
	if (chip->usb_present){
		if (chip->btm_state == BTM_HEALTH_OVERHEAT)
			return POWER_SUPPLY_HEALTH_OVERHEAT;
		if (chip->btm_state == BTM_HEALTH_COLD)
			return POWER_SUPPLY_HEALTH_COLD;
		else
			return POWER_SUPPLY_HEALTH_GOOD;
	}
	else{
		if (chip->batt_temp > BATT_TEMP_OVERHEAT)
			return POWER_SUPPLY_HEALTH_OVERHEAT;
		if (chip->batt_temp < BATT_TEMP_COLD)
			return POWER_SUPPLY_HEALTH_COLD;
		else
			return POWER_SUPPLY_HEALTH_GOOD;
	}
#else
	u8 reg_val;
	int rc;

	rc = qpnp_lbc_read(chip, chip->bat_if_base + BAT_IF_TEMP_STATUS_REG,
				&reg_val, 1);
	if (rc) {
		pr_err("Failed to read battery health rc=%d\n", rc);
		return POWER_SUPPLY_HEALTH_UNKNOWN;
	}

	if (BATT_TEMP_HOT_MASK & reg_val)
		return POWER_SUPPLY_HEALTH_OVERHEAT;
	if (!(BATT_TEMP_COLD_MASK & reg_val))
		return POWER_SUPPLY_HEALTH_COLD;
	if (chip->bat_is_cool)
		return POWER_SUPPLY_HEALTH_COOL;
	if (chip->bat_is_warm)
		return POWER_SUPPLY_HEALTH_WARM;

	return POWER_SUPPLY_HEALTH_GOOD;
#endif
}

static int get_prop_charge_type(struct qpnp_lbc_chip *chip)
{
	int rc;
	u8 reg_val;

	if (!get_prop_batt_present(chip))
		return POWER_SUPPLY_CHARGE_TYPE_NONE;

	rc = qpnp_lbc_read(chip, chip->chgr_base + INT_RT_STS_REG,
				&reg_val, 1);
	if (rc) {
		pr_err("Failed to read interrupt sts %d\n", rc);
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}

	if (reg_val & FAST_CHG_ON_IRQ)
		return POWER_SUPPLY_CHARGE_TYPE_FAST;

	return POWER_SUPPLY_CHARGE_TYPE_NONE;
}

static int get_prop_batt_status(struct qpnp_lbc_chip *chip)
{
	int rc;
	u8 reg_val;

	if (qpnp_lbc_is_usb_chg_plugged_in(chip) && chip->chg_done)
		return POWER_SUPPLY_STATUS_FULL;

	rc = qpnp_lbc_read(chip, chip->chgr_base + INT_RT_STS_REG,
				&reg_val, 1);
	if (rc) {
		pr_err("Failed to read interrupt sts rc= %d\n", rc);
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}

#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
	if (qpnp_lbc_is_usb_chg_plugged_in(chip) && chip->pseudo_ui_chg)
		return POWER_SUPPLY_STATUS_CHARGING;
#endif

	if (reg_val & FAST_CHG_ON_IRQ)
		return POWER_SUPPLY_STATUS_CHARGING;

	return POWER_SUPPLY_STATUS_DISCHARGING;
}

static int get_prop_current_now(struct qpnp_lbc_chip *chip)
{
#ifndef CONFIG_LGE_PM_BATTERY_EXTERNAL_FUELGAUGE
	union power_supply_propval ret = {0,};

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CURRENT_NOW, &ret);
		return ret.intval;
	} else {
		pr_debug("No BMS supply registered return 0\n");
	}
#endif
	return 0;
}

#define DEFAULT_CAPACITY	50
#ifdef CONFIG_LGE_PM_BATTERY_EXTERNAL_FUELGAUGE
#define COMPLETE_CAPACITY 	100
#endif

#ifdef CONFIG_LGE_PM 
#define LGE_RESUME_CHG_SOC  98
static void lge_set_chg_state_change(struct qpnp_lbc_chip *chip, int batt_status, int charger_status, 
            bool chg_disabled, int resume_limit, int cal_soc)
{

	pr_info("batt_status=%d,charger_status=%d,chg_disabled=%d\n",
				batt_status,charger_status,chg_disabled);

    /* Display as Full but real status is discahrging */
    if(batt_status == POWER_SUPPLY_STATUS_FULL
		&& charger_status
		&& !chg_disabled
		&& resume_limit
		&& cal_soc > resume_limit) {
			qpnp_lbc_charger_enable(chip, SOC, 0);
    }
	/* Resume charging start */
	else if(batt_status == POWER_SUPPLY_STATUS_FULL
		&& charger_status
		&& !chg_disabled
		&& resume_limit
		&& cal_soc <= resume_limit) {
			qpnp_lbc_vbatdet_override(chip, OVERRIDE_0);
			qpnp_lbc_charger_enable(chip, SOC, 1);
    }
	/* Normal Charging Start */
	else if(((batt_status == POWER_SUPPLY_STATUS_DISCHARGING) || (batt_status == POWER_SUPPLY_STATUS_NOT_CHARGING))
		&& charger_status
		&& !chg_disabled
		&& resume_limit) {
			qpnp_lbc_vbatdet_override(chip, OVERRIDE_0);
			qpnp_lbc_charger_enable(chip, SOC, 1);
   }
}
#endif

static int get_prop_capacity(struct qpnp_lbc_chip *chip)
{
#ifdef CONFIG_BATTERY_MAX17048
	int soc, battery_status, charger_in;

#ifdef CONFIG_LGE_PM
    int battery_status = 0;
#endif


	if (chip->fake_battery_soc >= 0)
		return chip->fake_battery_soc;

	if (chip->cfg_use_fake_battery || !get_prop_batt_present(chip))
		return DEFAULT_CAPACITY;

	battery_status = get_prop_batt_status(chip);
	charger_in = qpnp_lbc_is_usb_chg_plugged_in(chip);
	soc = max17048_get_capacity();

#ifdef CONFIG_LGE_PM
    battery_status = get_prop_batt_status(chip);
    if (battery_status == POWER_SUPPLY_STATUS_FULL)
		chip->chg_done = true;
#endif

	/* reset chg_done flag if capacity not 100% */
#ifdef CONFIG_LGE_PM
//                                                                                        
    if (chip->cfg_soc_resume_limit >= soc && chip->chg_done) {
#else
	if (soc < 100 && chip->chg_done) {
#endif
		chip->chg_done = false;
		power_supply_changed(&chip->batt_psy);
	}

	if (battery_status != POWER_SUPPLY_STATUS_CHARGING
			&& charger_in
			&& !chip->cfg_charging_disabled
			&& chip->cfg_soc_resume_limit
			&& soc <= chip->cfg_soc_resume_limit) {
		pr_debug("resuming charging at %d%% soc\n", soc);
		qpnp_lbc_vbatdet_override(chip, OVERRIDE_0);
		qpnp_lbc_charger_enable(chip, SOC, 1);
#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
		pr_info("wake lock = %d\n", wake_lock_active(&chip->chg_wake_lock));
		if (!wake_lock_active(&chip->chg_wake_lock)) {
			pr_info("resume CHG chg_wake_lock\n");
			wake_lock(&chip->chg_wake_lock);
		}
#endif
	}

	if (soc == 0) {
		if (!qpnp_lbc_is_usb_chg_plugged_in(chip))
			pr_warn_ratelimited("Batt 0, CHG absent\n");
	}
	pr_info("%s : soc = %d\n", __func__, soc);
	return soc;
//FOR RT9428, START ===============================
#elif defined(CONFIG_LGE_PM_BATTERY_EXTERNAL_FUELGAUGE)
	int soc, battery_status, charger_in;
#ifdef CONFIG_LGE_PM_BATTERY_RT9428_EOC_BY_SOC
	int scaled_soc = DEFAULT_CAPACITY;
#endif
	union power_supply_propval ret = {0,};

	if (chip->fake_battery_soc >= 0)
		return chip->fake_battery_soc;

	if (chip->cfg_use_fake_battery || !get_prop_batt_present(chip))
		return DEFAULT_CAPACITY;

	battery_status = get_prop_batt_status(chip);
	charger_in = qpnp_lbc_is_usb_chg_plugged_in(chip);

	if (chip->fuelgauge == NULL)
		chip->fuelgauge = power_supply_get_by_name("fuelgauge");

	if (chip->fuelgauge != NULL) {
		chip->fuelgauge->get_property(chip->fuelgauge, POWER_SUPPLY_PROP_CAPACITY, &ret);
	#ifdef CONFIG_LGE_PM_BATTERY_RT9428_EOC_BY_SOC
		scaled_soc = ret.intval;
		chip->fuelgauge->get_property(chip->fuelgauge, POWER_SUPPLY_PROP_CAPACITY_RAW, &ret);
	#endif
		soc = ret.intval;

#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE
		if(start_chg_factory_testmode == true)
		{
			pr_err("factory_testmode charging enable\n");
			soc = COMPLETE_CAPACITY;
		#ifdef CONFIG_LGE_PM_BATTERY_RT9428_EOC_BY_SOC
			scaled_soc = COMPLETE_CAPACITY;
		#endif
		}
#endif
	} else {
		printk("%s : Failed to read externalfuel gauge\n", __func__);
		soc = DEFAULT_CAPACITY;
	}

#ifdef CONFIG_LGE_PM
    battery_status = get_prop_batt_status(chip);
    if (battery_status == POWER_SUPPLY_STATUS_FULL)
		chip->chg_done = true;
#endif

	/* reset chg_done flag if capacity not 100% */
#ifdef CONFIG_LGE_PM
//                                                                                        
    if (chip->cfg_soc_resume_limit >= soc && chip->chg_done) {
#else
	if (soc < 100 && chip->chg_done) {
#endif
		chip->chg_done = false;
		power_supply_changed(&chip->batt_psy);
	}

	lge_set_chg_state_change(chip, battery_status, charger_in,
		chip->cfg_charging_disabled, chip->cfg_soc_resume_limit,soc);

	if (battery_status != POWER_SUPPLY_STATUS_CHARGING
			&& charger_in
			&& !chip->cfg_charging_disabled
			&& chip->cfg_soc_resume_limit
			&& soc <= chip->cfg_soc_resume_limit) {
		pr_debug("resuming charging at %d%% soc\n", soc);
		qpnp_lbc_vbatdet_override(chip, OVERRIDE_0);
		qpnp_lbc_charger_enable(chip, SOC, 1);

		if((chip->chg_fail_irq_happen == true) && (soc <= 90) ){
			qpnp_lbc_charger_enable(chip,CHG_FAIL_IRQ_HAPPEN, 1);
			chip->chg_fail_irq_happen = false;
			wake_unlock(&chip->chg_fail_irq_wake_lock);
		}

#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
		pr_info("wake lock = %d\n", wake_lock_active(&chip->chg_wake_lock));
		if (!wake_lock_active(&chip->chg_wake_lock)) {
			pr_info("resume CHG chg_wake_lock\n");
			wake_lock(&chip->chg_wake_lock);
		}
#endif
	}

	if (soc == 0) {
		if (!qpnp_lbc_is_usb_chg_plugged_in(chip))
			pr_warn_ratelimited("Batt 0, CHG absent\n");
	}
#ifdef CONFIG_LGE_PM_BATTERY_RT9428_EOC_BY_SOC
	pr_info(" %d (raw:%d) \n",scaled_soc,soc);
	return scaled_soc;
#else
	pr_info("%s : soc = %d\n", __func__, soc);
	return soc;
#endif
//FOR BMS, START ===============================
#else
	union power_supply_propval ret = {0,};
	int soc, battery_status, charger_in;

#ifdef CONFIG_LGE_PM
	union power_supply_propval ret_calculated_soc = {0,};
#endif

	if (chip->fake_battery_soc >= 0)
		return chip->fake_battery_soc;

#ifdef CONFIG_LGE_PM
	if (chip->cfg_use_fake_battery)
		return DEFAULT_CAPACITY;
#else
	if (chip->cfg_use_fake_battery || !get_prop_batt_present(chip))
		return DEFAULT_CAPACITY;
#endif

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_CAPACITY, &ret);
		mutex_lock(&chip->chg_enable_lock);
		if (chip->chg_done)
			chip->bms_psy->get_property(chip->bms_psy,
					POWER_SUPPLY_PROP_CAPACITY, &ret);
		battery_status = get_prop_batt_status(chip);
		charger_in = qpnp_lbc_is_usb_chg_plugged_in(chip);

#ifdef CONFIG_LGE_PM
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_CALCULATED_SOC, &ret_calculated_soc);
		pr_debug("get calculated_soc = %d\n",ret_calculated_soc.intval);
#endif

#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE
		if(start_chg_factory_testmode == true)
		{
			pr_info("factory_testmode charging enable\n");
			ret_calculated_soc.intval = chip->cfg_soc_resume_limit;
			ret.intval = chip->cfg_soc_resume_limit;
		}
#endif

		/* reset chg_done flag if capacity not 100% */
#ifdef CONFIG_LGE_PM
//                                                                  
		if((ret_calculated_soc.intval <= chip->cfg_soc_resume_limit) && chip->chg_done){
#else
		if (ret.intval < 100 && chip->chg_done) {
#endif
			chip->chg_done = false;
			power_supply_changed(&chip->batt_psy);
		}
#ifdef CONFIG_LGE_PM
        lge_set_chg_state_change(chip, battery_status, charger_in,
            chip->cfg_charging_disabled, chip->cfg_soc_resume_limit, ret_calculated_soc.intval);

		if (battery_status != POWER_SUPPLY_STATUS_CHARGING
				&& charger_in
				&& !chip->cfg_charging_disabled
				&& chip->cfg_soc_resume_limit
				&& ret_calculated_soc.intval <= chip->cfg_soc_resume_limit) {
			pr_info("resuming charging at %d%% calculated_soc\n",
					ret_calculated_soc.intval);
			if (!chip->cfg_disable_vbatdet_based_recharge)
				qpnp_lbc_vbatdet_override(chip, OVERRIDE_0);
			qpnp_lbc_charger_enable(chip, SOC, 1);

			if((chip->chg_fail_irq_happen == true) && (ret_calculated_soc.intval <= 90) ){
				qpnp_lbc_charger_enable(chip,CHG_FAIL_IRQ_HAPPEN, 1);
				chip->chg_fail_irq_happen = false;
				wake_unlock(&chip->chg_fail_irq_wake_lock);
			}
#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
			pr_info("wake lock = %d\n", wake_lock_active(&chip->chg_wake_lock));
			if (!wake_lock_active(&chip->chg_wake_lock)) {
				pr_info("resume chg_wake_lock\n");
				wake_lock(&chip->chg_wake_lock);
			}
#endif
		}
#else
		if (battery_status != POWER_SUPPLY_STATUS_CHARGING
				&& charger_in
				&& !chip->cfg_charging_disabled
				&& chip->cfg_soc_resume_limit
				&& ret.intval <= chip->cfg_soc_resume_limit) {
			pr_debug("resuming charging at %d%% soc\n",
					ret.intval);
			if (!chip->cfg_disable_vbatdet_based_recharge)
				qpnp_lbc_vbatdet_override(chip, OVERRIDE_0);
			qpnp_lbc_charger_enable(chip, SOC, 1);
		}
#endif
		mutex_unlock(&chip->chg_enable_lock);

		soc = ret.intval;
		if (soc == 0) {
			if (!qpnp_lbc_is_usb_chg_plugged_in(chip))
				pr_warn_ratelimited("Batt 0, CHG absent\n");
		}
		return soc;
	} else {
		pr_debug("No BMS supply registered return %d\n",
							DEFAULT_CAPACITY);
	}

	/*
	 * Return default capacity to avoid userspace
	 * from shutting down unecessarily
	 */
	return DEFAULT_CAPACITY;
#endif
}

#ifdef CONFIG_LGE_PM_BATTERY_RT9428_EOC_BY_SOC
static int get_prop_batt_status_lge(struct qpnp_lbc_chip *chip)
{
	/* report full when state of charge is 100, even if phone is charging */
	if(qpnp_lbc_is_usb_chg_plugged_in(chip)
			&& get_prop_capacity(chip) >= 100)
		return POWER_SUPPLY_STATUS_FULL;
	else
		return get_prop_batt_status(chip);
}
#endif

#define DEFAULT_TEMP		250
static int get_prop_batt_temp(struct qpnp_lbc_chip *chip)
{
	int rc = 0;
	struct qpnp_vadc_result results;

#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
	if (pseudo_batt_info.mode) {
		pr_debug("battery fake mode : %d \n", pseudo_batt_info.mode);
		return pseudo_batt_info.temp * 10;
		}
#endif

	if (chip->cfg_use_fake_battery || !get_prop_batt_present(chip))
		return DEFAULT_TEMP;

#ifdef CONFIG_LGE_PM_FACTORY_CABLE
	if (is_factory_cable() && !get_prop_batt_present(chip)) {
		pr_debug("factory cable without battery: %d\n", DEFAULT_TEMP / 10);
		return DEFAULT_TEMP;
	}
#endif

	rc = qpnp_vadc_read(chip->vadc_dev, LR_MUX1_BATT_THERM, &results);
	if (rc) {
		pr_debug("Unable to read batt temperature rc=%d\n", rc);
		return DEFAULT_TEMP;
	}
	pr_debug("get_bat_temp %d, %lld\n", results.adc_code,
							results.physical);

	pr_info("[LGE][batt_temp] adc_code = %d, physical = %lld\n", results.adc_code, results.physical);

#if defined (CONFIG_MACH_MSM8916_C30_GLOBAL_COM) || defined (CONFIG_MACH_MSM8916_C30DS_GLOBAL_COM) || defined (CONFIG_MACH_MSM8916_C30F_GLOBAL_COM)
	return DEFAULT_TEMP;
#else
	return (int)results.physical;
#endif
}

#ifdef CONFIG_LGE_PM
#ifdef CONFIG_LGE_PM_BATTERY_RT9428_FUELGAUGE
int get_prop_batt_temp_raw(void){
	int rc = 0;
	struct qpnp_vadc_result results;

	if ( qpnp_chg == NULL ){
		pr_debug("qpnp_chg is not yet ready\n");
		return DEFAULT_TEMP;
	}

	rc = qpnp_vadc_read(qpnp_chg->vadc_dev, LR_MUX1_BATT_THERM, &results);
	if (rc) {
		pr_debug("Unable to read batt temperature rc=%d\n", rc);
		return DEFAULT_TEMP;
	}
	pr_debug("get_bat_temp %d, %lld\n", results.adc_code,
							results.physical);

	pr_info("[LGE][batt_temp] adc_code = %d, physical = %lld\n", results.adc_code, results.physical);

	return (int)results.physical;
}
#endif
#endif

static void qpnp_lbc_set_appropriate_current(struct qpnp_lbc_chip *chip)
{
	unsigned int chg_current = chip->usb_psy_ma;

	if (chip->bat_is_cool && chip->cfg_cool_bat_chg_ma)
		chg_current = min(chg_current, chip->cfg_cool_bat_chg_ma);
	if (chip->bat_is_warm && chip->cfg_warm_bat_chg_ma)
		chg_current = min(chg_current, chip->cfg_warm_bat_chg_ma);
#ifndef CONFIG_LGE_PM
	if (chip->therm_lvl_sel != 0 && chip->thermal_mitigation)
		chg_current = min(chg_current,
			chip->thermal_mitigation[chip->therm_lvl_sel]);
#endif
	pr_debug("setting charger current %d mA\n", chg_current);
	qpnp_lbc_ibatmax_set(chip, chg_current);
}

#ifdef CONFIG_LGE_PM_BATTERY_ID_CHECKER
static int get_prop_batt_id_valid(void)
{
	return (int)is_lge_battery_valid();
}
#endif

#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE
static char *get_prop_hw_rev_name(void)
{
	char *name;
	name = lge_get_board_rev();
	return name;
}
#endif

static void qpnp_batt_external_power_changed(struct power_supply *psy)
{
	struct qpnp_lbc_chip *chip = container_of(psy, struct qpnp_lbc_chip,
								batt_psy);
	union power_supply_propval ret = {0,};
	int current_ma;
	unsigned long flags;

	spin_lock_irqsave(&chip->ibat_change_lock, flags);
#ifndef CONFIG_LGE_PM_BATTERY_EXTERNAL_FUELGAUGE
	if (!chip->bms_psy)
		chip->bms_psy = power_supply_get_by_name("bms");
#endif
	if (qpnp_lbc_is_usb_chg_plugged_in(chip)) {
		chip->usb_psy->get_property(chip->usb_psy,
				POWER_SUPPLY_PROP_CURRENT_MAX, &ret);
		current_ma = ret.intval / 1000;

		if (current_ma == chip->prev_max_ma)
			goto skip_current_config;

#ifdef CONFIG_LGE_THERMALE_CHG_CONTROL
		chip->chg_current_max = current_ma;
		if (qpnp_chg->chg_current_te == 0)
			chip->chg_current_te = current_ma;
		else
			chip->chg_current_te = qpnp_chg->chg_current_te;

		pr_info("thermal charging current_te to %d(mA)\n", chip->chg_current_te);
#endif

#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
			if((pseudo_batt_info.mode == 1)&&(!is_factory_cable())){
				current_ma = PSEUDO_BATT_MAX;
			}
#endif
		/* Disable charger in case of reset or suspend event */
		if (current_ma <= 2 && !chip->cfg_use_fake_battery
				&& get_prop_batt_present(chip)) {
			qpnp_lbc_charger_enable(chip, CURRENT, 0);
			chip->usb_psy_ma = QPNP_CHG_I_MAX_MIN_90;
			qpnp_lbc_set_appropriate_current(chip);
		} else {
#ifdef CONFIG_LGE_THERMALE_CHG_CONTROL
			if (chip->chg_current_te <= chip->prev_max_ma) {
				chip->usb_psy_ma = chip->chg_current_te;
				pr_info("thermal curr decrease %dmA\n",chip->usb_psy_ma);
			} else {
				if(chip->battemp_chg_state < CHG_BATT_DECCUR_STATE) {
					if(chip->chg_current_te <= current_ma)
						chip->usb_psy_ma = chip->chg_current_te;
					else
						chip->usb_psy_ma = current_ma;
				}
				else if(chip->battemp_chg_state == CHG_BATT_DECCUR_STATE) {
					if(chip->chg_current_te <= DC_IUSB_CURRENT)
						chip->usb_psy_ma = chip->chg_current_te;
					else
						chip->usb_psy_ma = DC_IUSB_CURRENT;
				}
				else {
					if(chip->chg_current_te >= current_ma)
						chip->usb_psy_ma = current_ma;
					else
						chip->usb_psy_ma = chip->chg_current_te;
				}
				pr_info("scenario state : %d usb_psy_ma : %d",
						chip->battemp_chg_state, chip->usb_psy_ma);
			}
#else
			chip->usb_psy_ma = current_ma;
			pr_info("usb_psy_ma current to %d(mA)\n", chip->usb_psy_ma);
#endif
			qpnp_lbc_set_appropriate_current(chip);
			qpnp_lbc_charger_enable(chip, CURRENT, 1);
		}
	}

skip_current_config:
	spin_unlock_irqrestore(&chip->ibat_change_lock, flags);
	pr_debug("power supply changed batt_psy\n");
	power_supply_changed(&chip->batt_psy);
}

#ifndef CONFIG_LGE_PM
static int qpnp_lbc_system_temp_level_set(struct qpnp_lbc_chip *chip,
								int lvl_sel)
{
	int rc = 0;
	int prev_therm_lvl;
	unsigned long flags;

	if (!chip->thermal_mitigation) {
		pr_err("Thermal mitigation not supported\n");
		return -EINVAL;
	}

	if (lvl_sel < 0) {
		pr_err("Unsupported level selected %d\n", lvl_sel);
		return -EINVAL;
	}

	if (lvl_sel >= chip->cfg_thermal_levels) {
		pr_err("Unsupported level selected %d forcing %d\n", lvl_sel,
				chip->cfg_thermal_levels - 1);
		lvl_sel = chip->cfg_thermal_levels - 1;
	}

	if (lvl_sel == chip->therm_lvl_sel)
		return 0;

	spin_lock_irqsave(&chip->ibat_change_lock, flags);
	prev_therm_lvl = chip->therm_lvl_sel;
	chip->therm_lvl_sel = lvl_sel;
	if (chip->therm_lvl_sel == (chip->cfg_thermal_levels - 1)) {
		/* Disable charging if highest value selected by */
		rc = qpnp_lbc_charger_enable(chip, THERMAL, 0);
		if (rc < 0)
			dev_err(chip->dev,
				"Failed to set disable charging rc %d\n", rc);
		goto out;
	}

	qpnp_lbc_set_appropriate_current(chip);

	if (prev_therm_lvl == chip->cfg_thermal_levels - 1) {
		/*
		 * If previously highest value was selected charging must have
		 * been disabed. Enable charging.
		 */
		rc = qpnp_lbc_charger_enable(chip, THERMAL, 1);
		if (rc < 0) {
			dev_err(chip->dev,
				"Failed to enable charging rc %d\n", rc);
		}
	}
out:
	spin_unlock_irqrestore(&chip->ibat_change_lock, flags);
	return rc;
}

#endif
#define MIN_COOL_TEMP		-300
#define MAX_WARM_TEMP		1000
#define HYSTERISIS_DECIDEGC	20

static int qpnp_lbc_configure_jeita(struct qpnp_lbc_chip *chip,
			enum power_supply_property psp, int temp_degc)
{
	int rc = 0;

	if ((temp_degc < MIN_COOL_TEMP) || (temp_degc > MAX_WARM_TEMP)) {
		pr_err("Bad temperature request %d\n", temp_degc);
		return -EINVAL;
	}

	mutex_lock(&chip->jeita_configure_lock);
	switch (psp) {
	case POWER_SUPPLY_PROP_COOL_TEMP:
		if (temp_degc >=
			(chip->cfg_warm_bat_decidegc - HYSTERISIS_DECIDEGC)) {
			pr_err("Can't set cool %d higher than warm %d - hysterisis %d\n",
					temp_degc,
					chip->cfg_warm_bat_decidegc,
					HYSTERISIS_DECIDEGC);
			rc = -EINVAL;
			goto mutex_unlock;
		}
		if (chip->bat_is_cool)
			chip->adc_param.high_temp =
				temp_degc + HYSTERISIS_DECIDEGC;
		else if (!chip->bat_is_warm)
			chip->adc_param.low_temp = temp_degc;

		chip->cfg_cool_bat_decidegc = temp_degc;
		break;
	case POWER_SUPPLY_PROP_WARM_TEMP:
		if (temp_degc <=
		(chip->cfg_cool_bat_decidegc + HYSTERISIS_DECIDEGC)) {
			pr_err("Can't set warm %d higher than cool %d + hysterisis %d\n",
					temp_degc,
					chip->cfg_warm_bat_decidegc,
					HYSTERISIS_DECIDEGC);
			rc = -EINVAL;
			goto mutex_unlock;
		}
		if (chip->bat_is_warm)
			chip->adc_param.low_temp =
				temp_degc - HYSTERISIS_DECIDEGC;
		else if (!chip->bat_is_cool)
			chip->adc_param.high_temp = temp_degc;

		chip->cfg_warm_bat_decidegc = temp_degc;
		break;
	default:
		rc = -EINVAL;
		goto mutex_unlock;
	}

	if (qpnp_adc_tm_channel_measure(chip->adc_tm_dev, &chip->adc_param))
		pr_err("request ADC error\n");

mutex_unlock:
	mutex_unlock(&chip->jeita_configure_lock);
	return rc;
}

static int qpnp_batt_property_is_writeable(struct power_supply *psy,
					enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
#ifdef CONFIG_LGE_PM_BATTERY_RT9428_EOC_BY_SOC
	case POWER_SUPPLY_PROP_STATUS_RAW:
#endif
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_COOL_TEMP:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
	case POWER_SUPPLY_PROP_WARM_TEMP:
#ifdef CONFIG_LGE_PM
	case POWER_SUPPLY_PROP_SAFETY_TIMER:
#endif
#ifndef CONFIG_LGE_PM
	 case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
#endif
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX
	case POWER_SUPPLY_PROP_USB_CURRENT_MAX:
#endif
		return 1;
	default:
		break;
	}

	return 0;
}

/*
 * End of charge happens only when BMS reports the battery status as full. For
 * charging to end the s/w must put the usb path in suspend. Note that there
 * is no battery fet and usb path suspend is the only control to prevent any
 * current going in to the battery (and the system)
 * Charging can begin only when VBATDET comparator outputs 0. This indicates
 * that the battery is a at a lower voltage than 4% of the vddmax value.
 * S/W can override this comparator to output a favourable value - this is
 * used while resuming charging when the battery hasnt fallen below 4% but
 * the SOC has fallen below the resume threshold.
 *
 * In short, when SOC resume happens:
 * a. overide the comparator to output 0
 * b. enable charging
 *
 * When vbatdet based resume happens:
 * a. enable charging
 *
 * When end of charge happens:
 * a. disable the overrides in the comparator
 *    (may be from a previous soc resume)
 * b. disable charging
 */
static int qpnp_batt_power_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct qpnp_lbc_chip *chip = container_of(psy, struct qpnp_lbc_chip,
								batt_psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
#ifdef CONFIG_LGE_PM_BATTERY_RT9428_EOC_BY_SOC
	case POWER_SUPPLY_PROP_STATUS_RAW:
#endif
		if (val->intval == POWER_SUPPLY_STATUS_FULL &&
				!chip->cfg_float_charge) {
			mutex_lock(&chip->chg_enable_lock);

			/* Disable charging */
			rc = qpnp_lbc_charger_enable(chip, SOC, 0);
			if (rc)
				pr_err("Failed to disable charging rc=%d\n",
						rc);
			else
				chip->chg_done = true;

#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
			pr_info("wake lock = %d\n", wake_lock_active(&chip->chg_wake_lock));
			if (wake_lock_active(&chip->chg_wake_lock)) {
				pr_info("chg FULL chg_wake_unlock\n");
				wake_unlock(&chip->chg_wake_lock);
			}
#endif
			/*
			 * Enable VBAT_DET based charging:
			 * To enable charging when VBAT falls below VBAT_DET
			 * and device stays suspended after EOC.
			 */
			if (!chip->cfg_disable_vbatdet_based_recharge) {
				/* No override for VBAT_DET_LO comp */
				rc = qpnp_lbc_vbatdet_override(chip,
							OVERRIDE_NONE);
				if (rc)
					pr_err("Failed to override VBAT_DET rc=%d\n",
							rc);
				else
					qpnp_lbc_enable_irq(chip,
						&chip->irqs[CHG_VBAT_DET_LO]);
			}

			mutex_unlock(&chip->chg_enable_lock);
		}
		break;
	case POWER_SUPPLY_PROP_COOL_TEMP:
		rc = qpnp_lbc_configure_jeita(chip, psp, val->intval);
		break;
	case POWER_SUPPLY_PROP_WARM_TEMP:
		rc = qpnp_lbc_configure_jeita(chip, psp, val->intval);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		chip->fake_battery_soc = val->intval;
		pr_debug("power supply changed batt_psy\n");
		break;
#ifdef CONFIG_LGE_PM
	case POWER_SUPPLY_PROP_SAFETY_TIMER:
		set_safety_timer(chip, val->intval);
		break;
#endif
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		chip->cfg_charging_disabled = !(val->intval);
		rc = qpnp_lbc_charger_enable(chip, USER,
						!chip->cfg_charging_disabled);
		if (rc)
			pr_err("Failed to disable charging rc=%d\n", rc);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		qpnp_lbc_vinmin_set(chip, val->intval / 1000);
		break;
#ifndef CONFIG_LGE_PM
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		qpnp_lbc_system_temp_level_set(chip, val->intval);
		break;
#endif
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX
	case POWER_SUPPLY_PROP_USB_CURRENT_MAX:
		if(val->intval)
			usb_current_max_enabled = 1;
		else
			usb_current_max_enabled = 0;
		qpnp_lbc_ibatmax_set(qpnp_chg, qpnp_chg->chg_current_max);
		break;
#endif
	default:
		return -EINVAL;
	}

	power_supply_changed(&chip->batt_psy);
	return rc;
}

static int qpnp_batt_power_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE
	char *buf;
#endif
	struct qpnp_lbc_chip *chip =
		container_of(psy, struct qpnp_lbc_chip, batt_psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
#ifdef CONFIG_LGE_PM_BATTERY_RT9428_EOC_BY_SOC
		val->intval = get_prop_batt_status_lge(chip);
		break;
	case POWER_SUPPLY_PROP_STATUS_RAW:
#endif
		val->intval = get_prop_batt_status(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = get_prop_charge_type(chip);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = get_prop_batt_health(chip);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = get_prop_batt_present(chip);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = chip->cfg_max_voltage_mv * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = chip->cfg_min_voltage_mv * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
		if (pseudo_batt_info.mode) {
			val->intval = pseudo_batt_info.volt;
			break;
		}
#endif
		val->intval = get_prop_battery_voltage_now(chip);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = get_prop_batt_temp(chip);
		break;
	case POWER_SUPPLY_PROP_COOL_TEMP:
		val->intval = chip->cfg_cool_bat_decidegc;
		break;
	case POWER_SUPPLY_PROP_WARM_TEMP:
		val->intval = chip->cfg_warm_bat_decidegc;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = get_prop_capacity(chip);
#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
		if (pseudo_batt_info.mode) {
			val->intval = pseudo_batt_info.capacity;
		}
#endif
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = get_prop_current_now(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		val->intval = !(chip->cfg_charging_disabled);
		break;
#ifndef CONFIG_LGE_PM
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		val->intval = chip->therm_lvl_sel;
		break;
#endif
#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
	case POWER_SUPPLY_PROP_PSEUDO_BATT:
		val->intval = pseudo_batt_info.mode;
		break;
#endif
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX
	case POWER_SUPPLY_PROP_USB_CURRENT_MAX:
		if(usb_current_max_enabled)
			val->intval = 1;
		else
			val->intval = 0;
		break;
#endif
#ifdef CONFIG_LGE_PM
	case POWER_SUPPLY_PROP_SAFETY_TIMER:
		val->intval = (int)is_safety_timer;
		break;
#endif
#ifdef CONFIG_LGE_PM_BATTERY_ID_CHECKER
	case POWER_SUPPLY_PROP_BATTERY_ID_CHECKER:
		val->intval = read_lge_battery_id();
		break;
	case POWER_SUPPLY_PROP_VALID_BATT:
		val->intval = get_prop_batt_id_valid();
		break;
#endif
#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE
	case POWER_SUPPLY_PROP_HW_REV:
		buf = get_prop_hw_rev_name();
		val->strval = buf;
		break;
#endif
	default:
		return -EINVAL;
	}

	return 0;
}

static void qpnp_lbc_jeita_adc_notification(enum qpnp_tm_state state, void *ctx)
{
	struct qpnp_lbc_chip *chip = ctx;
	bool bat_warm = 0, bat_cool = 0;
	int temp;
	unsigned long flags;

	if (state >= ADC_TM_STATE_NUM) {
		pr_err("invalid notification %d\n", state);
		return;
	}

	temp = get_prop_batt_temp(chip);

	pr_debug("temp = %d state = %s\n", temp,
			state == ADC_TM_WARM_STATE ? "warm" : "cool");

	if (state == ADC_TM_WARM_STATE) {
		if (temp >= chip->cfg_warm_bat_decidegc) {
			/* Normal to warm */
			bat_warm = true;
			bat_cool = false;
			chip->adc_param.low_temp =
					chip->cfg_warm_bat_decidegc
					- HYSTERISIS_DECIDEGC;
			chip->adc_param.state_request =
				ADC_TM_COOL_THR_ENABLE;
		} else if (temp >=
			chip->cfg_cool_bat_decidegc + HYSTERISIS_DECIDEGC) {
			/* Cool to normal */
			bat_warm = false;
			bat_cool = false;

			chip->adc_param.low_temp =
					chip->cfg_cool_bat_decidegc;
			chip->adc_param.high_temp =
					chip->cfg_warm_bat_decidegc;
			chip->adc_param.state_request =
					ADC_TM_HIGH_LOW_THR_ENABLE;
		}
	} else {
		if (temp <= chip->cfg_cool_bat_decidegc) {
			/* Normal to cool */
			bat_warm = false;
			bat_cool = true;
			chip->adc_param.high_temp =
					chip->cfg_cool_bat_decidegc
					+ HYSTERISIS_DECIDEGC;
			chip->adc_param.state_request =
					ADC_TM_WARM_THR_ENABLE;
		} else if (temp <= (chip->cfg_warm_bat_decidegc -
					HYSTERISIS_DECIDEGC)){
			/* Warm to normal */
			bat_warm = false;
			bat_cool = false;

			chip->adc_param.low_temp =
					chip->cfg_cool_bat_decidegc;
			chip->adc_param.high_temp =
					chip->cfg_warm_bat_decidegc;
			chip->adc_param.state_request =
					ADC_TM_HIGH_LOW_THR_ENABLE;
		}
	}

	if (chip->bat_is_cool ^ bat_cool || chip->bat_is_warm ^ bat_warm) {
		spin_lock_irqsave(&chip->ibat_change_lock, flags);
		chip->bat_is_cool = bat_cool;
		chip->bat_is_warm = bat_warm;
		qpnp_lbc_set_appropriate_vddmax(chip);
		qpnp_lbc_set_appropriate_current(chip);
		spin_unlock_irqrestore(&chip->ibat_change_lock, flags);
	}

	pr_debug("warm %d, cool %d, low = %d deciDegC, high = %d deciDegC\n",
			chip->bat_is_warm, chip->bat_is_cool,
			chip->adc_param.low_temp, chip->adc_param.high_temp);

	if (qpnp_adc_tm_channel_measure(chip->adc_tm_dev, &chip->adc_param))
		pr_err("request ADC error\n");
}

#define IBAT_TERM_EN_MASK		BIT(3)
static int qpnp_lbc_chg_init(struct qpnp_lbc_chip *chip)
{
	int rc;
	u8 reg_val;

	qpnp_lbc_vbatweak_set(chip, chip->cfg_batt_weak_voltage_uv);
	rc = qpnp_lbc_vinmin_set(chip, chip->cfg_min_voltage_mv);
	if (rc) {
		pr_err("Failed  to set  vin_min rc=%d\n", rc);
		return rc;
	}
	rc = qpnp_lbc_vddsafe_set(chip, chip->cfg_safe_voltage_mv);
	if (rc) {
		pr_err("Failed to set vdd_safe rc=%d\n", rc);
		return rc;
	}
	rc = qpnp_lbc_vddmax_set(chip, chip->cfg_max_voltage_mv);
	if (rc) {
		pr_err("Failed to set vdd_safe rc=%d\n", rc);
		return rc;
	}
	rc = qpnp_lbc_ibatsafe_set(chip, chip->cfg_safe_current);
	if (rc) {
		pr_err("Failed to set ibat_safe rc=%d\n", rc);
		return rc;
	}

	if (of_find_property(chip->spmi->dev.of_node, "qcom,tchg-mins", NULL)) {
		rc = qpnp_lbc_tchg_max_set(chip, chip->cfg_tchg_mins);
		if (rc) {
			pr_err("Failed to set tchg_mins rc=%d\n", rc);
			return rc;
		}
	}

	/*
	 * Override VBAT_DET comparator to enable charging
	 * irrespective of VBAT above VBAT_DET.
	 */
	rc = qpnp_lbc_vbatdet_override(chip, OVERRIDE_0);
	if (rc) {
		pr_err("Failed to override comp rc=%d\n", rc);
		return rc;
	}

	/*
	 * Disable iterm comparator of linear charger to disable charger
	 * detecting end of charge condition based on DT configuration
	 * and float charge configuration.
	 */
	if (!chip->cfg_charger_detect_eoc || chip->cfg_float_charge) {
		rc = qpnp_lbc_masked_write(chip,
				chip->chgr_base + CHG_IBATTERM_EN_REG,
				IBAT_TERM_EN_MASK, 0);
		if (rc) {
			pr_err("Failed to disable EOC comp rc=%d\n", rc);
			return rc;
		}
	}

	/* Disable charger watchdog */
	reg_val = 0;
	rc = qpnp_lbc_write(chip, chip->chgr_base + CHG_WDOG_EN_REG,
				&reg_val, 1);

	return rc;
}

static int qpnp_lbc_bat_if_init(struct qpnp_lbc_chip *chip)
{
	u8 reg_val;
	int rc;

	/* Select battery presence detection */
	switch (chip->cfg_bpd_detection) {
	case BPD_TYPE_BAT_THM:
		reg_val = BATT_THM_EN;
		break;
	case BPD_TYPE_BAT_ID:
		reg_val = BATT_ID_EN;
		break;
	case BPD_TYPE_BAT_THM_BAT_ID:
		reg_val = BATT_THM_EN | BATT_ID_EN;
		break;
	default:
		reg_val = BATT_THM_EN;
		break;
	}

	rc = qpnp_lbc_masked_write(chip,
			chip->bat_if_base + BAT_IF_BPD_CTRL_REG,
			BATT_BPD_CTRL_SEL_MASK, reg_val);
	if (rc) {
		pr_err("Failed to choose BPD rc=%d\n", rc);
		return rc;
	}

	/* Force on VREF_BAT_THM */
	reg_val = VREF_BATT_THERM_FORCE_ON;
	rc = qpnp_lbc_write(chip,
			chip->bat_if_base + BAT_IF_VREF_BAT_THM_CTRL_REG,
			&reg_val, 1);
	if (rc) {
		pr_err("Failed to force on VREF_BAT_THM rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int qpnp_lbc_usb_path_init(struct qpnp_lbc_chip *chip)
{
	int rc;
	u8 reg_val;

	if (qpnp_lbc_is_usb_chg_plugged_in(chip)) {
		reg_val = 0;
		rc = qpnp_lbc_write(chip,
			chip->usb_chgpth_base + CHG_USB_ENUM_T_STOP_REG,
			&reg_val, 1);
		if (rc) {
			pr_err("Failed to write enum stop rc=%d\n", rc);
			return -ENXIO;
		}
	}

	if (chip->cfg_charging_disabled) {
		rc = qpnp_lbc_charger_enable(chip, USER, 0);
		if (rc)
			pr_err("Failed to disable charging rc=%d\n", rc);

	/*
	 * Disable follow-on-reset if charging is explictly disabled,
	 * this forces the charging to be disabled across reset.
	 * Note: Explicitly disabling charging is only a debug/test
	 * configuration
	 */
		reg_val = 0x0;
		rc = __qpnp_lbc_secure_write(chip->spmi, chip->chgr_base,
				CHG_PERPH_RESET_CTRL3_REG, &reg_val, 1);
		if (rc)
			pr_err("Failed to configure PERPH_CTRL3 rc=%d\n", rc);
		else
			pr_debug("Charger is not following PMIC reset\n");
	} else {
		/*
		 * Enable charging explictly,
		 * because not sure the default behavior.
		 */
		reg_val = CHG_ENABLE;
		rc = qpnp_lbc_masked_write(chip, chip->chgr_base + CHG_CTRL_REG,
					CHG_EN_MASK, reg_val);
		if (rc)
			pr_err("Failed to enable charger rc=%d\n", rc);
	}

	return rc;
}

#define LBC_MISC_DIG_VERSION_1			0x01
static int qpnp_lbc_misc_init(struct qpnp_lbc_chip *chip)
{
	int rc;
	u8 reg_val, reg_val1, trim_center;

	/* Check if this LBC MISC version supports VDD trimming */
	rc = qpnp_lbc_read(chip, chip->misc_base + MISC_REV2_REG,
			&reg_val, 1);
	if (rc) {
		pr_err("Failed to read VDD_EA TRIM3 reg rc=%d\n", rc);
		return rc;
	}

	if (reg_val >= LBC_MISC_DIG_VERSION_1) {
		chip->supported_feature_flag |= VDD_TRIM_SUPPORTED;
		/* Read initial VDD trim value */
		rc = qpnp_lbc_read(chip, chip->misc_base + MISC_TRIM3_REG,
				&reg_val, 1);
		if (rc) {
			pr_err("Failed to read VDD_EA TRIM3 reg rc=%d\n", rc);
			return rc;
		}

		rc = qpnp_lbc_read(chip, chip->misc_base + MISC_TRIM4_REG,
				&reg_val1, 1);
		if (rc) {
			pr_err("Failed to read VDD_EA TRIM3 reg rc=%d\n", rc);
			return rc;
		}

		trim_center = ((reg_val & MISC_TRIM3_VDD_MASK)
					>> VDD_TRIM3_SHIFT)
					| ((reg_val1 & MISC_TRIM4_VDD_MASK)
					>> VDD_TRIM4_SHIFT);
		chip->init_trim_uv = qpnp_lbc_get_trim_voltage(trim_center);
		chip->delta_vddmax_uv = chip->init_trim_uv;
		pr_debug("Initial trim center %x trim_uv %d\n",
				trim_center, chip->init_trim_uv);
	}

	pr_debug("Setting BOOT_DONE\n");
	reg_val = MISC_BOOT_DONE;
	rc = qpnp_lbc_write(chip, chip->misc_base + MISC_BOOT_DONE_REG,
				&reg_val, 1);

	return rc;
}

#define OF_PROP_READ(chip, prop, qpnp_dt_property, retval, optional)	\
do {									\
	if (retval)							\
		break;							\
									\
	retval = of_property_read_u32(chip->spmi->dev.of_node,		\
					"qcom," qpnp_dt_property,	\
					&chip->prop);			\
									\
	if ((retval == -EINVAL) && optional)				\
		retval = 0;						\
	else if (retval)						\
		pr_err("Error reading " #qpnp_dt_property		\
				" property rc = %d\n", rc);		\
} while (0)

static int qpnp_charger_read_dt_props(struct qpnp_lbc_chip *chip)
{
	int rc = 0;
	const char *bpd;

	OF_PROP_READ(chip, cfg_max_voltage_mv, "vddmax-mv", rc, 0);
	OF_PROP_READ(chip, cfg_safe_voltage_mv, "vddsafe-mv", rc, 0);
	OF_PROP_READ(chip, cfg_min_voltage_mv, "vinmin-mv", rc, 0);
	OF_PROP_READ(chip, cfg_safe_current, "ibatsafe-ma", rc, 0);
	if (rc)
		pr_err("Error reading required property rc=%d\n", rc);

	OF_PROP_READ(chip, cfg_tchg_mins, "tchg-mins", rc, 1);
	OF_PROP_READ(chip, cfg_warm_bat_decidegc, "warm-bat-decidegc", rc, 1);
	OF_PROP_READ(chip, cfg_cool_bat_decidegc, "cool-bat-decidegc", rc, 1);
	OF_PROP_READ(chip, cfg_hot_batt_p, "batt-hot-percentage", rc, 1);
	OF_PROP_READ(chip, cfg_cold_batt_p, "batt-cold-percentage", rc, 1);
	OF_PROP_READ(chip, cfg_batt_weak_voltage_uv, "vbatweak-uv", rc, 1);
	OF_PROP_READ(chip, cfg_soc_resume_limit, "resume-soc", rc, 1);
	if (rc) {
		pr_err("Error reading optional property rc=%d\n", rc);
		return rc;
	}

	rc = of_property_read_string(chip->spmi->dev.of_node,
						"qcom,bpd-detection", &bpd);
	if (rc) {

		chip->cfg_bpd_detection = BPD_TYPE_BAT_THM;
		rc = 0;
	} else {
		chip->cfg_bpd_detection = get_bpd(bpd);
		if (chip->cfg_bpd_detection < 0) {
			pr_err("Failed to determine bpd schema rc=%d\n", rc);
			return -EINVAL;
		}
	}

	/*
	 * Look up JEITA compliance parameters if cool and warm temp
	 * provided
	 */
	if (chip->cfg_cool_bat_decidegc || chip->cfg_warm_bat_decidegc) {
		chip->adc_tm_dev = qpnp_get_adc_tm(chip->dev, "chg");
		if (IS_ERR(chip->adc_tm_dev)) {
			rc = PTR_ERR(chip->adc_tm_dev);
			if (rc != -EPROBE_DEFER)
				pr_err("Failed to get adc-tm rc=%d\n", rc);
			return rc;
		}

		OF_PROP_READ(chip, cfg_warm_bat_chg_ma, "ibatmax-warm-ma",
				rc, 1);
		OF_PROP_READ(chip, cfg_cool_bat_chg_ma, "ibatmax-cool-ma",
				rc, 1);
		OF_PROP_READ(chip, cfg_warm_bat_mv, "warm-bat-mv", rc, 1);
		OF_PROP_READ(chip, cfg_cool_bat_mv, "cool-bat-mv", rc, 1);
		if (rc) {
			pr_err("Error reading battery temp prop rc=%d\n", rc);
			return rc;
		}
	}

	/* Get the btc-disabled property */
	chip->cfg_btc_disabled = of_property_read_bool(
			chip->spmi->dev.of_node, "qcom,btc-disabled");

	/* Get the charging-disabled property */
#ifdef CONFIG_MACH_MSM8939_P1BDSN_GLOBAL_COM
	chip->cfg_charging_disabled = 0;
#else
	chip->cfg_charging_disabled =
		of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,charging-disabled");
#endif
	/* Get the fake-batt-values property */
	chip->cfg_use_fake_battery =
			of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,use-default-batt-values");

	/* Get the float charging property */
	chip->cfg_float_charge =
			of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,float-charge");

	/* Get the charger EOC detect property */
	chip->cfg_charger_detect_eoc =
			of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,charger-detect-eoc");

	/* Get the vbatdet disable property */
	chip->cfg_disable_vbatdet_based_recharge =
			of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,disable-vbatdet-based-recharge");

	/* Get the charger led support property */
	chip->cfg_chgr_led_support =
			of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,chgr-led-support");

	/* Disable charging when faking battery values */
	if (chip->cfg_use_fake_battery)
		chip->cfg_charging_disabled = true;

	chip->cfg_use_external_charger = of_property_read_bool(
			chip->spmi->dev.of_node, "qcom,use-external-charger");

#ifndef CONFIG_LGE_PM
	if (of_find_property(chip->spmi->dev.of_node,
					"qcom,thermal-mitigation",
					&chip->cfg_thermal_levels)) {
		chip->thermal_mitigation = devm_kzalloc(chip->dev,
			chip->cfg_thermal_levels,
			GFP_KERNEL);

		if (chip->thermal_mitigation == NULL) {
			pr_err("thermal mitigation kzalloc() failed.\n");
			return -ENOMEM;
		}

		chip->cfg_thermal_levels /= sizeof(int);
		rc = of_property_read_u32_array(chip->spmi->dev.of_node,
				"qcom,thermal-mitigation",
				chip->thermal_mitigation,
				chip->cfg_thermal_levels);
		if (rc) {
			pr_err("Failed to read threm limits rc = %d\n", rc);
			return rc;
		}
	}
#endif
	return rc;
}

static irqreturn_t qpnp_lbc_usbin_valid_irq_handler(int irq, void *_chip)
{
	struct qpnp_lbc_chip *chip = _chip;
	int usb_present;
	unsigned long flags;

#ifdef CONFIG_LGE_PM
	if ( (chip->uevent_wake_lock.ws.name) != NULL )
		wake_lock_timeout(&chip->uevent_wake_lock, HZ*2);
#endif

	usb_present = qpnp_lbc_is_usb_chg_plugged_in(chip);
#ifdef CONFIG_MACH_MSM8916_YG_SKT_KR
	pr_info("usbin-valid triggered: %d\n", usb_present);
#else
	pr_debug("usbin-valid triggered: %d\n", usb_present);
#endif

#ifdef CONFIG_LGE_PM
	/* Sometimes when USB cable is removed, usb_present value is still true
	* because USB_CHG register does not changed after remove USB cable
	* And then usb_present check one more time when usb_present does not change */
	if (!(chip->usb_present ^ usb_present)) {
		usb_present = qpnp_lbc_is_usb_chg_plugged_in(chip);
		pr_err("need to check usbin-valid triggered: %d\n", usb_present);
	}
#endif

	if (chip->usb_present ^ usb_present) {
		chip->usb_present = usb_present;

#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
	chip->is_charger_changed_from_irq = true;
	cancel_delayed_work(&qpnp_chg->battemp_work);
	schedule_delayed_work(&qpnp_chg->battemp_work, HZ*1);
#endif
		if (!usb_present) {
#ifdef CONFIG_LGE_PM
			if(chip->chg_fail_irq_happen == true)
			{
				qpnp_lbc_charger_enable(chip,CHG_FAIL_IRQ_HAPPEN, 1);
				chip->chg_fail_irq_happen = false;
				wake_unlock(&chip->chg_fail_irq_wake_lock);
			}
#endif
#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE
			if(stpchg_factory_testmode == true)
			{
				qpnp_lbc_charger_enable(chip, FACTORY_TESTMODE, 1);
				stpchg_factory_testmode = false;
			}
			if(start_chg_factory_testmode == true)
				start_chg_factory_testmode = false;
#endif

#ifdef CONFIG_LGE_PM_PWR_KEY_FOR_CHG_LOGO
			pr_err("============ QPNP CHG GONE ==============\n");
			if(lge_get_boot_mode() == LGE_BOOT_MODE_CHARGERLOGO)
			{
				pm_stay_awake(chip->dev);
			}
#endif
			qpnp_lbc_charger_enable(chip, CURRENT, 0);
			spin_lock_irqsave(&chip->ibat_change_lock, flags);
			chip->usb_psy_ma = QPNP_CHG_I_MAX_MIN_90;
			qpnp_lbc_set_appropriate_current(chip);
			spin_unlock_irqrestore(&chip->ibat_change_lock,
								flags);
#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
			pr_info("USB OUT chg wake lock = %d\n", wake_lock_active(&chip->chg_wake_lock));
			if (wake_lock_active(&chip->chg_wake_lock)) {
				pr_info("USB OUT chg_wake_unlock\n");
				wake_unlock(&chip->chg_wake_lock);
			}

			pr_info("USB OUT lcs wake lock = %d\n", wake_lock_active(&chip->lcs_wake_lock));
			if (wake_lock_active(&chip->lcs_wake_lock)) {
				pr_info("USB OUT lcs_wake_unlock\n");
				wake_unlock(&chip->lcs_wake_lock);
			}
#endif
		} else {
			/*
			 * Override VBAT_DET comparator to start charging
			 * even if VBAT > VBAT_DET.
			 */
			if (!chip->cfg_disable_vbatdet_based_recharge)
				qpnp_lbc_vbatdet_override(chip, OVERRIDE_0);

			/*
			 * Enable SOC based charging to make sure
			 * charging gets enabled on USB insertion
			 * irrespective of battery SOC above resume_soc.
			 */
			qpnp_lbc_charger_enable(chip, SOC, 1);
#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
			pr_info("USB IN wake lock = %d\n", wake_lock_active(&chip->chg_wake_lock));
			if (!wake_lock_active(&chip->chg_wake_lock)) {
				pr_info("USB IN chg_wake_lock\n");
				wake_lock(&chip->chg_wake_lock);
			}
#endif
		}

		pr_debug("Updating usb_psy PRESENT property\n");
		power_supply_set_present(chip->usb_psy, chip->usb_present);
	}

	return IRQ_HANDLED;
}

static int qpnp_lbc_is_batt_temp_ok(struct qpnp_lbc_chip *chip)
{
	u8 reg_val;
	int rc;

	rc = qpnp_lbc_read(chip, chip->bat_if_base + INT_RT_STS_REG,
				&reg_val, 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				chip->bat_if_base + INT_RT_STS_REG, rc);
		return rc;
	}

	return (reg_val & BAT_TEMP_OK_IRQ) ? 1 : 0;
}

static irqreturn_t qpnp_lbc_batt_temp_irq_handler(int irq, void *_chip)
{
	struct qpnp_lbc_chip *chip = _chip;
	int batt_temp_good;

	batt_temp_good = qpnp_lbc_is_batt_temp_ok(chip);
	pr_debug("batt-temp triggered: %d\n", batt_temp_good);

	pr_debug("power supply changed batt_psy\n");
	power_supply_changed(&chip->batt_psy);
	return IRQ_HANDLED;
}

static irqreturn_t qpnp_lbc_batt_pres_irq_handler(int irq, void *_chip)
{
	struct qpnp_lbc_chip *chip = _chip;
	int batt_present;

	batt_present = qpnp_lbc_is_batt_present(chip);
	pr_info("batt-pres triggered: %d\n", batt_present);

	if (chip->batt_present ^ batt_present) {
		chip->batt_present = batt_present;
		pr_debug("power supply changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);

		if ((chip->cfg_cool_bat_decidegc
					|| chip->cfg_warm_bat_decidegc)
					&& batt_present) {
			pr_debug("enabling vadc notifications\n");
			if (qpnp_adc_tm_channel_measure(chip->adc_tm_dev,
						&chip->adc_param))
				pr_err("request ADC error\n");
		} else if ((chip->cfg_cool_bat_decidegc
					|| chip->cfg_warm_bat_decidegc)
					&& !batt_present) {
			qpnp_adc_tm_disable_chan_meas(chip->adc_tm_dev,
					&chip->adc_param);
			pr_debug("disabling vadc notifications\n");
		}
	}
	return IRQ_HANDLED;
}

static irqreturn_t qpnp_lbc_chg_failed_irq_handler(int irq, void *_chip)
{
	struct qpnp_lbc_chip *chip = _chip;
	int rc;
	u8 reg_val = CHG_FAILED_BIT;

	pr_info("====> chg_failed triggered count=%u <=====\n", ++chip->chg_failed_count);

#ifdef CONFIG_LGE_PM
	if (!pseudo_batt_info.mode){
		pr_err("=========== [CHG STOP] ==============\n");
		wake_lock(&chip->chg_fail_irq_wake_lock);
		chip->chg_fail_irq_happen = true;
		qpnp_lbc_charger_enable(chip,CHG_FAIL_IRQ_HAPPEN,0);
	}
#endif
	rc = qpnp_lbc_write(chip, chip->chgr_base + CHG_FAILED_REG,
				&reg_val, 1);
	if (rc)
		pr_err("Failed to write chg_fail clear bit rc=%d\n", rc);

	if (chip->bat_if_base) {
		pr_debug("power supply changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
	}

	return IRQ_HANDLED;
}

static int qpnp_lbc_is_fastchg_on(struct qpnp_lbc_chip *chip)
{
	u8 reg_val;
	int rc;

	rc = qpnp_lbc_read(chip, chip->chgr_base + INT_RT_STS_REG,
				&reg_val, 1);
	if (rc) {
		pr_err("Failed to read interrupt status rc=%d\n", rc);
		return rc;
	}
	pr_debug("charger status %x\n", reg_val);
	return (reg_val & FAST_CHG_ON_IRQ) ? 1 : 0;
}

#define TRIM_PERIOD_NS			(50LL * NSEC_PER_SEC)
static irqreturn_t qpnp_lbc_fastchg_irq_handler(int irq, void *_chip)
{
	ktime_t kt;
	struct qpnp_lbc_chip *chip = _chip;
	bool fastchg_on = false;

	fastchg_on = qpnp_lbc_is_fastchg_on(chip);

	pr_info("FAST_CHG IRQ triggered, fastchg_on: %d\n", fastchg_on);

	if (chip->fastchg_on ^ fastchg_on) {
		chip->fastchg_on = fastchg_on;
		if (fastchg_on) {
			mutex_lock(&chip->chg_enable_lock);
			chip->chg_done = false;
			mutex_unlock(&chip->chg_enable_lock);
			/*
			 * Start alarm timer to periodically calculate
			 * and update VDD_MAX trim value.
			 */
			if (chip->supported_feature_flag &
						VDD_TRIM_SUPPORTED) {
				kt = ns_to_ktime(TRIM_PERIOD_NS);
				alarm_start_relative(&chip->vddtrim_alarm,
							kt);
			}
		}

		if (chip->bat_if_base) {
			pr_debug("power supply changed batt_psy\n");
			power_supply_changed(&chip->batt_psy);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t qpnp_lbc_chg_done_irq_handler(int irq, void *_chip)
{
	struct qpnp_lbc_chip *chip = _chip;

#ifdef CONFIG_MACH_MSM8916_YG_SKT_KR
	pr_info("charging done triggered\n");
#else
	pr_debug("charging done triggered\n");
#endif

	chip->chg_done = true;
	pr_debug("power supply changed batt_psy\n");
	power_supply_changed(&chip->batt_psy);

	return IRQ_HANDLED;
}

static irqreturn_t qpnp_lbc_vbatdet_lo_irq_handler(int irq, void *_chip)
{
	struct qpnp_lbc_chip *chip = _chip;
	int rc;

	pr_info("=== vbatdet-lo triggered ===\n");

	/*
	 * Disable vbatdet irq to prevent interrupt storm when VBAT is
	 * close to VBAT_DET.
	 */
	qpnp_lbc_disable_irq(chip, &chip->irqs[CHG_VBAT_DET_LO]);

	/*
	 * Override VBAT_DET comparator to 0 to fix comparator toggling
	 * near VBAT_DET threshold.
	 */
	qpnp_lbc_vbatdet_override(chip, OVERRIDE_0);

	/*
	 * Battery has fallen below the vbatdet threshold and it is
	 * time to resume charging.
	 */
	rc = qpnp_lbc_charger_enable(chip, SOC, 1);
	if (rc)
		pr_err("Failed to enable charging\n");

	return IRQ_HANDLED;
}

static int qpnp_lbc_is_overtemp(struct qpnp_lbc_chip *chip)
{
	u8 reg_val;
	int rc;

	rc = qpnp_lbc_read(chip, chip->usb_chgpth_base + INT_RT_STS_REG,
				&reg_val, 1);
	if (rc) {
		pr_err("Failed to read interrupt status rc=%d\n", rc);
		return rc;
	}

	pr_debug("OVERTEMP rt status %x\n", reg_val);
	return (reg_val & OVERTEMP_ON_IRQ) ? 1 : 0;
}

static irqreturn_t qpnp_lbc_usb_overtemp_irq_handler(int irq, void *_chip)
{
	struct qpnp_lbc_chip *chip = _chip;
	int overtemp = qpnp_lbc_is_overtemp(chip);

	pr_warn_ratelimited("charger %s temperature limit !!!\n",
					overtemp ? "exceeds" : "within");

	return IRQ_HANDLED;
}

static int qpnp_disable_lbc_charger(struct qpnp_lbc_chip *chip)
{
	int rc;
	u8 reg;

	reg = CHG_FORCE_BATT_ON;
	rc = qpnp_lbc_masked_write(chip, chip->chgr_base + CHG_CTRL_REG,
							CHG_EN_MASK, reg);
	/* disable BTC */
	rc |= qpnp_lbc_masked_write(chip, chip->bat_if_base + BAT_IF_BTC_CTRL,
							BTC_COMP_EN_MASK, 0);
	/* Enable BID and disable THM based BPD */
	reg = BATT_ID_EN | BATT_BPD_OFFMODE_EN;
	rc |= qpnp_lbc_write(chip, chip->bat_if_base + BAT_IF_BPD_CTRL_REG,
								&reg, 1);
	return rc;
}

#define SPMI_REQUEST_IRQ(chip, idx, rc, irq_name, threaded, flags, wake)\
do {									\
	if (rc)								\
		break;							\
	if (chip->irqs[idx].irq) {					\
		if (threaded)						\
			rc = devm_request_threaded_irq(chip->dev,	\
				chip->irqs[idx].irq, NULL,		\
				qpnp_lbc_##irq_name##_irq_handler,	\
				flags, #irq_name, chip);		\
		else							\
			rc = devm_request_irq(chip->dev,		\
				chip->irqs[idx].irq,			\
				qpnp_lbc_##irq_name##_irq_handler,	\
				flags, #irq_name, chip);		\
		if (rc < 0) {						\
			pr_err("Unable to request " #irq_name " %d\n",	\
								rc);	\
		} else {						\
			rc = 0;						\
			if (wake) {					\
				enable_irq_wake(chip->irqs[idx].irq);	\
				chip->irqs[idx].is_wake = true;		\
			}						\
		}							\
	}								\
} while (0)

#define SPMI_GET_IRQ_RESOURCE(chip, rc, resource, idx, name)		\
do {									\
	if (rc)								\
		break;							\
									\
	rc = spmi_get_irq_byname(chip->spmi, resource, #name);		\
	if (rc < 0) {							\
		pr_err("Unable to get irq resource " #name "%d\n", rc);	\
	} else {							\
		chip->irqs[idx].irq = rc;				\
		rc = 0;							\
	}								\
} while (0)

static int qpnp_lbc_request_irqs(struct qpnp_lbc_chip *chip)
{
	int rc = 0;

	SPMI_REQUEST_IRQ(chip, CHG_FAILED, rc, chg_failed, 0,
			IRQF_TRIGGER_RISING, 1);

	SPMI_REQUEST_IRQ(chip, CHG_FAST_CHG, rc, fastchg, 1,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
			| IRQF_ONESHOT, 1);

	SPMI_REQUEST_IRQ(chip, CHG_DONE, rc, chg_done, 0,
			IRQF_TRIGGER_RISING, 0);

	SPMI_REQUEST_IRQ(chip, CHG_VBAT_DET_LO, rc, vbatdet_lo, 0,
			IRQF_TRIGGER_FALLING, 1);

	SPMI_REQUEST_IRQ(chip, BATT_PRES, rc, batt_pres, 1,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
			| IRQF_ONESHOT, 1);

	SPMI_REQUEST_IRQ(chip, BATT_TEMPOK, rc, batt_temp, 0,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, 1);

	SPMI_REQUEST_IRQ(chip, USBIN_VALID, rc, usbin_valid, 0,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, 1);

	SPMI_REQUEST_IRQ(chip, USB_OVER_TEMP, rc, usb_overtemp, 0,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, 0);

	return 0;
}

static int qpnp_lbc_get_irqs(struct qpnp_lbc_chip *chip, u8 subtype,
					struct spmi_resource *spmi_resource)
{
	int rc = 0;

	switch (subtype) {
	case LBC_CHGR_SUBTYPE:
		SPMI_GET_IRQ_RESOURCE(chip, rc, spmi_resource,
						CHG_FAST_CHG, fast-chg-on);
		SPMI_GET_IRQ_RESOURCE(chip, rc, spmi_resource,
						CHG_FAILED, chg-failed);
		if (!chip->cfg_disable_vbatdet_based_recharge)
			SPMI_GET_IRQ_RESOURCE(chip, rc, spmi_resource,
						CHG_VBAT_DET_LO, vbat-det-lo);
		if (chip->cfg_charger_detect_eoc)
			SPMI_GET_IRQ_RESOURCE(chip, rc, spmi_resource,
						CHG_DONE, chg-done);
		break;
	case LBC_BAT_IF_SUBTYPE:
		SPMI_GET_IRQ_RESOURCE(chip, rc, spmi_resource,
						BATT_PRES, batt-pres);
		SPMI_GET_IRQ_RESOURCE(chip, rc, spmi_resource,
						BATT_TEMPOK, bat-temp-ok);
		break;
	case LBC_USB_PTH_SUBTYPE:
		SPMI_GET_IRQ_RESOURCE(chip, rc, spmi_resource,
						USBIN_VALID, usbin-valid);
		SPMI_GET_IRQ_RESOURCE(chip, rc, spmi_resource,
						USB_OVER_TEMP, usb-over-temp);
		break;
	};

	return 0;
}

#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE
static ssize_t at_chg_status_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qpnp_lbc_chip *chip = dev_get_drvdata(dev);
	int r;
	bool b_chg_ok = false;
	int chg_type;

	if (!chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	chg_type = get_prop_charge_type(chip);
	if (chg_type != POWER_SUPPLY_CHARGE_TYPE_NONE) {
		b_chg_ok = true;
		r = snprintf(buf, 3, "%d\n", b_chg_ok);
		pr_info("[Diag] true ! buf = %s, charging=1\n", buf);
	} else {
		b_chg_ok = false;
		r = snprintf(buf, 3, "%d\n", b_chg_ok);
		pr_info("[Diag] false ! buf = %s, charging=0\n", buf);
	}

	return r;
}

static ssize_t at_chg_status_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct qpnp_lbc_chip *chip = dev_get_drvdata(dev);
	int ret = 0;

	if (!count) {
		pr_err("[Diag] count 0 error\n");
		return -EINVAL;
	}

	if (!chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	if (strncmp(buf, "0", 1) == 0) {
		/* stop charging */
		pr_info("[Diag] stop charging\n");
		stpchg_factory_testmode = true;
		start_chg_factory_testmode = false;
		ret =qpnp_lbc_charger_enable(chip, FACTORY_TESTMODE, 0);

	} else if (strncmp(buf, "1", 1) == 0) {
		/* start charging */
		pr_info("[Diag] start charging\n");
		stpchg_factory_testmode = false;
		start_chg_factory_testmode = true;
		ret =qpnp_lbc_charger_enable(chip, FACTORY_TESTMODE, 1);
	}

	if (ret)
		return -EINVAL;
	power_supply_changed(&chip->batt_psy);

	return 1;
}

static ssize_t at_chg_complete_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qpnp_lbc_chip *chip = dev_get_drvdata(dev);
	int guage_level = 0;
	int r = 0;

	if (!chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	guage_level = get_prop_capacity(chip);

	if (guage_level == 100) {
		r = snprintf(buf, 3, "%d\n", 0);
		pr_err("[Diag] buf = %s, gauge == 100\n", buf);
	} else {
		r = snprintf(buf, 3, "%d\n", 1);
		pr_err("[Diag] buf = %s, gauge < 100\n", buf);
	}

	return r;
}

static ssize_t at_chg_complete_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct qpnp_lbc_chip *chip = dev_get_drvdata(dev);
	int ret = 0;

	if (!count) {
		pr_err("[Diag] count 0 error\n");
		return -EINVAL;
	}

	if (!chip) {
		pr_err("called before init\n");
		return -EINVAL;
	}

	if (strncmp(buf, "0", 1) == 0) {
		/* charging not complete */
		pr_info("[Diag] charging not complete start\n");
		stpchg_factory_testmode = false;
		start_chg_factory_testmode = true;
		ret =qpnp_lbc_charger_enable(chip, FACTORY_TESTMODE, 1);
	} else if (strncmp(buf, "1", 1) == 0) {
		/* charging complete */
		pr_info("[Diag] charging complete start\n");
		stpchg_factory_testmode = true;
		start_chg_factory_testmode = false;
		ret =qpnp_lbc_charger_enable(chip, FACTORY_TESTMODE, 0);
	}

	if (ret)
		return -EINVAL;

	return 1;
}
static DEVICE_ATTR(at_charge, 0600, at_chg_status_show, at_chg_status_store);
static DEVICE_ATTR(at_chcomp, 0600, at_chg_complete_show, at_chg_complete_store);

#endif

#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
void pseudo_batt_ibatmax_set(void)
{
	if((pseudo_batt_info.mode == 1)&&(!is_factory_cable())){
		qpnp_lbc_ibatmax_set(qpnp_chg, PSEUDO_BATT_MAX);
	}
#ifdef CONFIG_LGE_THERMALE_CHG_CONTROL
	else {
		qpnp_lbc_ibatmax_set(qpnp_chg,qpnp_chg->chg_current_max);
	}
#endif
}
#endif

/* Get/Set initial state of charger */
static void determine_initial_status(struct qpnp_lbc_chip *chip)
{
	chip->usb_present = qpnp_lbc_is_usb_chg_plugged_in(chip);
	power_supply_set_present(chip->usb_psy, chip->usb_present);
	/*
	 * Set USB psy online to avoid userspace from shutting down if battery
	 * capacity is at zero and no chargers online.
	 */
	if (chip->usb_present)
		power_supply_set_online(chip->usb_psy, 1);
}

#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
static int temp_before;

#ifdef CONFIG_LGE_THERMALE_CHG_CONTROL
static int qpnp_thermal_mitigation;
static int qpnp_set_thermal_chg_current(const char *val, struct kernel_param *kp)
{
	int ret;
	ret = param_set_int(val, kp);
	if (ret) {
		pr_err("error setting value %d\n", ret);
		return ret;
	}

	if (!qpnp_chg) {
		pr_err("called before init\n");
		return ret;
	}

	if (qpnp_thermal_mitigation <= 0) {
		qpnp_chg->chg_current_te = qpnp_chg->chg_current_max;
		pr_info("qpnp_thermal_mitigation=0 : current to %d(mA)\n",
			qpnp_chg->chg_current_te);
	} else {
		qpnp_chg->chg_current_te = qpnp_thermal_mitigation;
		pr_info("qpnp_thermal_mitigation set current to %d(mA)\n",
			qpnp_chg->chg_current_te);
	}

#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
	if ((pseudo_batt_info.mode == 1)&&(!is_factory_cable())) {
		qpnp_chg->chg_current_te = PSEUDO_BATT_MAX;
		pr_info("Battery Fake Mode on thermal_mitigation set current %d(mA)\n",
			qpnp_chg->chg_current_te);
	}
#endif

	cancel_delayed_work_sync(&qpnp_chg->battemp_work);
	schedule_delayed_work(&qpnp_chg->battemp_work, HZ*1);

	return 0;
}
module_param_call(qpnp_thermal_mitigation, qpnp_set_thermal_chg_current,
	param_get_uint, &qpnp_thermal_mitigation, 0644);
#endif

static void qpnp_lbc_monitor_batt_temp(struct work_struct *work)
{
	struct qpnp_lbc_chip *chip =
		container_of(work, struct qpnp_lbc_chip, battemp_work.work);
	struct charging_info req;
	struct charging_rsp res;
	bool is_changed = false;
	union power_supply_propval ret = {0,};

	chip->batt_psy.get_property(&(chip->batt_psy),
		POWER_SUPPLY_PROP_TEMP, &ret);
	req.batt_temp = ret.intval / 10;
	chip->batt_temp = req.batt_temp;

	chip->batt_psy.get_property(&(chip->batt_psy),
		POWER_SUPPLY_PROP_VOLTAGE_NOW, &ret);
	req.batt_volt = ret.intval;

	chip->batt_psy.get_property(&(chip->batt_psy),
		POWER_SUPPLY_PROP_CURRENT_NOW, &ret);
	req.current_now = ret.intval / 1000;

#ifdef CONFIG_LGE_THERMALE_CHG_CONTROL
	req.chg_current_ma = chip->chg_current_max;
	req.chg_current_te = chip->chg_current_te;
#endif

	// Added this code to update state when charger is reconnected during wait delay.
	if ( chip->is_charger_changed_from_irq == true ){
		req.is_charger_changed = true;
		chip->is_charger_changed_from_irq = false;
	}
	else
		req.is_charger_changed = false;

	req.is_charger = qpnp_lbc_is_fastchg_on(chip);

	lge_monitor_batt_temp(req, &res);
	chip->battemp_chg_state = res.state;

	if (((res.change_lvl != STS_CHE_NONE) && req.is_charger) ||
		(res.force_update == true)) {
		if (res.change_lvl == STS_CHE_NORMAL_TO_DECCUR ||
			( res.state == CHG_BATT_DECCUR_STATE &&
			res.dc_current != DC_CURRENT_DEF &&
			res.change_lvl != STS_CHE_STPCHG_TO_DECCUR )) {
			pr_info("ibatmax_set STS_CHE_NORMAL_TO_DECCUR\n");
			qpnp_lbc_ibatmax_set(chip, res.dc_current);
			qpnp_lbc_charger_enable(chip,
				LGE_CHARGING_TEMP_SCENARIO, !res.disable_chg);
		} else if (res.change_lvl == STS_CHE_NORMAL_TO_STPCHG ||
			( res.state == CHG_BATT_STPCHG_STATE)) {
			wake_lock(&chip->lcs_wake_lock);
			qpnp_lbc_charger_enable(chip,
				LGE_CHARGING_TEMP_SCENARIO, !res.disable_chg);
		} else if (res.change_lvl == STS_CHE_DECCUR_TO_NORMAL) {
#ifdef CONFIG_LGE_THERMALE_CHG_CONTROL
			pr_info("ibatmax_set STS_CHE_DECCUR_TO_NORMAL\n");
			qpnp_lbc_ibatmax_set(chip, res.dc_current);
#else
			qpnp_lbc_ibatmax_set(chip, chip->prev_max_ma);
#endif
		} else if (res.change_lvl == STS_CHE_DECCUR_TO_STPCHG) {
			wake_lock(&chip->lcs_wake_lock);
			qpnp_lbc_charger_enable(chip,
				LGE_CHARGING_TEMP_SCENARIO, !res.disable_chg);
		} else if (res.change_lvl == STS_CHE_STPCHG_TO_NORMAL) {
#ifdef CONFIG_LGE_THERMALE_CHG_CONTROL
			pr_info("ibatmax_set STS_CHE_STPCHG_TO_NORMAL\n");
			qpnp_lbc_ibatmax_set(chip, res.dc_current);
#endif
			qpnp_lbc_charger_enable(chip,
				LGE_CHARGING_TEMP_SCENARIO, !res.disable_chg);
			wake_unlock(&chip->lcs_wake_lock);
		}else if (res.change_lvl == STS_CHE_STPCHG_TO_DECCUR) {
#ifdef CONFIG_LGE_THERMALE_CHG_CONTROL
			pr_info("ibatmax_set STS_CHE_STPCHG_TO_DECCUR\n");
			qpnp_lbc_ibatmax_set(chip, res.dc_current);
#endif
			qpnp_lbc_charger_enable(chip,
				LGE_CHARGING_TEMP_SCENARIO, !res.disable_chg);
			wake_unlock(&chip->lcs_wake_lock);
		}
#ifdef CONFIG_LGE_THERMALE_CHG_CONTROL
		else if (res.force_update == true && res.state == CHG_BATT_NORMAL_STATE &&
			res.dc_current != DC_CURRENT_DEF) {
			pr_info("ibatmax_set CHG_BATT_NORMAL_STATE\n");
			qpnp_lbc_ibatmax_set(chip, res.dc_current);
		}
#endif
	}

	if (chip->pseudo_ui_chg ^ res.pseudo_chg_ui) {
		is_changed = true;
		chip->pseudo_ui_chg = res.pseudo_chg_ui;
	}

	if (chip->btm_state ^ res.btm_state) {
		is_changed = true;
		chip->btm_state = res.btm_state;
	}

	if (temp_before != req.batt_temp) {
		is_changed = true;
		temp_before = req.batt_temp;
	}

	if(!req.is_charger){
		if(wake_lock_active(&chip->lcs_wake_lock))
			wake_unlock(&chip->lcs_wake_lock);
	}

	if (is_changed == true)
		power_supply_changed(&chip->batt_psy);

#if defined (CONFIG_MACH_MSM8916_C90NAS_SPR_US) || defined (CONFIG_MACH_MSM8916_C50_SPR_US)
	if(chip->usb_present) {
		if ((res.state == CHG_BATT_STPCHG_STATE) || (res.state == CHG_BATT_DECCUR_STATE) ||
				(res.state == CHG_BATT_WARNIG_STATE) || (req.batt_temp <= 0))
			schedule_delayed_work(&chip->battemp_work,
				MONITOR_BATTEMP_POLLING_PERIOD / 6);
		else if ((res.state == CHG_BATT_NORMAL_STATE) && (req.batt_temp > 0))
			schedule_delayed_work(&chip->battemp_work,
				MONITOR_BATTEMP_POLLING_PERIOD);
	}

	else
		schedule_delayed_work(&chip->battemp_work,
			MONITOR_BATTEMP_POLLING_PERIOD);
#else
	schedule_delayed_work(&chip->battemp_work,
		MONITOR_BATTEMP_POLLING_PERIOD);
#endif
}
#endif

#ifdef CONFIG_LGE_PM_PWR_KEY_FOR_CHG_LOGO
//u32 lge_get_bl_level(void);

static bool key_filter_end = false;
static bool key_filter_start = false;
static int prev_key_val = 1;
static int prev_key_code = 0;

#define QPNP_PWR_KEY_MONITOR_PERIOD_MS 500
#define KEY_UP_EVENT    0

void qpnp_goto_suspend_for_chg_logo(void)
{
	pr_info("===== [qpnp_goto_suspend_for_chg_logo] PM RELAX !!! ======\n");

	key_filter_start = false;
	key_filter_end = true;

	if (qpnp_chg != NULL) {
		pm_relax(qpnp_chg->dev);
	}
}
EXPORT_SYMBOL(qpnp_goto_suspend_for_chg_logo);


static void qpnp_pwr_key_filter_delay_for_chg_logo(struct work_struct *work)
{
	pr_info("=== Key filter timer expired ======\n");

	key_filter_start = false;
	key_filter_end = true;
}

void qpnp_pwr_key_action_set_for_chg_logo(struct input_dev *dev, unsigned int code, int value)
{
	if (qpnp_chg == NULL) {
		return;
	}
	else {
		pm_stay_awake(qpnp_chg->dev);
	}

	if (value == 0 && code != prev_key_code){
		// We'll receive only one key in onetime.
		pr_info("=== Other key up event is detected. ignore it~\n");
		return;
	}

	// ignore the key down event if key down event is happened within 300ms after key down event
	// 1. KEY DOWN: value is 1
	// 2. KEY UP: value is 0
	if(prev_key_val == KEY_UP_EVENT && key_filter_start && !key_filter_end)
	{
		pr_info("=== Very fast key input. return it~!!! ======\n");
		return;
	}

	if(prev_key_val == KEY_UP_EVENT && value == KEY_UP_EVENT && !key_filter_start)
	{
		pr_info("=== Not matched key pair. return it~!!! ======\n");
		return;
	}

	prev_key_val = value;

	if(key_filter_start && !key_filter_end)
	{
		key_filter_start = false;
		key_filter_end = true;

		cancel_delayed_work_sync(&qpnp_chg->pwr_key_monitor_for_chg_logo);
		return;
	}

	pr_info("=== Key posting code is %d value is %d ======\n", code, value);
	prev_key_code = code;
	input_report_key(dev, code, value);
	input_sync(dev);

	if(value == KEY_UP_EVENT)
	{
		key_filter_start = true;
		key_filter_end = false;
		schedule_delayed_work(&qpnp_chg->pwr_key_monitor_for_chg_logo, msecs_to_jiffies(QPNP_PWR_KEY_MONITOR_PERIOD_MS));
	}
	else
	{
		key_filter_start = false;
		power_supply_changed(&qpnp_chg->batt_psy);
	}
}
EXPORT_SYMBOL(qpnp_pwr_key_action_set_for_chg_logo);
#endif

#define IBAT_TRIM			-300
static void qpnp_lbc_vddtrim_work_fn(struct work_struct *work)
{
#ifndef CONFIG_LGE_PM_BATTERY_EXTERNAL_FUELGAUGE
	int rc, vbat_now_uv, ibat_now;
#else
	int rc, vbat_now_uv;
#endif
	u8 reg_val;
	ktime_t kt;
	struct qpnp_lbc_chip *chip = container_of(work, struct qpnp_lbc_chip,
						vddtrim_work);

	vbat_now_uv = get_prop_battery_voltage_now(chip);
#ifndef CONFIG_LGE_PM_BATTERY_EXTERNAL_FUELGAUGE
	ibat_now = get_prop_current_now(chip) / 1000;
	pr_debug("vbat %d ibat %d capacity %d\n",
			vbat_now_uv, ibat_now, get_prop_capacity(chip));
#else
	pr_debug("vbat %d capacity %d\n",
			vbat_now_uv, get_prop_capacity(chip));
#endif
	/*
	 * Stop trimming under following condition:
	 * USB removed
	 * Charging Stopped
	 */
	if (!qpnp_lbc_is_fastchg_on(chip) ||
			!qpnp_lbc_is_usb_chg_plugged_in(chip)) {
		pr_debug("stop trim charging stopped\n");
		goto exit;
	} else {
		rc = qpnp_lbc_read(chip, chip->chgr_base + CHG_STATUS_REG,
					&reg_val, 1);
		if (rc) {
			pr_err("Failed to read chg status rc=%d\n", rc);
			goto out;
		}

		/*
		 * Update VDD trim voltage only if following conditions are
		 * met:
		 * If charger is in VDD loop AND
		 * If ibat is between 0 ma and -300 ma
		 */
#ifdef CONFIG_LGE_PM_BATTERY_EXTERNAL_FUELGAUGE
		if ((reg_val & CHG_VDD_LOOP_BIT))
#else
		if ((reg_val & CHG_VDD_LOOP_BIT) &&
				((ibat_now < 0) && (ibat_now > IBAT_TRIM)))
#endif
			qpnp_lbc_adjust_vddmax(chip, vbat_now_uv);
	}

out:
	kt = ns_to_ktime(TRIM_PERIOD_NS);
	alarm_start_relative(&chip->vddtrim_alarm, kt);
exit:
	pm_relax(chip->dev);
}

static enum alarmtimer_restart vddtrim_callback(struct alarm *alarm,
					ktime_t now)
{
	struct qpnp_lbc_chip *chip = container_of(alarm, struct qpnp_lbc_chip,
						vddtrim_alarm);

	pm_stay_awake(chip->dev);
	schedule_work(&chip->vddtrim_work);

	return ALARMTIMER_NORESTART;
}
#ifdef CONFIG_MACH_MSM8916_YG_SKT_KR
#define QPNP_DEFAULT_IBAT	1000
#else
#define QPNP_DEFAULT_IBAT	810
#endif
static int qpnp_lbc_probe(struct spmi_device *spmi)
{
	u8 subtype;
	ktime_t kt;
	struct qpnp_lbc_chip *chip;
	struct resource *resource;
	struct spmi_resource *spmi_resource;
	struct power_supply *usb_psy;
#ifdef CONFIG_LGE_PM_FACTORY_CABLE
	unsigned int cable_smem_size;
	unsigned int *p_cable_type;
#endif
	int rc = 0;

	usb_psy = power_supply_get_by_name("usb");
	if (!usb_psy) {
		pr_err("usb supply not found deferring probe\n");
		return -EPROBE_DEFER;
	}

	chip = devm_kzalloc(&spmi->dev, sizeof(struct qpnp_lbc_chip),
				GFP_KERNEL);
	if (!chip) {
		pr_err("memory allocation failed.\n");
		return -ENOMEM;
	}

	chip->usb_psy = usb_psy;
	chip->dev = &spmi->dev;
	chip->spmi = spmi;
	chip->fake_battery_soc = -EINVAL;
	dev_set_drvdata(&spmi->dev, chip);
	device_init_wakeup(&spmi->dev, 1);
	mutex_init(&chip->jeita_configure_lock);
	mutex_init(&chip->chg_enable_lock);
	spin_lock_init(&chip->hw_access_lock);
	spin_lock_init(&chip->ibat_change_lock);
	spin_lock_init(&chip->irq_lock);
	INIT_WORK(&chip->vddtrim_work, qpnp_lbc_vddtrim_work_fn);
	alarm_init(&chip->vddtrim_alarm, ALARM_REALTIME, vddtrim_callback);
#ifdef CONFIG_LGE_PM
	spin_lock_init(&chip->chg_set_lock);
#endif
	/* Get all device-tree properties */
	rc = qpnp_charger_read_dt_props(chip);
	if (rc) {
		pr_err("Failed to read DT properties rc=%d\n", rc);
		return rc;
	}
	get_cable_data_from_dt(spmi->dev.of_node);

	spmi_for_each_container_dev(spmi_resource, spmi) {
		if (!spmi_resource) {
			pr_err("spmi resource absent\n");
			rc = -ENXIO;
			goto fail_chg_enable;
		}

		resource = spmi_get_resource(spmi, spmi_resource,
							IORESOURCE_MEM, 0);
		if (!(resource && resource->start)) {
			pr_err("node %s IO resource absent!\n",
						spmi->dev.of_node->full_name);
			rc = -ENXIO;
			goto fail_chg_enable;
		}

		rc = qpnp_lbc_read(chip, resource->start + PERP_SUBTYPE_REG,
					&subtype, 1);
		if (rc) {
			pr_err("Peripheral subtype read failed rc=%d\n", rc);
			goto fail_chg_enable;
		}

		switch (subtype) {
		case LBC_CHGR_SUBTYPE:
			chip->chgr_base = resource->start;

			/* Get Charger peripheral irq numbers */
			rc = qpnp_lbc_get_irqs(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to get CHGR irqs rc=%d\n", rc);
				goto fail_chg_enable;
			}
			break;
		case LBC_USB_PTH_SUBTYPE:
			chip->usb_chgpth_base = resource->start;
			rc = qpnp_lbc_get_irqs(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to get USB_PTH irqs rc=%d\n",
						rc);
				goto fail_chg_enable;
			}
			break;
		case LBC_BAT_IF_SUBTYPE:
			chip->bat_if_base = resource->start;
			chip->vadc_dev = qpnp_get_vadc(chip->dev, "chg");
			if (IS_ERR(chip->vadc_dev)) {
				rc = PTR_ERR(chip->vadc_dev);
				if (rc != -EPROBE_DEFER)
					pr_err("vadc prop missing rc=%d\n",
							rc);
				goto fail_chg_enable;
			}
			/* Get Charger Batt-IF peripheral irq numbers */
			rc = qpnp_lbc_get_irqs(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to get BAT_IF irqs rc=%d\n", rc);
				goto fail_chg_enable;
			}
			break;
		case LBC_MISC_SUBTYPE:
			chip->misc_base = resource->start;
			break;
		default:
			pr_err("Invalid peripheral subtype=0x%x\n", subtype);
			rc = -EINVAL;
		}
	}

	if (chip->cfg_use_external_charger) {
		pr_warn("Disabling Linear Charger (e-external-charger = 1)\n");
		rc = qpnp_disable_lbc_charger(chip);
		if (rc)
			pr_err("Unable to disable charger rc=%d\n", rc);
		return -ENODEV;
	}

	/* Initialize h/w */
	rc = qpnp_lbc_misc_init(chip);
	if (rc) {
		pr_err("unable to initialize LBC MISC rc=%d\n", rc);
		return rc;
	}
	rc = qpnp_lbc_chg_init(chip);
	if (rc) {
		pr_err("unable to initialize LBC charger rc=%d\n", rc);
		return rc;
	}
	rc = qpnp_lbc_bat_if_init(chip);
	if (rc) {
		pr_err("unable to initialize LBC BAT_IF rc=%d\n", rc);
		return rc;
	}
	rc = qpnp_lbc_usb_path_init(chip);
	if (rc) {
		pr_err("unable to initialize LBC USB path rc=%d\n", rc);
		return rc;
	}

	if (chip->cfg_chgr_led_support) {
		rc = qpnp_lbc_register_chgr_led(chip);
		if (rc) {
			pr_err("unable to register charger led rc=%d\n", rc);
			return rc;
		}
	}

	if (chip->bat_if_base) {
		chip->batt_present = qpnp_lbc_is_batt_present(chip);
		chip->batt_psy.name = "battery";
		chip->batt_psy.type = POWER_SUPPLY_TYPE_BATTERY;
		chip->batt_psy.properties = msm_batt_power_props;
		chip->batt_psy.num_properties =
			ARRAY_SIZE(msm_batt_power_props);
		chip->batt_psy.get_property = qpnp_batt_power_get_property;
		chip->batt_psy.set_property = qpnp_batt_power_set_property;
		chip->batt_psy.property_is_writeable =
			qpnp_batt_property_is_writeable;
		chip->batt_psy.external_power_changed =
			qpnp_batt_external_power_changed;
		chip->batt_psy.supplied_to = pm_batt_supplied_to;
		chip->batt_psy.num_supplicants =
			ARRAY_SIZE(pm_batt_supplied_to);
		rc = power_supply_register(chip->dev, &chip->batt_psy);
		if (rc < 0) {
			pr_err("batt failed to register rc=%d\n", rc);
			goto fail_chg_enable;
		}
	}

	if ((chip->cfg_cool_bat_decidegc || chip->cfg_warm_bat_decidegc)
			&& chip->bat_if_base) {
		chip->adc_param.low_temp = chip->cfg_cool_bat_decidegc;
		chip->adc_param.high_temp = chip->cfg_warm_bat_decidegc;
		chip->adc_param.timer_interval = ADC_MEAS1_INTERVAL_1S;
		chip->adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
		chip->adc_param.btm_ctx = chip;
		chip->adc_param.threshold_notification =
			qpnp_lbc_jeita_adc_notification;
		chip->adc_param.channel = LR_MUX1_BATT_THERM;

		if (get_prop_batt_present(chip)) {
			rc = qpnp_adc_tm_channel_measure(chip->adc_tm_dev,
					&chip->adc_param);
			if (rc) {
				pr_err("request ADC error rc=%d\n", rc);
				goto unregister_batt;
			}
		}
	}

#ifdef CONFIG_LGE_PM_AC_ONLINE
		chip->ac_psy.name = "ac";
		chip->ac_psy.type = POWER_SUPPLY_TYPE_MAINS;
		chip->ac_psy.supplied_to = pm_power_supplied_to;
		chip->ac_psy.num_supplicants = ARRAY_SIZE(pm_power_supplied_to);
		chip->ac_psy.properties = pm_power_props_mains;
		chip->ac_psy.num_properties = ARRAY_SIZE(pm_power_props_mains);
		chip->ac_psy.set_property = qpnp_power_set_property_mains;
		chip->ac_psy.get_property = qpnp_power_get_property_mains;
		chip->ac_psy.property_is_writeable =
				qpnp_power_property_is_writeable;
		rc = power_supply_register(chip->dev, &chip->ac_psy);
		if (rc < 0) {
			pr_err("power_supply_register dc failed rc=%d\n", rc);
			goto unregister_ac;
		}
#endif

#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE
	stpchg_factory_testmode = false;
	start_chg_factory_testmode = false;
#endif
#ifdef CONFIG_LGE_PM
	chip->chg_fail_irq_happen = false;
	qpnp_chg = chip;
#endif

	rc = qpnp_lbc_bat_if_configure_btc(chip);
	if (rc) {
		pr_err("Failed to configure btc rc=%d\n", rc);
		goto unregister_batt;
	}

	/* Get/Set charger's initial status */
	determine_initial_status(chip);

	rc = qpnp_lbc_request_irqs(chip);
	if (rc) {
		pr_err("unable to initialize LBC MISC rc=%d\n", rc);
		goto unregister_batt;
	}

	if (chip->cfg_charging_disabled && !get_prop_batt_present(chip))
		pr_info("Battery absent and charging disabled !!!\n");

	/* Configure initial alarm for VDD trim */
	if ((chip->supported_feature_flag & VDD_TRIM_SUPPORTED) &&
			qpnp_lbc_is_fastchg_on(chip)) {
		kt = ns_to_ktime(TRIM_PERIOD_NS);
		alarm_start_relative(&chip->vddtrim_alarm, kt);
	}
#ifdef CONFIG_LGE_PM_FACTORY_CABLE
	p_cable_type = (unsigned int *)
		(smem_get_entry(SMEM_ID_VENDOR1, &cable_smem_size, 0, 0));

	if (p_cable_type)
		cable_type = *p_cable_type;
	else
		cable_type = 0;
#endif

#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE
	rc = device_create_file(&spmi->dev, &dev_attr_at_charge);
	if (rc < 0) {
		pr_err("%s:File dev_attr_at_charge creation failed: %d\n",
				__func__, rc);
		rc = -ENODEV;
		goto err_at_charge;
	}

	rc = device_create_file(&spmi->dev, &dev_attr_at_chcomp);
	if (rc < 0) {
		pr_err("%s:File dev_attr_at_chcomp creation failed: %d\n",
				__func__, rc);
		rc = -ENODEV;
		goto err_at_chcomp;
	}

#endif

#ifdef CONFIG_LGE_PM_PWR_KEY_FOR_CHG_LOGO
	INIT_DELAYED_WORK(&chip->pwr_key_monitor_for_chg_logo, qpnp_pwr_key_filter_delay_for_chg_logo);
#endif

#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
	chip->batt_temp = DEFAULT_TEMP;
#ifdef CONFIG_LGE_THERMALE_CHG_CONTROL
	if (is_factory_cable())
		chip->chg_current_te = QPNP_LBC_IBATMAX_MAX;
	else
		chip->chg_current_te = QPNP_DEFAULT_IBAT;
	pr_info("probe define chg_curret_te %d(mA)\n", chip->chg_current_te);
	chip->force_ichg_20pct = 0;
#endif

	chip->is_charger_changed_from_irq = false;
	wake_lock_init(&chip->lcs_wake_lock,
		WAKE_LOCK_SUSPEND, "LGE charging scenario");

	wake_lock_init(&chip->chg_wake_lock, WAKE_LOCK_SUSPEND, "qpnp_lbc_chg");

	INIT_DELAYED_WORK(&chip->battemp_work, qpnp_lbc_monitor_batt_temp);
	schedule_delayed_work(&chip->battemp_work,
		MONITOR_BATTEMP_POLLING_PERIOD / 12);
#endif

#ifdef CONFIG_LGE_PM
	wake_lock_init(&chip->uevent_wake_lock, WAKE_LOCK_SUSPEND, "qpnp_lbc_uevent");
	wake_lock_init(&chip->chg_fail_irq_wake_lock, WAKE_LOCK_SUSPEND, "qpnp_lbc_chg_fail_irq");
#endif


	pr_info("Probe chg_dis=%d bpd=%d usb=%d batt_pres=%d batt_volt=%d soc=%d\n",
			chip->cfg_charging_disabled,
			chip->cfg_bpd_detection,
			qpnp_lbc_is_usb_chg_plugged_in(chip),
			get_prop_batt_present(chip),
			get_prop_battery_voltage_now(chip),
			get_prop_capacity(chip));

	return 0;
#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE

	device_remove_file(&spmi->dev, &dev_attr_at_chcomp);
err_at_chcomp:
	device_remove_file(&spmi->dev, &dev_attr_at_charge);
err_at_charge:
#endif

unregister_batt:
	if (chip->bat_if_base)
		power_supply_unregister(&chip->batt_psy);
#ifdef CONFIG_LGE_PM_AC_ONLINE
unregister_ac:
	power_supply_unregister(&chip->ac_psy);
#endif
fail_chg_enable:
	dev_set_drvdata(&spmi->dev, NULL);
	return rc;
}

static int qpnp_lbc_remove(struct spmi_device *spmi)
{
	struct qpnp_lbc_chip *chip = dev_get_drvdata(&spmi->dev);

	if (chip->supported_feature_flag & VDD_TRIM_SUPPORTED) {
		alarm_cancel(&chip->vddtrim_alarm);
		cancel_work_sync(&chip->vddtrim_work);
	}
	if (chip->bat_if_base)
		power_supply_unregister(&chip->batt_psy);
	mutex_destroy(&chip->jeita_configure_lock);
	mutex_destroy(&chip->chg_enable_lock);
	dev_set_drvdata(&spmi->dev, NULL);
	return 0;
}

/*
 * S/W workaround to force VREF_BATT_THERM ON:
 * Switching between aVDD and LDO has h/w issues, forcing VREF_BATT_THM
 * alway ON to prevent switching to aVDD.
 */

static int qpnp_lbc_resume(struct device *dev)
{
	struct qpnp_lbc_chip *chip = dev_get_drvdata(dev);
	int rc = 0;

	if (chip->bat_if_base) {
		rc = qpnp_lbc_masked_write(chip,
			chip->bat_if_base + BAT_IF_VREF_BAT_THM_CTRL_REG,
			VREF_BATT_THERM_FORCE_ON, VREF_BATT_THERM_FORCE_ON);
		if (rc)
			pr_err("Failed to force on VREF_BAT_THM rc=%d\n", rc);
	}

	return rc;
}

static int qpnp_lbc_suspend(struct device *dev)
{
	struct qpnp_lbc_chip *chip = dev_get_drvdata(dev);
	int rc = 0;

	if (chip->bat_if_base) {
		rc = qpnp_lbc_masked_write(chip,
			chip->bat_if_base + BAT_IF_VREF_BAT_THM_CTRL_REG,
			VREF_BATT_THERM_FORCE_ON, VREF_BAT_THM_ENABLED_FSM);
		if (rc)
			pr_err("Failed to set FSM VREF_BAT_THM rc=%d\n", rc);
	}

	return rc;
}

static const struct dev_pm_ops qpnp_lbc_pm_ops = {
	.resume		= qpnp_lbc_resume,
	.suspend	= qpnp_lbc_suspend,
};

static struct of_device_id qpnp_lbc_match_table[] = {
	{ .compatible = QPNP_CHARGER_DEV_NAME, },
	{}
};

static struct spmi_driver qpnp_lbc_driver = {
	.probe		= qpnp_lbc_probe,
	.remove		= qpnp_lbc_remove,
	.driver		= {
		.name		= QPNP_CHARGER_DEV_NAME,
		.owner		= THIS_MODULE,
		.of_match_table	= qpnp_lbc_match_table,
		.pm		= &qpnp_lbc_pm_ops,
	},
};

/*
 * qpnp_lbc_init() - register spmi driver for qpnp-chg
 */
static int __init qpnp_lbc_init(void)
{
	return spmi_driver_register(&qpnp_lbc_driver);
}
module_init(qpnp_lbc_init);

static void __exit qpnp_lbc_exit(void)
{
	spmi_driver_unregister(&qpnp_lbc_driver);
}
module_exit(qpnp_lbc_exit);

MODULE_DESCRIPTION("QPNP Linear charger driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" QPNP_CHARGER_DEV_NAME);
