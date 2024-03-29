#define MODULE_TAG	"<atmf04>"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/wakelock.h>
#include <linux/unistd.h>
#include <linux/async.h>
#include <linux/in.h>

#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/sort.h>

#include <linux/firmware.h>

#ifdef CONFIG_OF
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#endif

#include "bs_log.h"

#include "atmf04.h"

#define ATMF04_DRV_NAME     "atmf04"

/* I2C Suspend Check */
#define ATMF04_STATUS_RESUME        0
#define ATMF04_STATUS_SUSPEND       1
#define ATMF04_STATUS_QUEUE_WORK    2

/* Calibration Check */

#if defined(CONFIG_MACH_MSM8916_E7IILTE_SPR_US)
#define ATMF04_CRCS_DUTY_LOW        385
#define ATMF04_CRCS_DUTY_HIGH       645
#define ATMF04_CRCS_COUNT           30
#else
#define ATMF04_CRCS_DUTY_LOW        385
#define ATMF04_CRCS_DUTY_HIGH       645
#define ATMF04_CRCS_COUNT           50
#endif

/* I2C Register */

#if defined(CONFIG_MACH_MSM8939_ALTEV2_VZW) || defined(CONFIG_MACH_MSM8939_P1BDSN_GLOBAL_COM) || defined(CONFIG_MACH_MSM8939_P1BC_SPR_US)
#define I2C_ADDR_SSTVT_H            0x01
#define I2C_ADDR_SSTVT_L            0x02
#define I2C_ADDR_TCH_ONOFF_CNT      0x03
#define I2C_ADDR_DIG_FILTER         0x04
#define I2C_ADDR_TEMP_COEF_UP       0x06
#define I2C_ADDR_TEMP_COEF_DN       0x07
#define I2C_ADDR_SAFE_DUTY_CHK      0x0E
#define I2C_ADDR_SYS_CTRL           0x0F
#define I2C_ADDR_SYS_STAT           0x10
#define I2C_ADDR_CR_DUTY_H          0x11
#define I2C_ADDR_CR_DUTY_L          0x12
#define I2C_ADDR_CS_DUTY_H          0x13
#define I2C_ADDR_CS_DUTY_L          0x14
#define I2C_ADDR_PER_H              0x15
#define I2C_ADDR_PER_L              0x16
#define I2C_ADDR_TCH_OUTPUT         0x17
#define I2C_ADDR_PGM_VER_MAIN       0x18
#define I2C_ADDR_PGM_VER_SUB        0x19
#else
#define I2C_ADDR_SSTVT_H            0x01
#define I2C_ADDR_SSTVT_L            0x02
#define I2C_ADDR_TEMP_COEF_UP       0x03
#define I2C_ADDR_TEMP_COEF_DN       0x04
#define I2C_ADDR_DIG_FILTER         0x05
#define I2C_ADDR_TCH_ONOFF_CNT      0x06
#define I2C_ADDR_SAFE_DUTY_CHK      0x07
#define I2C_ADDR_SYS_CTRL           0x08
#define I2C_ADDR_SYS_STAT           0x09
#define I2C_ADDR_CR_DUTY_H          0x0A
#define I2C_ADDR_CR_DUTY_L          0x0B
#define I2C_ADDR_CS_DUTY_H          0x0C
#define I2C_ADDR_CS_DUTY_L          0x0D
#define I2C_ADDR_PER_H              0x0E
#define I2C_ADDR_PER_L              0x0F
#define I2C_ADDR_TCH_OUTPUT         0x10
#define I2C_ADDR_PGM_VER_MAIN       0x16
#define I2C_ADDR_PGM_VER_SUB        0x17
#endif

#if defined(CONFIG_MACH_MSM8939_ALTEV2_VZW)
//Calibration Data Backup/Restore
#define I2C_ADDR_CMD_OPT 					0x7E
#define I2C_ADDR_COMMAND 					0x7F
#define I2C_ADDR_REQ_DATA					0x80
#define CMD_R_CD_DUTY						0x04		//Cal Data Duty Read
#define CMD_R_CD_REF						0x05		//Cal Data Ref Read
#define CMD_W_CD_DUTY						0x84		//Cal Data Duty Read
#define CMD_W_CD_REF						0x85		//Cal Data Ref Read
#define SZ_CALDATA_UNIT 					16
int CalData[4][SZ_CALDATA_UNIT];
#endif

#define BIT_PERCENT_UNIT            8.192
#define MK_INT(X, Y)                (((int)X << 8)+(int)Y)

#define ENABLE_SENSOR_PINS          0
#define DISABLE_SENSOR_PINS         1

#define ON_SENSOR                   1
#define OFF_SENSOR                  2
#define PATH_CAPSENSOR_CAL  "/sns/capsensor_cal.dat"

#if defined(CONFIG_MACH_MSM8939_ALTEV2_VZW) || defined(CONFIG_MACH_MSM8939_P1BDSN_GLOBAL_COM) || defined(CONFIG_MACH_MSM8939_P1BC_SPR_US)
#define CNT_INITCODE                9
const unsigned char InitCodeAddr[CNT_INITCODE] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0E };
const unsigned char InitCodeVal[CNT_INITCODE] = { 0x00, 0x0C, 0x33, 0x0B, 0x08, 0x72, 0x65, 0x06, 0x32 };
#else
#define CNT_INITCODE                7
const unsigned char InitCodeAddr[CNT_INITCODE] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
const unsigned char InitCodeVal[CNT_INITCODE] = { 0x00, 0x0A, 0x69, 0x67, 0x0B, 0x33, 0x1E };
#endif
static struct i2c_driver atmf04_driver;
static struct workqueue_struct *atmf04_workqueue;


#ifdef CONFIG_OF
enum sensor_dt_entry_status {
	DT_REQUIRED,
	DT_SUGGESTED,
	DT_OPTIONAL,
};

enum sensor_dt_entry_type {
	DT_U32,
	DT_GPIO,
	DT_BOOL,
	DT_STRING,
};

struct sensor_dt_to_pdata_map {
	const char			*dt_name;
	void				*ptr_data;
	enum sensor_dt_entry_status status;
	enum sensor_dt_entry_type	type;
	int				default_val;
};
#endif

static struct i2c_client
		*atmf04_i2c_client; /* global i2c_client to support ioctl */

struct atmf04_platform_data {
	int (*init)(struct i2c_client *client);
	void (*exit)(struct i2c_client *client);
	unsigned int irq_gpio;
	unsigned long chip_enable;
	int (*power_on)(struct i2c_client *client, bool on);
	u32 irq_gpio_flags;

	bool i2c_pull_up;

	struct regulator *vcc_ana;
	struct regulator *vcc_dig;
	struct regulator *vcc_i2c;

	u32 vdd_ana_supply_min;
	u32 vdd_ana_supply_max;
	u32 vdd_ana_load_ua;

	u32 input_pins_num; // not include ref sensor pin
	const char *fw_name;
};

struct atmf04_data {
	int (*get_nirq_low)(void);
	struct i2c_client *client;
	struct mutex update_lock;
	struct mutex enable_lock;
	struct delayed_work	dwork;		/* for PS interrupt */
	struct wake_lock ps_wlock;
	struct input_dev *input_dev_cap;
#ifdef CONFIG_OF
	struct atmf04_platform_data *platform_data;
	int irq;
#endif
	unsigned int enable;
	unsigned int sw_mode;
	atomic_t i2c_status;

	unsigned int cap_detection;
	unsigned int touch_out;
};

static bool on_sensor = false;
static bool check_allnear = false;

int get_bit(unsigned short x, int n);

void chg_mode(unsigned char flag, struct i2c_client *client)
{
	if(flag == ON) {
		i2c_smbus_write_byte_data(client, ADDR_EFLA_STS, 0x80);
		PINFO("change_mode : %d\n",i2c_smbus_read_byte_data(client, ADDR_EFLA_STS));
	}
	else {
		i2c_smbus_write_byte_data(client, ADDR_EFLA_STS, 0x00);
		PINFO("change_mode : %d\n",i2c_smbus_read_byte_data(client, ADDR_EFLA_STS));
	}
	mdelay(1);
}

#if defined(CONFIG_MACH_MSM8939_ALTEV2_VZW)
int Backup_CalData(struct i2c_client *client)
{
	int loop, dloop;
    int ret;

	for(loop = 0 ; loop < 2 ; loop++)
	{
		ret = i2c_smbus_write_byte_data(client,I2C_ADDR_CMD_OPT, loop);
        if (ret) {
            PINFO("atmf04: i2c_write_fail \n");
                return ret;
        }
		ret = i2c_smbus_write_byte_data(client,I2C_ADDR_COMMAND, CMD_R_CD_DUTY);
        if (ret) {
            PINFO("atmf04: i2c_write_fail \n");
                return ret;
        }

		mdelay(1); 		//1 ms Delay

		for(dloop = 0; dloop < SZ_CALDATA_UNIT ; dloop++)
			CalData[loop][dloop] = i2c_smbus_read_byte_data(client,I2C_ADDR_REQ_DATA + dloop);
	}

	for(loop = 0 ; loop < 2 ; loop++)
	{
		ret = i2c_smbus_write_byte_data(client,I2C_ADDR_CMD_OPT, loop);
        if (ret) {
            PINFO("atmf04: i2c_write_fail \n");
                return ret;
        }
		ret = i2c_smbus_write_byte_data(client,I2C_ADDR_COMMAND, CMD_R_CD_REF);
        if (ret) {
            PINFO("atmf04: i2c_write_fail \n");
                return ret;
        }

		mdelay(1); 		//1 ms Delay

		for(dloop = 0; dloop < SZ_CALDATA_UNIT ; dloop++)
			CalData[2+loop][dloop] = i2c_smbus_read_byte_data(client,I2C_ADDR_REQ_DATA + dloop);
	}
    if(CalData[0][0] == 0xFF || (CalData[0][0] == 0x00 && CalData[0][1] == 0x00))
    {
        PINFO("atmf04: Invalid cal data, Not back up this value.");
        return -1;
    }

    for(loop =0;loop<2;loop++)
    {
        for(dloop=0;dloop < SZ_CALDATA_UNIT ; dloop++)
			PINFO("atmf04: backup_caldata data[%d][%d] : %d \n",loop,dloop,CalData[loop][dloop]);
    }

    PINFO("atmf04 backup_cal success");
    return 0;
}

int Write_CalData(struct i2c_client *client)
{
	int loop, dloop;
    int ret;

	for(loop = 0 ; loop < 2 ; loop++)
	{
		ret = i2c_smbus_write_byte_data(client,I2C_ADDR_CMD_OPT, loop);
        if (ret) {
            PINFO("atmf04: i2c_write_fail \n");
                return ret;
        }
		ret =i2c_smbus_write_byte_data(client,I2C_ADDR_COMMAND, CMD_W_CD_DUTY);
        if (ret) {
            PINFO("atmf04: i2c_write_fail \n");
                return ret;
        }

		mdelay(1); 		//1 ms Delay

		for(dloop = 0; dloop < SZ_CALDATA_UNIT ; dloop++)
        {
			ret = i2c_smbus_write_byte_data(client,I2C_ADDR_REQ_DATA + dloop, CalData[loop][dloop]);
            if (ret) {
                PINFO("atmf04: i2c_write_fail \n");
                return ret;
            }

        }
	}

	for(loop = 0 ; loop < 2 ; loop++)
	{
		ret = i2c_smbus_write_byte_data(client,I2C_ADDR_CMD_OPT, loop);
        if (ret) {
            PINFO("atmf04: i2c_write_fail \n");
                return ret;
        }
		ret = i2c_smbus_write_byte_data(client,I2C_ADDR_COMMAND, CMD_W_CD_REF);
        if (ret) {
            PINFO("atmf04: i2c_write_fail \n");
                return ret;
        }

		mdelay(1); 		//1 ms Delay

		for(dloop = 0; dloop < SZ_CALDATA_UNIT ; dloop++)
        {
			ret = i2c_smbus_write_byte_data(client,I2C_ADDR_REQ_DATA + dloop, CalData[2+loop][dloop]);
            if (ret) {
                PINFO("atmf04: i2c_write_fail \n");
                return ret;
            }

        }
	}
    return 0;
}

int RestoreProc_CalData(struct atmf04_data *data, struct i2c_client *client)
{
    int loop;
    int ret;
	//Power On
    gpio_direction_output(data->platform_data->chip_enable, 0);
    mdelay(450);


	//Calibration data write
	ret = Write_CalData(client);
    if(ret)
        return ret;

	//Initial code write
    for(loop = 0 ; loop < CNT_INITCODE ; loop++) {
        ret = i2c_smbus_write_byte_data(client, InitCodeAddr[loop], InitCodeVal[loop]);
        if (ret) {
            PINFO("i2c_write_fail[0x%x]\n",InitCodeAddr[loop]);
        }
        PINFO("##[0x%x][0x%x]##\n", InitCodeAddr[loop], i2c_smbus_read_byte_data(client, InitCodeAddr[loop]));
    }

	//E-flash Data Save Command
    i2c_smbus_write_byte_data(client, I2C_ADDR_SYS_CTRL, 0x80);

	mdelay(50);		//50ms delay

	//Software Reset2
	i2c_smbus_write_byte_data(client,I2C_ADDR_SYS_CTRL, 0x02);
    PINFO("atmf04 restore_cal success");
    return 0;
}
#endif
unsigned char chk_done(unsigned int wait_cnt, struct i2c_client *client)
{
	unsigned int trycnt = 0;
	unsigned char rtn;

	do
	{
		if(++trycnt > wait_cnt) {
			PINFO("RTN_TIMEOUT");
			return RTN_TIMEOUT;
		}
		mdelay(1);
		rtn = i2c_smbus_read_byte_data(client, ADDR_EFLA_STS);
	}while((rtn & FLAG_DONE) != FLAG_DONE);

	return RTN_SUCC;
}

unsigned char chk_done_erase(unsigned int wait_cnt, struct i2c_client *client)
{
	unsigned int trycnt = 0;
	unsigned char rtn;

	do
	{
		if(++trycnt > wait_cnt) return RTN_TIMEOUT;

		mdelay(1);
		rtn = i2c_smbus_read_byte_data(client, ADDR_EFLA_STS);
	}while((rtn & FLAG_DONE_ERASE) != FLAG_DONE_ERASE);

	return RTN_SUCC;
}

unsigned char erase_eflash(struct i2c_client *client)
{
	i2c_smbus_write_byte_data(client, ADDR_EFLA_CTRL, CMD_EFL_ERASE_ALL);

	if(chk_done_erase(FL_EFLA_TIMEOUT_CNT, client) == RTN_TIMEOUT)
		return RTN_TIMEOUT; //timeout

	return RTN_SUCC;
}

unsigned char write_eflash_page(unsigned char flag, unsigned char * page_addr, unsigned char * wdata, struct i2c_client *client)
{
	unsigned char paddr[2];

	if(flag != FLAG_FUSE)
	{
		paddr[0] = page_addr[1];
		paddr[1] = page_addr[0];
	}
	else	//Extra User Memory
	{
		paddr[0] = 0x00;
		paddr[1] = 0x00;
	}

	if(flag != FLAG_FUSE)
	{
		i2c_smbus_write_byte_data(client, ADDR_EFLA_CTRL, CMD_EFL_L_WR);
	}
	else
	{
		//Erase
		i2c_smbus_write_byte_data(client, ADDR_EFLA_CTRL, CMD_EUM_ERASE);
		if(chk_done_erase(FL_EFLA_TIMEOUT_CNT, client) == RTN_TIMEOUT) return RTN_TIMEOUT;
		//Write
		i2c_smbus_write_byte_data(client, ADDR_EFLA_CTRL, CMD_EUM_WR);
	}

	if(chk_done(FL_EFLA_TIMEOUT_CNT, client) == RTN_TIMEOUT) return RTN_TIMEOUT;

	return RTN_SUCC;
}

unsigned char read_eflash_page(unsigned char flag, unsigned char * page_addr, unsigned char * rdata, struct i2c_client *client)
{
	unsigned char paddr[2];

	if(flag != FLAG_FUSE)
	{
		paddr[0] = page_addr[1];
		paddr[1] = page_addr[0];
	}
	else	//Extra User Memory
	{
		paddr[0] = 0x00;
		paddr[1] = 0x00;
	}

	if(flag != FLAG_FUSE)
		i2c_smbus_write_byte_data(client, ADDR_EFLA_CTRL, CMD_EFL_RD);
	else
		i2c_smbus_write_byte_data(client, ADDR_EFLA_CTRL, CMD_EUM_RD);

	if(chk_done(FL_EFLA_TIMEOUT_CNT, client) == RTN_TIMEOUT) return RTN_TIMEOUT;

	return RTN_SUCC;
}

static void onoff_sensor(struct atmf04_data *data, int onoff_mode)
{
	int nparse_mode;

    nparse_mode = onoff_mode;
	if (nparse_mode == ENABLE_SENSOR_PINS) {
		gpio_direction_output(data->platform_data->chip_enable, 0);    /*chip_en pin - low : on, high : off*/
		mdelay(250);
    if (!on_sensor)
		enable_irq_wake(data->irq);
    on_sensor = true;
    }
    if (nparse_mode == DISABLE_SENSOR_PINS) {
		gpio_direction_output(data->platform_data->chip_enable, 1);    /*chip_en pin - low : on, high : off*/
    if (on_sensor)
		disable_irq_wake(data->irq);
	on_sensor = false;
        }
}

unsigned char load_firmware(struct atmf04_data *data, struct i2c_client *client, const char *name)
{
	const struct firmware *fw = NULL;
	unsigned char rtn;
	int ret, i, count = 0;
	int max_page;
	unsigned short main_version, sub_version;
	unsigned char page_addr[2];
	unsigned char fw_version, ic_fw_version, page_num;
	int version_addr;
#if defined(CONFIG_MACH_MSM8939_ALTEV2_VZW)
    int restore = 0;
#endif

	gpio_direction_output(data->platform_data->chip_enable, 0);

	ret = request_firmware(&fw, name, &data->client->dev);
	if (ret) {
		PINFO("Unable to open bin [%s]  ret %d\n", name, ret);
		return 1;
	} else {
		PINFO("Open bin [%s] ret : %d \n", name, ret);
	}

	max_page = (fw->size)/SZ_PAGE_DATA;
	version_addr = (fw->size)-SZ_PAGE_DATA;
	fw_version = MK_INT(fw->data[version_addr], fw->data[version_addr+1]);
	page_num = fw->data[version_addr+3];
	PINFO("###########fw version : %d.%d, fw_version : %d, page_num : %d###########\n", fw->data[version_addr], fw->data[version_addr+1], fw_version, page_num);

	mdelay(200);

	main_version = i2c_smbus_read_byte_data(client, I2C_ADDR_PGM_VER_MAIN);
	sub_version = i2c_smbus_read_byte_data(client, I2C_ADDR_PGM_VER_SUB);
	ic_fw_version = MK_INT(main_version, sub_version);
	PINFO("###########ic version : %d.%d, ic_fw_version : %d###########\n", main_version, sub_version, ic_fw_version);

	if (fw_version > ic_fw_version || fw->data[version_addr] > main_version) {
		//mdelay(200);
#if defined(CONFIG_MACH_MSM8939_ALTEV2_VZW)
        if( Backup_CalData(client) == -1)
        {
            restore = 0;
        }
        else
        {
            restore = 1;
        }
#endif
		chg_mode(ON, client);

		rtn = erase_eflash(client);
		if(rtn != RTN_SUCC) {
			PINFO("earse fail\n");
			return rtn;		//earse fail
		}

		while(count < page_num) {
			//PINFO("%d\n",count);
			for(i=0; i < SZ_PAGE_DATA; i++) {
				i2c_smbus_write_byte_data(client, i, fw->data[i + (count*SZ_PAGE_DATA)]);
				//PINFO("%d : %x ",i + (count*SZ_PAGE_DATA),fw->data[i + (count*SZ_PAGE_DATA)]);
			}
			//PINFO("\n");
			page_addr[1] = (unsigned char)((count & 0xFF00) >> 8);
			page_addr[0] = (unsigned char)(count & 0x00FF);

			i2c_smbus_write_byte_data(client, ADDR_EFLA_PAGE_L, page_addr[0]);
			i2c_smbus_write_byte_data(client, ADDR_EFLA_PAGE_H, page_addr[1]);

			/*Eflash write command 0xFC -> 0x01 Write*/
			i2c_smbus_write_byte_data(client, ADDR_EFLA_CTRL, CMD_EFL_L_WR);

			if(chk_done(FL_EFLA_TIMEOUT_CNT, client) == RTN_TIMEOUT)
				return RTN_TIMEOUT;

			count++;
		}
	}
	else {
		PINFO("Not update firmware. Firmware version is lower than ic.(Or same version)\n");
	}
	chg_mode(OFF, client);
	gpio_direction_output(data->platform_data->chip_enable, 1);
#if defined(CONFIG_MACH_MSM8939_ALTEV2_VZW)
	mdelay(10);
    if(restore)
    {
    ret = RestoreProc_CalData(data, client);
    if(ret)
        PINFO("restore fail ret: %d",ret);
    }
#endif
	PINFO("disable ok");

	release_firmware(fw);

	return 0;
}

int write_initcode(struct i2c_client *client)
{
	struct atmf04_data *data = i2c_get_clientdata(client);
	unsigned char loop;
	int en_state;
	int ret = 0;

	en_state = gpio_get_value(data->platform_data->chip_enable);

	if (en_state)
		gpio_set_value(data->platform_data->chip_enable, 0);
#ifdef CONFIG_MACH_MSM8916_E7IILTE_SPR_US
	mdelay(350);
#else
	mdelay(450);
#endif
	for(loop = 0 ; loop < CNT_INITCODE ; loop++) {
	ret = i2c_smbus_write_byte_data(client, InitCodeAddr[loop], InitCodeVal[loop]);
	if (ret) {
			PINFO("i2c_write_fail[0x%x]\n",InitCodeAddr[loop]);
			return ret;
		}
       PINFO("##[0x%x][0x%x]##\n", InitCodeAddr[loop], i2c_smbus_read_byte_data(client, InitCodeAddr[loop]));
	}
	return 0;
}

static bool valid_multiple_input_pins(struct atmf04_data *data)
{
	if (data->platform_data->input_pins_num > 1)
		return true;

	return false;

}

static int write_calibration_data(struct atmf04_data *data, char *filename)
{
	int fd = 0;

	set_fs(KERNEL_DS);
	fd = sys_open(filename, O_WRONLY|O_CREAT, 0664);

	if (fd >= 0) {
		sys_write(fd,0,sizeof(int));
		sys_close(fd);
	}
	PINFO("sys open to save cal.dat\n");

	return 0;
}

static ssize_t atmf04_show_reg(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int loop;

	int ret = 0;

    for (loop = 0; loop < CNT_INITCODE; loop++) {
        PINFO("###### [0x%x][0x%x]###\n", InitCodeAddr[loop], i2c_smbus_read_byte_data(client, InitCodeAddr[loop]));
    }
    return ret;
}

static ssize_t atmf04_show_regproxctrl0(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	PINFO("atmf04_show_regproxctrl0: %d\n",on_sensor);
	if(on_sensor==true)
		return sprintf(buf,"0x%02x\n",0x0C);
	return sprintf(buf,"0x%02x\n",0x00);
}

static ssize_t atmf04_store_reg(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char loop;

	for (loop = 0; loop < CNT_INITCODE; loop++) {
		i2c_smbus_write_byte_data(client, InitCodeAddr[loop], InitCodeVal[loop]);
		PINFO("##[0x%x][0x%x]##\n", InitCodeAddr[loop], InitCodeVal[loop]);
	}
		i2c_smbus_write_byte_data(client, I2C_ADDR_SYS_CTRL, 0x01);
	return count;
}

static ssize_t atmf04_show_proxstatus(struct device *dev,
                     struct device_attribute *attr, char *buf)
{
	int ret ;
    struct atmf04_data *data = dev_get_drvdata(dev);
	ret = gpio_get_value(data->platform_data->irq_gpio);
	return sprintf(buf, "%d\n", ret);
}


static ssize_t atmf04_store_onoffsensor(struct device *dev,
                     struct device_attribute *attr,
                     const char *buf, size_t count)
{
    struct atmf04_data *data = dev_get_drvdata(dev);
	unsigned long val;

	if (strict_strtoul(buf, 0, &val))
		return -EINVAL;

	if(val == ON_SENSOR) {
		onoff_sensor(data,ENABLE_SENSOR_PINS);
		PINFO("ON_SENSOR\n");
	}
	else if (val == OFF_SENSOR) {
		PINFO("OFF_SENSOR\n");
		onoff_sensor(data,DISABLE_SENSOR_PINS);
	}
	return count;
}

static ssize_t atmf04_show_docalibration(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = to_i2c_client(dev);
	unsigned char  safe_duty;

	/* check safe duty for validation of cal*/
	safe_duty = i2c_smbus_read_byte_data(client, I2C_ADDR_SAFE_DUTY_CHK | 0x80);

	return sprintf(buf, "%x\n", safe_duty);
}

static ssize_t atmf04_store_docalibration(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
    struct atmf04_data *data = i2c_get_clientdata(client);
	int ret, init_state;
	unsigned char  safe_duty;

	PINFO("atmf04_store_docalibration\n");
	init_state = write_initcode(client);
	if(init_state)
		return count;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client, I2C_ADDR_SYS_CTRL, 0x0C);
	mutex_unlock(&data->update_lock);
	if(ret)
		PINFO("i2c_write_fail\n");
#ifdef CONFIG_MACH_MSM8916_E7IILTE_SPR_US
	mdelay(350);
#else
	mdelay(450);
#endif

	safe_duty = i2c_smbus_read_byte_data(client, I2C_ADDR_SAFE_DUTY_CHK | 0x80);

	write_calibration_data(data, PATH_CAPSENSOR_CAL);

	return count;
}

static ssize_t atmf04_store_regreset(struct device *dev,
                     struct device_attribute *attr,
                     const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	int ret;

	ret = i2c_smbus_write_byte_data(client, I2C_ADDR_SYS_CTRL, 0x02);
	if(ret)
		PINFO("i2c_write_fail\n");

	return count;
}

int get_bit(unsigned short x, int n) {
	return (x & (1 << n)) >> n;
}

static ssize_t atmf04_show_regproxdata(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	short cs_per[2], cs_per_result;
	short cr_duty[2], cs_duty[2], cr_duty_val, cs_duty_val;
	short tmp, cap_value;
    int nlength = 0;
    char buf_regproxdata[256] = "";
    char buf_line[64] = "";
	unsigned char init_touch_md;

    memset(buf_line, 0, sizeof(buf_line));
    memset(buf_regproxdata, 0, sizeof(buf_regproxdata));

	init_touch_md = i2c_smbus_read_byte_data(client, I2C_ADDR_SYS_STAT);

	cs_per[0] = i2c_smbus_read_byte_data(client,I2C_ADDR_PER_H);
	cs_per[1] = i2c_smbus_read_byte_data(client,I2C_ADDR_PER_L);
	tmp = MK_INT(cs_per[0], cs_per[1]);
	cs_per_result = tmp / 8;    // BIT_PERCENT_UNIT;

	cr_duty[1] = i2c_smbus_read_byte_data(client, I2C_ADDR_CR_DUTY_H);
	cr_duty[0] = i2c_smbus_read_byte_data(client, I2C_ADDR_CR_DUTY_L);
	cr_duty_val = MK_INT(cr_duty[1], cr_duty[0]);

	cs_duty[1] = i2c_smbus_read_byte_data(client, I2C_ADDR_CS_DUTY_H);
	cs_duty[0] = i2c_smbus_read_byte_data(client, I2C_ADDR_CS_DUTY_L);
	cs_duty_val = MK_INT(cs_duty[1], cs_duty[0]);

	cap_value = (int)cs_duty_val * (int)cs_per_result;
	// printk("H: %x L:%x H:%x L:%x\n",cr_duty[1] ,cr_duty[0], cs_duty[1], cs_duty[0]);

    sprintf(buf_line, "[R]%6d %6d %6d %6d %6d\n", get_bit(init_touch_md,1), cs_per_result, cr_duty_val, cs_duty_val, cap_value);
    nlength = strlen(buf_regproxdata);
	strcpy(&buf_regproxdata[nlength], buf_line);

	return sprintf(buf, "%s", buf_regproxdata);
}

static ssize_t atmf04_store_checkallnear(struct device *dev,
                     struct device_attribute *attr,
                     const char *buf, size_t count)
{
    unsigned long val;

    if (strict_strtoul(buf, 0, &val))
        return -EINVAL;

    if (val == 0)
        check_allnear = false;
    else if (val == 1)
        check_allnear = true;

	printk("atmf04_store_checkallnear %d\n",check_allnear);
    return count;
}

static ssize_t atmf04_show_count_inputpins(struct device *dev,
                     struct device_attribute *attr, char *buf)
{
    int count_inputpins = 0;

    struct atmf04_data *data = dev_get_drvdata(dev);

	count_inputpins = data->platform_data->input_pins_num;
	if (count_inputpins > 1) {
		if (valid_multiple_input_pins(data) == false)
			count_inputpins = 1;
	}
    return sprintf(buf, "%d\n", count_inputpins);
}

static ssize_t atmf04_store_firmware(struct device *dev,
                     struct device_attribute *attr,
                     const char *buf, size_t count)
{
	const char *fw_name = NULL;
	struct atmf04_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = to_i2c_client(dev);

	fw_name = data->platform_data->fw_name;
	load_firmware(data, client, fw_name);
	return count;
}

static ssize_t atmf04_show_version(struct device *dev,
		 struct device_attribute *attr, char *buf)
{
	unsigned short main_version, sub_version;
	unsigned char ic_fw_version;
	struct i2c_client *client = to_i2c_client(dev);
    struct atmf04_data *data = dev_get_drvdata(dev);
    char buf_line[64] = "";
    int nlength = 0;
    char buf_regproxdata[256] = "";

    memset(buf_line, 0, sizeof(buf_line));
	onoff_sensor(data,ENABLE_SENSOR_PINS);

	mdelay(200);

	main_version = i2c_smbus_read_byte_data(client, I2C_ADDR_PGM_VER_MAIN);
	sub_version = i2c_smbus_read_byte_data(client, I2C_ADDR_PGM_VER_SUB);
	ic_fw_version = MK_INT(main_version, sub_version);
	PINFO("###########ic version : %d.%d, ic_fw_version : %d###########\n", main_version, sub_version, ic_fw_version);

    sprintf(buf_line, "%d.%d\n",main_version, sub_version);
    nlength = strlen(buf_regproxdata);
	strcpy(&buf_regproxdata[nlength], buf_line);

	return sprintf(buf,"%s", buf_regproxdata);
}

static ssize_t atmf04_show_check_far(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct atmf04_data *data = i2c_get_clientdata(client);
    unsigned char init_touch_md_check;
    short tmp, cs_per[2], cs_per_result, crcs_count;
    short cr_duty[2], cs_duty[2], cr_duty_val, cs_duty_val;

    mutex_lock(&data->update_lock);

    cs_per[0] = i2c_smbus_read_byte_data(client,I2C_ADDR_PER_H);
    cs_per[1] = i2c_smbus_read_byte_data(client,I2C_ADDR_PER_L);
    tmp = MK_INT(cs_per[0], cs_per[1]);
    cs_per_result = tmp / 8;    // BIT_PERCENT_UNIT;

    cr_duty[1] = i2c_smbus_read_byte_data(client, I2C_ADDR_CR_DUTY_H);
    cr_duty[0] = i2c_smbus_read_byte_data(client, I2C_ADDR_CR_DUTY_L);
    cr_duty_val = MK_INT(cr_duty[1], cr_duty[0]);

    cs_duty[1] = i2c_smbus_read_byte_data(client, I2C_ADDR_CS_DUTY_H);
    cs_duty[0] = i2c_smbus_read_byte_data(client, I2C_ADDR_CS_DUTY_L);
    cs_duty_val = MK_INT(cs_duty[1], cs_duty[0]);

    crcs_count = cr_duty_val - cs_duty_val;

    init_touch_md_check = i2c_smbus_read_byte_data(client, I2C_ADDR_SYS_STAT);

    if(gpio_get_value(data->platform_data->irq_gpio) == 1) {
        if ((get_bit(init_touch_md_check,1) != 1) || (cs_duty_val < ATMF04_CRCS_DUTY_LOW)\
                ||(cr_duty_val <ATMF04_CRCS_DUTY_LOW) || (cs_duty_val > ATMF04_CRCS_DUTY_HIGH)\
                || (cr_duty_val > ATMF04_CRCS_DUTY_HIGH) || (cs_per_result < -1) || (crcs_count > ATMF04_CRCS_COUNT)) {
            printk("1.cal_check : %d, cr_value : %d, cs_value : %d, status : %d Count : %d\n",get_bit(init_touch_md_check,1), cr_duty_val, cs_duty_val, cs_per_result, crcs_count);
            mutex_unlock(&data->update_lock);
            return sprintf(buf,"%d",0);
        }
        else {
            printk("2.cal_check : %d, cr_value : %d, cs_value : %d, status : %d Count : %d\n",get_bit(init_touch_md_check,1), cr_duty_val, cs_duty_val, cs_per_result, crcs_count);
            mutex_unlock(&data->update_lock);
            return sprintf(buf,"%d",1);
        }
    }
    else {
        printk("3.cal_check : %d, cr_value : %d, cs_value : %d, status : %d Count : %d\n",get_bit(init_touch_md_check,1), cr_duty_val, cs_duty_val, cs_per_result, crcs_count);
        mutex_unlock(&data->update_lock);
        return sprintf(buf,"%d",0);
    }
}

static ssize_t atmf04_show_check_mid(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct atmf04_data *data = i2c_get_clientdata(client);
    unsigned char init_touch_md_check;
    short tmp, cs_per[2], cs_per_result, crcs_count;
    short cr_duty[2], cs_duty[2], cr_duty_val, cs_duty_val;

    mutex_lock(&data->update_lock);

    cs_per[0] = i2c_smbus_read_byte_data(client,I2C_ADDR_PER_H);
    cs_per[1] = i2c_smbus_read_byte_data(client,I2C_ADDR_PER_L);
    tmp = MK_INT(cs_per[0], cs_per[1]);
    cs_per_result = tmp / 8;    // BIT_PERCENT_UNIT;

    cr_duty[1] = i2c_smbus_read_byte_data(client, I2C_ADDR_CR_DUTY_H);
    cr_duty[0] = i2c_smbus_read_byte_data(client, I2C_ADDR_CR_DUTY_L);
    cr_duty_val = MK_INT(cr_duty[1], cr_duty[0]);

    cs_duty[1] = i2c_smbus_read_byte_data(client, I2C_ADDR_CS_DUTY_H);
    cs_duty[0] = i2c_smbus_read_byte_data(client, I2C_ADDR_CS_DUTY_L);
    cs_duty_val = MK_INT(cs_duty[1], cs_duty[0]);

    crcs_count = cr_duty_val - cs_duty_val;

    init_touch_md_check = i2c_smbus_read_byte_data(client, I2C_ADDR_SYS_STAT);

    if ((get_bit(init_touch_md_check,1) != 1) || (cs_duty_val < ATMF04_CRCS_DUTY_LOW)\
            ||(cr_duty_val <ATMF04_CRCS_DUTY_LOW) || (cs_duty_val > ATMF04_CRCS_DUTY_HIGH)\
            || (cr_duty_val > ATMF04_CRCS_DUTY_HIGH) || (cs_per_result < -1) || (crcs_count > ATMF04_CRCS_COUNT)) {
        printk("1.cal_check : %d, cr_value : %d, cs_value : %d, status : %d Count : %d\n",get_bit(init_touch_md_check,1), cr_duty_val, cs_duty_val, cs_per_result, crcs_count);
        mutex_unlock(&data->update_lock);
        return sprintf(buf,"%d",0);
    }
    else {
        printk("2.cal_check : %d, cr_value : %d, cs_value : %d, status : %d Count : %d\n",get_bit(init_touch_md_check,1), cr_duty_val, cs_duty_val, cs_per_result, crcs_count);
        mutex_unlock(&data->update_lock);
        return sprintf(buf,"%d",1);
    }
}

static DEVICE_ATTR(onoff,        0664, NULL, atmf04_store_onoffsensor);
static DEVICE_ATTR(proxstatus,   0664, atmf04_show_proxstatus, NULL);
static DEVICE_ATTR(docalibration,0664, atmf04_show_docalibration, atmf04_store_docalibration);
static DEVICE_ATTR(reg_ctrl,     0664, atmf04_show_reg, atmf04_store_reg);
static DEVICE_ATTR(regproxdata,  0664, atmf04_show_regproxdata, NULL);
static DEVICE_ATTR(regreset,     0664, NULL, atmf04_store_regreset);
static DEVICE_ATTR(checkallnear, 0664, NULL, atmf04_store_checkallnear);
static DEVICE_ATTR(cntinputpins, 0664, atmf04_show_count_inputpins, NULL);
static DEVICE_ATTR(regproxctrl0, 0664, atmf04_show_regproxctrl0, NULL);
static DEVICE_ATTR(download,     0664, NULL, atmf04_store_firmware);
static DEVICE_ATTR(version,      0664, atmf04_show_version, NULL);
static DEVICE_ATTR(check_far,    0664, atmf04_show_check_far, NULL);
static DEVICE_ATTR(check_mid,    0664, atmf04_show_check_mid, NULL);


static struct attribute *atmf04_attributes[] = {
    &dev_attr_onoff.attr,
    &dev_attr_docalibration.attr,
    &dev_attr_proxstatus.attr,
    &dev_attr_reg_ctrl.attr,
    &dev_attr_regproxdata.attr,
    &dev_attr_regreset.attr,
    &dev_attr_checkallnear.attr,
    &dev_attr_cntinputpins.attr,
    &dev_attr_regproxctrl0.attr,
	&dev_attr_download.attr,
	&dev_attr_version.attr,
	&dev_attr_check_far.attr,
	&dev_attr_check_mid.attr,
    NULL,
};

static struct attribute_group atmf04_attr_group = {
    .attrs = atmf04_attributes,
};

static void atmf04_reschedule_work(struct atmf04_data *data,
				     unsigned long delay)
{
	int ret;
	struct i2c_client *client = data->client;
	/*
	 * If work is already scheduled then subsequent schedules will not
	 * change the scheduled time that's why we have to cancel it first.
	 */
	dev_err(&client->dev, "atmf04_reschedule_work : set wake lock timeout!\n");
	wake_lock_timeout(&data->ps_wlock, msecs_to_jiffies(1500));
	cancel_delayed_work(&data->dwork);
	ret = queue_delayed_work(atmf04_workqueue, &data->dwork, delay);
	if (ret < 0)
		PINFO("queue_work fail, ret = %d\n", ret);
}

/* assume this is ISR */
static irqreturn_t atmf04_interrupt(int vec, void *info)
{
	struct i2c_client *client = (struct i2c_client *)info;
	struct atmf04_data *data = i2c_get_clientdata(client);
	int tmp = -1;
	tmp = atomic_read(&data->i2c_status);

	dev_err(&client->dev,"atmf04_interrupt\n");
			atmf04_reschedule_work(data, 0);
	return IRQ_HANDLED;
}

static void atmf04_work_handler(struct work_struct *work)
{
	struct atmf04_data *data = container_of(work, struct atmf04_data, dwork.work);
	struct i2c_client *client = data->client;

	data->touch_out = i2c_smbus_read_byte_data(client, I2C_ADDR_TCH_OUTPUT);

	if(data->touch_out) {
//	if ((data->touch_out == 1 ) && (data->cap_datection == 0)) {
		data->cap_detection = 1;

        input_report_abs(data->input_dev_cap, ABS_DISTANCE, 0);/* FAR-to-NEAR detection */
        input_sync(data->input_dev_cap);

		PINFO("far-to-NEAR \n");
	} else {
		data->cap_detection = 0;

        input_report_abs(data->input_dev_cap, ABS_DISTANCE, 1);/* NEAR-to-FAR detection */
        input_sync(data->input_dev_cap);

        PINFO("near-to-FAR \n");
	}
}

static int reg_set_optimum_mode_check(struct regulator *reg, int load_uA)
{
	return (regulator_count_voltages(reg) > 0) ?
	       regulator_set_optimum_mode(reg, load_uA) : 0;
}

static int sensor_regulator_configure(struct atmf04_data *data, bool on)
{
	struct i2c_client *client = data->client;
	struct atmf04_platform_data *pdata = data->platform_data;
	int rc;

	if (on == false)
		goto hw_shutdown;

	pdata->vcc_ana = regulator_get(&client->dev, "Adsemicon,vdd_ana");
	if (IS_ERR(pdata->vcc_ana)) {
		rc = PTR_ERR(pdata->vcc_ana);
		dev_err(&client->dev,
			"Regulator get failed vcc_ana rc=%d\n", rc);
		return rc;
	}

	if (regulator_count_voltages(pdata->vcc_ana) > 0) {
		rc = regulator_set_voltage(pdata->vcc_ana, pdata->vdd_ana_supply_min,
					   pdata->vdd_ana_supply_max);

		if (rc) {
			dev_err(&client->dev,
				"regulator set_vtg failed rc=%d\n", rc);
			goto error_set_vtg_vcc_ana;
		}
	}

	return 0;

error_set_vtg_vcc_ana:
	regulator_put(pdata->vcc_ana);
	return rc;

hw_shutdown:
	if (regulator_count_voltages(pdata->vcc_ana) > 0)
		regulator_set_voltage(pdata->vcc_ana, 0, pdata->vdd_ana_supply_max);

	regulator_put(pdata->vcc_ana);
	regulator_put(pdata->vcc_dig);

	if (pdata->i2c_pull_up) {
	regulator_put(pdata->vcc_i2c);
	}
	return 0;
}

static int sensor_regulator_power_on(struct atmf04_data *data, bool on)
{
	struct i2c_client *client = data->client;
	struct atmf04_platform_data *pdata = data->platform_data;

	int rc;

	if (on == false)
		goto power_off;

	rc = reg_set_optimum_mode_check(pdata->vcc_ana, pdata->vdd_ana_load_ua);
	if (rc < 0) {
		dev_err(&client->dev,
			"Regulator vcc_ana set_opt failed rc=%d\n", rc);
		return rc;
	}

	rc = regulator_enable(pdata->vcc_ana);
	if (rc) {
		dev_err(&client->dev,
			"Regulator vcc_ana enable failed rc=%d\n", rc);
		goto error_reg_en_vcc_ana;
	}
	return 0;

error_reg_en_vcc_ana:
	reg_set_optimum_mode_check(pdata->vcc_ana, 0);
	return rc;

power_off:
	reg_set_optimum_mode_check(pdata->vcc_ana, 0);
	regulator_disable(pdata->vcc_ana);
	if (pdata->i2c_pull_up) {
		reg_set_optimum_mode_check(pdata->vcc_i2c, 0);
		regulator_disable(pdata->vcc_i2c);
	}
	return 0;
}

static int sensor_platform_hw_power_on(struct i2c_client *client, bool on)
{
	sensor_regulator_power_on(i2c_get_clientdata(client), on);
	return 0;
}

static int sensor_platform_hw_init(struct i2c_client *client)
{
	struct atmf04_data *data = i2c_get_clientdata(client);
	int error;

	error = sensor_regulator_configure(data, true);

	error = gpio_request(data->platform_data->chip_enable, "atmf04_chip_enable");
	if(error) {
		PINFO("chip_enable request fail\n");
	}
	gpio_direction_output(data->platform_data->chip_enable, 0);
	PINFO("gpio direction output ok\n");

	if (gpio_is_valid(data->platform_data->irq_gpio)) {
		/* configure touchscreen irq gpio */
		error = gpio_request(data->platform_data->irq_gpio, "atmf04_irq_gpio");
		if (error) {
			dev_err(&client->dev, "unable to request gpio [%d]\n",
				data->platform_data->irq_gpio);
		}
		error = gpio_direction_input(data->platform_data->irq_gpio);
		if (error) {
			dev_err(&client->dev,
				"unable to set direction for gpio [%d]\n",
				data->platform_data->irq_gpio);
		}
		data->irq = client->irq = gpio_to_irq(data->platform_data->irq_gpio);
	} else {
		dev_err(&client->dev, "irq gpio not provided\n");
	}
	PINFO("sensor_platform_hw_init entered\n");
	return 0;
}

static void sensor_platform_hw_exit(struct i2c_client *client)
{
	struct atmf04_data *data = i2c_get_clientdata(client);;

	sensor_regulator_configure(data, false);

	if (gpio_is_valid(data->platform_data->irq_gpio))
		gpio_free(data->platform_data->irq_gpio);
	PINFO("sensor_platform_hw_exit entered\n");
}

static int sensor_parse_dt(struct device *dev,
			   struct atmf04_platform_data *pdata)
{
	struct device_node *np = dev->of_node;

	int ret, err = 0;
	struct sensor_dt_to_pdata_map *itr;
	struct sensor_dt_to_pdata_map map[] = {
		{"Adsemicon,irq-gpio",		&pdata->irq_gpio,		DT_REQUIRED,	DT_GPIO,	0},
		{"Adsemicon,vdd_ana_supply_min",	&pdata->vdd_ana_supply_min,	DT_SUGGESTED,	DT_U32,		0},
		{"Adsemicon,vdd_ana_supply_max",	&pdata->vdd_ana_supply_max,	DT_SUGGESTED,	DT_U32,		0},
		{"Adsemicon,vdd_ana_load_ua",	&pdata->vdd_ana_load_ua,	DT_SUGGESTED,	DT_U32,		0},
		{"Adsemicon,chip_enable",   &pdata->chip_enable,    DT_SUGGESTED,   DT_GPIO,     0},
		{"Adsemicon,InputPinsNum",         &pdata->input_pins_num,      DT_SUGGESTED,   DT_U32,  0},
        {"Adsemicon,fw_name",              &pdata->fw_name,             DT_SUGGESTED,   DT_STRING,  0},
		/* add */
		{NULL,				NULL,				0,		0,		0},
	};

	for (itr = map; itr->dt_name ; ++itr) {
		switch (itr->type) {
		case DT_GPIO:
			ret = of_get_named_gpio(np, itr->dt_name, 0);
			if (ret >= 0) {
				*((int *) itr->ptr_data) = ret;
				ret = 0;
			}
			break;
		case DT_U32:
			ret = of_property_read_u32(np, itr->dt_name,
						   (u32 *) itr->ptr_data);
			break;
		case DT_BOOL:
			*((bool *) itr->ptr_data) =
				of_property_read_bool(np, itr->dt_name);
			ret = 0;
			break;
		case DT_STRING:
			ret = of_property_read_string(np, itr->dt_name, itr->ptr_data);
			break;
		default:
			PINFO("%d is an unknown DT entry type\n",
			      itr->type);
			ret = -EBADE;
		}

		PINFO("DT entry ret:%d name:%s val:%d\n",
		      ret, itr->dt_name, *((int *)itr->ptr_data));

		if (ret) {
			*((int *)itr->ptr_data) = itr->default_val;

			if (itr->status < DT_OPTIONAL) {
				PINFO("Missing '%s' DT entry\n",
				      itr->dt_name);

				/* cont on err to dump all missing entries */
				if (itr->status == DT_REQUIRED && !err)
					err = ret;
			}
		}
	}

	/* set functions of platform data */
	pdata->init = sensor_platform_hw_init;
	pdata->exit = sensor_platform_hw_exit;
	pdata->power_on = sensor_platform_hw_power_on;
	/*pdata->ppcount = 12;	//no need to set, dt_parse */

	return err;

	return 0;
}

static int atmf04_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct atmf04_data *data;
#ifdef CONFIG_OF
	struct atmf04_platform_data *platform_data;
#endif
	int err = 0;

	PINFO("ATMF04 Entered\n");
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE)) {
		return -EIO;
	}

	data = devm_kzalloc(&client->dev, sizeof(struct atmf04_data), GFP_KERNEL);
	if (!data) {
		return -ENOMEM;
	}
	PINFO("ATMF04 probe, kzalloc complete\n");

#ifdef CONFIG_OF
	if (client->dev.of_node) {
		platform_data = devm_kzalloc(&client->dev,
					     sizeof(struct atmf04_platform_data), GFP_KERNEL);
		if (!platform_data) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}
		data->platform_data = platform_data;
		client->dev.platform_data = platform_data;
		err = sensor_parse_dt(&client->dev, platform_data);
		if (err)
			return err;

	} else {
		platform_data = client->dev.platform_data;
	}
#endif
	data->client = client;
	atmf04_i2c_client = client;
	i2c_set_clientdata(client, data);
	data->cap_detection = 0;

#ifdef CONFIG_OF
	/* h/w initialization */
	if (platform_data->init)
		err = platform_data->init(client);

	if (platform_data->power_on)
		err = platform_data->power_on(client, true);
#endif

	client->adapter->retries = 15;

	if (client->adapter->retries == 0)
		goto exit;

	atomic_set(&data->i2c_status, ATMF04_STATUS_RESUME);

	mutex_init(&data->update_lock);
	mutex_init(&data->enable_lock);

	wake_lock_init(&data->ps_wlock, WAKE_LOCK_SUSPEND, "capsensor_wakelock");
	INIT_DELAYED_WORK(&data->dwork, atmf04_work_handler);

	if(request_irq(client->irq, atmf04_interrupt, IRQF_DISABLED|IRQ_TYPE_EDGE_FALLING|IRQ_TYPE_EDGE_RISING,
			ATMF04_DRV_NAME, (void *)client )) {
		PINFO("Could not allocate ATMF04_INT %d !\n", data->irq);
		goto exit_irq_init_failed;
	}

	err = enable_irq_wake(data->irq);

	data->input_dev_cap = input_allocate_device();
	if (!data->input_dev_cap) {
		err = -ENOMEM;
        PINFO("Failed to allocate input device cap !\n");
        goto exit_free_irq;
	}

	set_bit(EV_ABS, data->input_dev_cap->evbit);

	input_set_abs_params(data->input_dev_cap, ABS_DISTANCE, 0, 1, 0, 0);

	data->input_dev_cap->name = ATMF04_DRV_NAME;
	data->input_dev_cap->id.bustype = BUS_I2C;

	err = input_register_device(data->input_dev_cap);
	if (err) {
		err = -ENOMEM;
		PINFO("Unable to register input device cap(%s)\n",
				data->input_dev_cap->name);
        goto exit_free_dev_cap;
    }

    err = sysfs_create_group(&client->dev.kobj, &atmf04_attr_group);
    if (err)
		PINFO("sysfs create fail!\n");
	if (data->platform_data->fw_name) {
		err = load_firmware(data, client, data->platform_data->fw_name);
		if (err) {
			PINFO("Failed to request firmware\n");
			goto exit_free_irq;
		}
	}
	/* default sensor off */
	onoff_sensor(data, DISABLE_SENSOR_PINS);
	PINFO("interrupt is hooked\n");

	return 0;

exit_irq_init_failed:
	wake_lock_destroy(&data->ps_wlock);
	mutex_destroy(&data->update_lock);
	mutex_destroy(&data->enable_lock);
exit_free_dev_cap:
exit_free_irq:
	free_irq(data->irq, client);
exit:
	PINFO("Error");
	return err;
}

static int atmf04_remove(struct i2c_client *client)
{

	struct atmf04_data *data = i2c_get_clientdata(client);
	struct atmf04_platform_data *pdata = data->platform_data;


	disable_irq_wake(client->irq);

	free_irq(client->irq, client);

	if (pdata->power_on)
		pdata->power_on(client, false);

	if (pdata->exit)
		pdata->exit(client);

	mutex_destroy(&data->update_lock);
	mutex_destroy(&data->enable_lock);
	PINFO("ATMF04 remove\n");
	return 0;
}

static const struct i2c_device_id atmf04_id[] = {
	{ "atmf04", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, atmf04_id);

#ifdef CONFIG_OF
static struct of_device_id atmf04_match_table[] = {
	{ .compatible = "adsemicon,atmf04",},
	{ },
};
#else
#define atmf04_match_table NULL
#endif

static struct i2c_driver atmf04_driver = {
	.driver = {
		.name   = ATMF04_DRV_NAME,
		.owner  = THIS_MODULE,
		.of_match_table = atmf04_match_table,
	},
	.probe  = atmf04_probe,
	.remove = atmf04_remove,
	.id_table = atmf04_id,
};

static int __init atmf04_init(void)
{
	PINFO("ATMF04 init Proximity driver: release.\n");
	atmf04_workqueue = create_workqueue("capsensor");
	i2c_add_driver(&atmf04_driver);
	return 0;
}

static void __exit atmf04_exit(void)
{
	PINFO("ATMF04 Proximity driver: release.\n");
	if (atmf04_workqueue)
			destroy_workqueue(atmf04_workqueue);

	atmf04_workqueue = NULL;

	i2c_del_driver(&atmf04_driver);
}

MODULE_DESCRIPTION("ATMF04 cap sensor driver");
MODULE_LICENSE("GPL");

module_init(atmf04_init);
module_exit(atmf04_exit);

