/*
* arch/arm/mach-msm/lge/lge_qfprom_access.c
*
* Copyright (C) 2010 LGE, Inc
*
* This software is licensed under the terms of the GNU General Public
* License version 2, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <asm/setup.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/fs.h>
#include <linux/syscalls.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/mutex.h>

//chipset dependency : header location
#include <mach/board_lge.h>
#include <mach/secinfo.h>
#include <mach/qfprom_addr.h>

#define LGE_QFPROM_INTERFACE_NAME "lge-qfprom"
#define DEFENSIVE_LOOP_NUM 3

static u32 qfprom_result_check_data(void);
static u32 qfprom_verification_blow_data(void);
static u32 qfprom_read(u32 fuse_addr);
static u32 qfprom_secdat_read(void);
static u32 qfprom_verify_data(int ret_type);
static u32 qfprom_version_check(void);

static u32 qfprom_address;
static u32 qfprom_lsb_value;
static u32 qfprom_msb_value;

static fuseprov_secdat_type secdat;
static struct mutex secdat_lock;

#define RET_OK 0
#define RET_ERR 1

#define TYPE_QFUSE_CHECK 0
#define TYPE_QFUSE_VERIFICATION 1

static ssize_t sec_read_show(struct device *dev, struct device_attribute *attr, char *buf)
{
  int ret = RET_ERR;

  printk(KERN_INFO "[QFUSE]%s start\n", __func__);

  ret = qfprom_secdat_read();

  printk(KERN_ERR "[QFUSE]%s end\n", __func__);
  ret = (ret)? RET_OK: RET_ERR;
  return sprintf(buf, "%x\n", ret);
}

static ssize_t sec_read_store(struct device *dev, struct device_attribute *attr,
               const char *buf, size_t count)
{
  printk(KERN_INFO "[QFUSE]%s start\n", __func__);
  qfprom_secdat_read();
  printk(KERN_INFO "[QFUSE]%s end\n", __func__);
  return count;
}
static DEVICE_ATTR(sec_read, S_IWUSR | S_IRUGO, sec_read_show, sec_read_store);

static ssize_t qfusing_show(struct device *dev, struct device_attribute *attr, char *buf)
{
  int ret = RET_ERR;

  printk(KERN_INFO "[QFUSE]%s start\n", __func__);

  ret = qfprom_verify_data(TYPE_QFUSE_CHECK);

  printk(KERN_ERR "[QFUSE]%s end\n", __func__);
  ret = (ret)? RET_OK: RET_ERR;
  return sprintf(buf, "%x\n", ret);

}
static DEVICE_ATTR(qfusing, S_IWUSR | S_IRUGO, qfusing_show, NULL);

static ssize_t qfusing_verification_show(struct device *dev, struct device_attribute *attr, char *buf)
{
  int verification_value = 0;

  printk(KERN_INFO "[QFUSE]%s start\n", __func__);

  verification_value = qfprom_verify_data(TYPE_QFUSE_VERIFICATION);

  printk(KERN_ERR "[QFUSE]%s end\n", __func__);
  return sprintf(buf, "%x\n", verification_value);
}
static DEVICE_ATTR(qfusing_verification, S_IWUSR | S_IRUGO, qfusing_verification_show, NULL);


static ssize_t qfuse_result_show(struct device *dev, struct device_attribute *attr, char *buf)
{
  u32 result_value = 0;

  printk(KERN_INFO "[QFUSE]%s start\n", __func__);

  if(qfprom_secdat_read()){
    result_value = 0;
    printk(KERN_ERR "[QFUSE]%s: sec dat read fail \n", __func__);
    printk(KERN_INFO "[QFUSE]%s end\n", __func__);
    return sprintf(buf, "%x\n", result_value);
  }

  result_value = qfprom_result_check_data();
  printk(KERN_INFO "[QFUSE]%s : result_value = %x\n",  __func__, result_value);

  printk(KERN_INFO "[QFUSE]%s end\n", __func__);
  return sprintf(buf, "%x\n", result_value);
}
static DEVICE_ATTR(qresult, S_IWUSR | S_IRUGO, qfuse_result_show, NULL);


static ssize_t qfprom_addr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
  return sprintf(buf, "%x\n", qfprom_address);
}
static ssize_t qfprom_addr_store(struct device *dev, struct device_attribute *attr,
               const char *buf, size_t count)
{
  unsigned long val;
  if (strict_strtoul(buf, 16, &val) < 0)
    return -EINVAL;
  qfprom_address = val;
  return count;
}
static DEVICE_ATTR(addr, S_IWUSR | S_IRUGO, qfprom_addr_show, qfprom_addr_store);

static ssize_t qfprom_read_store(struct device *dev, struct device_attribute *attr,
               const char *buf, size_t count)
{

  printk(KERN_INFO "[QFUSE]%s start\n", __func__);

  if (!qfprom_address) {
    printk(KERN_ERR "[QFUSE]%s: qfprom address is NULL\n", __func__);
    printk(KERN_ERR "[QFUSE]%s end\n", __func__);
    return -EINVAL;
  }

  qfprom_lsb_value = qfprom_read(qfprom_address);
  qfprom_msb_value = qfprom_read(qfprom_address + 4);
  qfprom_address = 0;

  printk(KERN_INFO "[QFUSE]%s end\n", __func__);
  return count;
}
static DEVICE_ATTR(read, S_IWUSR | S_IRUGO, NULL, qfprom_read_store);

static ssize_t qfprom_hwstatus_show(struct device *dev, struct device_attribute *attr, char *buf)
{
  return sprintf(buf, "%x\n", qfprom_read(QFPROM_SEC_HW_KEY));
}

static DEVICE_ATTR(hwstatus,  S_IWUSR | S_IRUGO, qfprom_hwstatus_show, NULL);

static ssize_t qfprom_lsb_show(struct device *dev, struct device_attribute *attr, char *buf)
{
  printk(KERN_INFO "[QFUSE]%s start\n", __func__);
  return sprintf(buf, "%x\n", qfprom_lsb_value);
}

static ssize_t qfprom_lsb_store(struct device *dev, struct device_attribute *attr,
               const char *buf, size_t count)
{
  unsigned long val;
  if (strict_strtoul(buf, 16, &val) < 0)
    return -EINVAL;
  qfprom_lsb_value = val;
  return count;
}
static DEVICE_ATTR(lsb, S_IWUSR | S_IRUGO, qfprom_lsb_show, qfprom_lsb_store);

static ssize_t qfprom_msb_show(struct device *dev, struct device_attribute *attr, char *buf)
{
  printk(KERN_INFO "[QFUSE]%s start\n", __func__);
  return sprintf(buf, "%x\n", qfprom_msb_value);
}

static ssize_t qfprom_msb_store(struct device *dev, struct device_attribute *attr,
               const char *buf, size_t count)
{
  unsigned long val;
  if (strict_strtoul(buf, 16, &val) < 0)
    return -EINVAL;
  qfprom_msb_value = val;
  return count;
}
static DEVICE_ATTR(msb, S_IWUSR | S_IRUGO, qfprom_msb_show, qfprom_msb_store);

static ssize_t qfprom_rbvalue_show(struct device *dev, struct device_attribute *attr, char *buf)
{
  int i;
  char tmpbuf[300] = "";
  if (qfprom_version_check() == -1) {
    sprintf(tmpbuf, "Anti-rollback fuse is not blowed");
    printk(KERN_INFO "[QFUSE]%s : %s\n", __func__, tmpbuf);
    return sprintf(buf, "%s\n", tmpbuf);
  }

  for (i = 0; i < ARRAY_SIZE(version_info); i++) {
    sprintf(tmpbuf, "%s%d ", tmpbuf, version_info[i].cnt);
  }
  printk(KERN_INFO "[QFUSE]%s : %s\n", __func__, tmpbuf);

  return sprintf(buf, "%s\n", tmpbuf);
}

static DEVICE_ATTR(rbv, S_IWUSR | S_IRUGO, qfprom_rbvalue_show, NULL);

static struct attribute *qfprom_attributes[] = {
  &dev_attr_sec_read.attr,
  &dev_attr_qfusing.attr,
  &dev_attr_qfusing_verification.attr,
  &dev_attr_qresult.attr,
  &dev_attr_hwstatus.attr,
  &dev_attr_addr.attr,
  &dev_attr_lsb.attr,
  &dev_attr_msb.attr,
  &dev_attr_read.attr,
  &dev_attr_rbv.attr,
  NULL
};

static const struct attribute_group qfprom_attribute_group = {
  .attrs = qfprom_attributes,
};

static u32 qfprom_verification_blow_data(void)
{
  int i,j;
  u32 fusing_verification = 0;
  fuseprov_qfuse_entry *qfuse;
  qfprom_result_bits *sec;

  printk(KERN_INFO "[QFUSE]%s start\n", __func__);
  for (i = 0 ; i < secdat.list_hdr.fuse_count; i++) {
    qfuse = &secdat.pentry[i];
    switch(qfuse->addr)
    {
      case QFPROM_SPARE_REG19:
      case QFPROM_SEC_HW_KEY_1:
      case QFPROM_SEC_HW_KEY_2:
      case QFPROM_SEC_HW_KEY_3:
      case QFPROM_SEC_HW_KEY_4:
        break;

      case QFPROM_SEC_HW_KEY:
        if((qfprom_read(qfuse->addr)&SEC_KEY_DERIVATION_BLOWN) == SEC_KEY_DERIVATION_BLOWN) {
          printk(KERN_INFO "[QFUSE]%s: 0x%x check complete\n", __func__, qfuse->addr);
          fusing_verification |= (0x1<<QFPROM_RESULT_SEC_HW_KEY);
        }
        break;

      case QFPROM_OEM_CONFIG:
        if (((qfprom_read(qfuse->addr+0)&qfuse->lsb) == qfuse->lsb)&&
            (qfuse->lsb&result_bits[QFPROM_RESULT_OEM_CONFIG].lsb)){
          printk(KERN_INFO "[QFUSE]%s: 0x%x check complete\n", __func__, qfuse->addr);
          fusing_verification |= (0x1<<QFPROM_RESULT_OEM_CONFIG);
          printk(KERN_INFO "[QFUSE]%s: %x fusing_verification\n", __func__, fusing_verification);
        }

        if(result_bits[QFPROM_RESULT_PRODUCT_ID].msb != 0)
        {
          if(((qfprom_read(qfuse->addr+4)&qfuse->msb) == qfuse->msb)&&
             (qfuse->msb&result_bits[QFPROM_RESULT_PRODUCT_ID].msb)){
            printk(KERN_INFO "[QFUSE]%s: 0x%x check complete\n", __func__, qfuse->addr);
            fusing_verification |= (0x1<<QFPROM_RESULT_PRODUCT_ID);
            printk(KERN_INFO "[QFUSE]%s: %x fusing_verification\n", __func__, fusing_verification);
          }
        }else{
          if(((qfprom_read(qfuse->addr+0)&qfuse->lsb) == qfuse->lsb)&&
             (qfuse->lsb&result_bits[QFPROM_RESULT_PRODUCT_ID].lsb)){
            printk(KERN_INFO "[QFUSE]%s: 0x%x check complete\n", __func__, qfuse->addr);
            fusing_verification |= (0x1<<QFPROM_RESULT_PRODUCT_ID);
            printk(KERN_INFO "[QFUSE]%s: %x fusing_verification\n", __func__, fusing_verification);
          }
        }
        break;

      default:
        if (((qfprom_read(qfuse->addr+0)&qfuse->lsb) == qfuse->lsb) &&
            ((qfprom_read(qfuse->addr+4)&qfuse->msb) == qfuse->msb)) {
          printk(KERN_INFO "[QFUSE]%s: 0x%x check complete\n", __func__, qfuse->addr);
          for(j=0; j<ARRAY_SIZE(result_bits); j++) {
            sec  = &result_bits[j];
            if(sec->addr == qfuse->addr){
              if(sec->lsb == 0 && sec->msb == 0) {
                // do nothing
              }
              else {
                if(((sec->lsb==0 || qfuse->lsb&sec->lsb)) &&
                    ((sec->msb==0 || qfuse->msb&sec->msb))) {
                  fusing_verification |= (0x1<<sec->type);
                  printk(KERN_INFO "[QFUSE]%s: %x fusing_verification\n", __func__, fusing_verification);
                }
              }
              break;
            }
          }
        }
        else {
          printk(KERN_INFO "[QFUSE]%s: 0x%x fusing value is not match\n",__func__, qfuse->addr);
        }
        break;
      }
    }
  printk(KERN_INFO "[QFUSE]%s end\n", __func__);
  return fusing_verification;
}

static u32 qfprom_result_check_data(void)
{
  int i,j;
  u32 qfuse_result = 0;
  fuseprov_qfuse_entry *qfuse;
  qfprom_result_bits *sec;

  printk(KERN_INFO "[QFUSE]%s start\n", __func__);

  for (i = 0 ; i < secdat.list_hdr.fuse_count; i++) {
    qfuse = &secdat.pentry[i];
    switch(qfuse->addr)
    {
      case QFPROM_SEC_HW_KEY:
        qfuse_result |= (0x1<<QFPROM_RESULT_SEC_HW_KEY);
        printk(KERN_INFO "[QFUSE]%s: 0x%x check complete\n", __func__, qfuse->addr);
        printk(KERN_INFO "[QFUSE]%s: %x fusing_verification\n", __func__, qfuse_result);
        break;

      case QFPROM_SPARE_REG19:
        break;

      default:
        for (j = 0 ; j < ARRAY_SIZE(result_bits) ; j++) {
          sec  = &result_bits[j];
          if(qfuse->addr == sec->addr) {
            if(sec->lsb == 0 && sec->msb == 0) {
              // do nothing
            }
            else {
              if((sec->lsb==0 || qfuse->lsb&sec->lsb) &&
                 (sec->msb==0 || qfuse->msb&sec->msb)) {
                qfuse_result |= (0x1<<sec->type);
                printk(KERN_INFO "[QFUSE]%s: 0x%x check complete\n", __func__, qfuse->addr);
                printk(KERN_INFO "[QFUSE]%s: %x fusing_verification\n", __func__, qfuse_result);
              }
            }
          }
        }
      break;
    }
  }
  printk(KERN_INFO "[QFUSE]%s end\n", __func__);
  return qfuse_result;
}

static u32 qfprom_read(u32 fuse_addr)
{
  void __iomem *value_addr;
  u32  value;

  printk(KERN_INFO "[QFUSE]%s start\n", __func__);

  if(fuse_addr ==  QFPROM_SEC_HW_KEY){
    value_addr = ioremap(QFPROM_HW_KEY_STATUS, sizeof(u32));
  }else{
    if(fuse_addr == 0){
      printk(KERN_ERR "[QFUSE]%s address is 0\n", __func__);
      return 0;
    }
    value_addr = ioremap(fuse_addr, sizeof(u32));
  }
  value = (u32)readl(value_addr);
  iounmap(value_addr);
  printk(KERN_INFO "[QFUSE]%s address:0x%x, value:0x%x\n", __func__, fuse_addr, value);
  printk(KERN_INFO "[QFUSE]%s end\n", __func__);
  return value;
}

static u32 qfprom_secdat_read(void)
{
  struct file *fp;
  int cnt=0;
  u32 ret = RET_OK;
  mm_segment_t old_fs=get_fs();

#ifdef CONFIG_LGE_QFPROM_SECHASH
  struct crypto_hash *tfm = NULL;
  struct hash_desc desc;
  struct scatterlist sg[FUSEPROV_SEC_STRUCTURE_MAX_NUM];
  char result[32]={0};
  unsigned char temp_buf[4]={0};
  unsigned char config_hash[32]={0};
  int i=0;
  int temp=0;
  int sg_idx=0;
  int segment_size=0;
#else
  printk(KERN_ERR "[QFUSE]%s : CONFIG_LGE_QFPROM_SECHASH is not exist\n", __func__);
  return RET_ERR;
#endif

  printk(KERN_INFO "[QFUSE]%s start\n", __func__);

  mutex_lock(&secdat_lock);
  if(secdat.pentry != NULL){
    printk(KERN_INFO "[QFUSE]%s : secdata file already loaded \n", __func__);
    mutex_unlock(&secdat_lock);
    return RET_OK;
  }


  set_fs(KERNEL_DS);
  fp=filp_open(SEC_PATH, O_RDONLY, S_IRUSR);

  if(IS_ERR(fp)){
    int temp_err=0;
    temp_err =PTR_ERR(fp);
    printk(KERN_ERR "[QFUSE]%s : secdata file open error : %d\n", __func__, temp_err);
    ret = RET_ERR;
    goto err;
  }

  tfm = crypto_alloc_hash("sha256", 0, CRYPTO_ALG_ASYNC);
  if (IS_ERR(tfm)){
    printk(KERN_ERR "[QFUSE]%s :hash alloc error\n", __func__);
    ret = RET_ERR;
    goto err_mem;
  }
  desc.tfm=tfm;
  desc.flags=0;

  if (crypto_hash_init(&desc) != 0){
    printk(KERN_ERR "[QFUSE]%s : hash init error\n", __func__);
    ret = RET_ERR;
    goto err_mem;
  }

  sg_init_table(sg, ARRAY_SIZE(sg));

  fp->f_pos = 0;
  cnt = vfs_read(fp,(char*)&secdat.hdr, sizeof(secdat.hdr),&fp->f_pos);
  if(cnt != sizeof(secdat.hdr)){
    printk(KERN_ERR "[QFUSE]%s : hdr read error\n", __func__);
    ret = RET_ERR;
    goto err_mem;
  }
  sg_set_buf(&sg[sg_idx++], (const char*)&secdat.hdr, sizeof(fuseprov_secdat_hdr_type));

  if(secdat.hdr.revision >= 2 && secdat.hdr.segment_number !=0)
  {
    for(i=0; i < secdat.hdr.segment_number ; i++)
    {
      cnt = vfs_read(fp, (char*)&secdat.segment, sizeof(secdat.segment),&fp->f_pos);
      if(cnt != sizeof(secdat.segment)){
        printk(KERN_ERR "[QFUSE]%s : segment read error\n", __func__);
        ret = RET_ERR;
        goto err_mem;
      }
      sg_set_buf(&sg[sg_idx++], (const char*)&secdat.segment, sizeof(fuseprov_secdat_hdr_segment_type));
    }
    segment_size = cnt;
  }

  cnt = vfs_read(fp, (char*)&secdat.list_hdr, sizeof(secdat.list_hdr),&fp->f_pos);
  if(cnt != sizeof(secdat.list_hdr)){
    printk(KERN_ERR "[QFUSE]%s : list_hdr read error\n", __func__);
    ret = RET_ERR;
    goto err_mem;
  }
  sg_set_buf(&sg[sg_idx++], (const char*)&secdat.list_hdr, sizeof(fuseprov_qfuse_list_hdr_type));

  if(secdat.list_hdr.size > 0 && secdat.list_hdr.fuse_count > 0 && secdat.list_hdr.fuse_count <= FUSEPROV_INFO_MAX_SIZE){
    secdat.pentry = kmalloc(secdat.list_hdr.size, GFP_KERNEL);
    if(secdat.pentry != NULL){
      memset(secdat.pentry, 0, secdat.list_hdr.size);
      cnt = vfs_read(fp, (char *)secdat.pentry, secdat.list_hdr.size,&fp->f_pos);
      if(cnt != secdat.list_hdr.size){
        printk(KERN_ERR "[QFUSE]%s : fuseprov_pentry read error\n", __func__);
        kfree(secdat.pentry);
        ret = RET_ERR;
        goto err_mem;
      }
      sg_set_buf(&sg[sg_idx++], (const char*)secdat.pentry, secdat.list_hdr.size);
    }else{
       printk(KERN_ERR "[QFUSE]%s : kmalloc pentry error\n", __func__);
       ret = RET_ERR;
       goto err_mem;
    }
  }else{
    printk(KERN_ERR "[QFUSE]%s : invalid header", __func__);
    printk(KERN_ERR "[QFUSE]hdr.magic1      : 0x%08X\n", secdat.hdr.magic1);
    printk(KERN_ERR "[QFUSE]   .magic2      : 0x%08X\n", secdat.hdr.magic2);
    printk(KERN_ERR "[QFUSE]   .revision    : 0x%08X\n", secdat.hdr.revision);
    printk(KERN_ERR "[QFUSE]   .size        : 0x%08X\n", secdat.hdr.size);
    printk(KERN_ERR "[QFUSE]   .segment_num : 0x%08X\n", secdat.hdr.segment_number);

    if(secdat.hdr.revision >= 2 && secdat.hdr.segment_number !=0){
      printk(KERN_ERR "[QFUSE]segment.offset    : 0x%08X\n", secdat.segment.offset);
      printk(KERN_ERR "[QFUSE]       .type      : 0x%08X\n", secdat.segment.type);
      printk(KERN_ERR "[QFUSE]       .attribute : 0x%08X\n", secdat.segment.attribute);
    }
    printk(KERN_ERR "[QFUSE]list_hdr.revision : 0x%08X\n", secdat.list_hdr.revision);
    printk(KERN_ERR "[QFUSE]        .size     : 0x%08X\n", secdat.list_hdr.size);
    printk(KERN_ERR "[QFUSE]        .fuse_cnt : 0x%08X, %d\n", secdat.list_hdr.fuse_count, secdat.list_hdr.fuse_count);

    ret = RET_ERR;
    goto err_mem;
  }

  cnt = vfs_read(fp,(char*)&secdat.footer, sizeof(secdat.footer),&fp->f_pos);
  if(cnt != sizeof(secdat.footer)){
    printk(KERN_ERR "[QFUSE]%s : fuseprov_footer read error\n", __func__);
    ret = RET_ERR;
    goto err_mem;
  }
  sg_set_buf(&sg[sg_idx], (const char*)&secdat.footer, sizeof(fuseprov_secdat_footer_type));

  if(crypto_hash_digest(&desc, sg, sizeof(fuseprov_secdat_hdr_type)+segment_size+secdat.hdr.size, result) != 0){
    printk(KERN_ERR "[QFUSE]%s : hash_digest error\n", __func__);
    ret = RET_ERR;
    goto err_mem;
  }

  for(i=0;i<64;i=i+2){
    memset(temp_buf, 0, 4);
    memcpy(temp_buf, CONFIG_LGE_QFPROM_SECHASH+i, 2);
    sscanf(temp_buf, "%x", &temp);
    config_hash[i/2] = temp;
  }

  if(strncmp(result, config_hash, sizeof(result))!=0){
    printk(KERN_ERR "[QFUSE]%s : sec hash different\n", __func__);
    printk(KERN_ERR "[QFUSE]partition hash : %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
          result[0],result[1],result[2],result[3],result[4],result[5],result[6],result[7],
          result[8],result[9],result[10],result[11],result[12],result[13],result[14],result[15],
          result[16],result[17],result[18],result[19],result[20],result[21],result[22],result[23],
          result[24],result[25],result[26],result[27],result[28],result[29],result[30],result[31]);
    printk(KERN_ERR "[QFUSE]config hash : %s\n",CONFIG_LGE_QFPROM_SECHASH);
    ret = RET_ERR;
    goto err_mem;
  }

err_mem:
  if(tfm)
    crypto_free_hash(tfm);

  if(ret == RET_ERR && secdat.pentry){
    kfree(secdat.pentry);
    secdat.pentry=NULL;
  }
  if(fp)
    filp_close(fp, NULL);

err:
  set_fs(old_fs);
  mutex_unlock(&secdat_lock);
  printk(KERN_INFO "[QFUSE]%s end\n", __func__);
  return ret;
}

static u32 qfprom_verify_data(int type)
{
  int i=0;
  int verification_value = 0;
  int result_value = 0;
  int ret=RET_ERR;

  printk(KERN_INFO "[QFUSE]%s start\n", __func__);

  if(qfprom_secdat_read()){
    verification_value = 0;
    printk(KERN_ERR "[QFUSE]%s: secdat read fail \n", __func__);
    printk(KERN_INFO "[QFUSE]%s end\n", __func__);
    if(type){
      /*QFUSE_VERIFICATION*/
      return verification_value;
    }else{
       /*QFUSE_CHECK*/
      return ret;
    }
  }

  if(type){
    /*QFUSE_VERIFICATION*/
    verification_value = qfprom_verification_blow_data();
    printk(KERN_INFO "[QFUSE]verification_blow_value = %x\n", verification_value);
  }else{
    /*QFUSE_CHECK*/
    result_value = qfprom_result_check_data();
    while(i < DEFENSIVE_LOOP_NUM){
      verification_value = qfprom_verification_blow_data();
      printk(KERN_INFO "[QFUSE]verification_blow_value = %x\n", verification_value);
      if(result_value > 0 && verification_value == result_value){
        printk(KERN_INFO "[QFUSE]%s: verification success\n", __func__);
        ret = RET_OK;
        break;
      }else{
        printk(KERN_ERR "[QFUSE]%s: verification fail %d\n", __func__, i+1);
      }
      i++;
    }
  }

  printk(KERN_INFO "[QFUSE]%s end\n", __func__);
  if(type){
    /*QFUSE_VERIFICATION*/
    return verification_value;
  }else{
    /*QFUSE_CHECK*/
    return ret;
  }
}

static u32 qfprom_version_check(void)
{
  int i = 0, j = 0, k = 0;
  u32 v_l = 0, v_m = 0;

  if (((qfprom_read(anti_rollback_enable.addr+0)&anti_rollback_enable.lsb) != anti_rollback_enable.lsb) ||
      ((qfprom_read(anti_rollback_enable.addr+4)&anti_rollback_enable.msb) != anti_rollback_enable.msb))
    return -1;

  for (i = 0; i < ARRAY_SIZE(version_info); i++) {
    for (j = 0; j < ARRAY_SIZE(version_bits); j++) {
      if(version_info[i].type == version_bits[j].type) {
        v_l = qfprom_read(version_bits[j].addr+0) & version_bits[j].lsb;
        v_m = qfprom_read(version_bits[j].addr+4) & version_bits[j].msb;
        for (k = 0; k < 32; k++) {
          if ((v_l & (0x1 << k)) != 0)
            version_info[i].cnt++;
          if ((v_m & (0x1 << k)) != 0)
            version_info[i].cnt++;
        }
      }
    }
  }

  return 0;
}

static int __exit lge_qfprom_interface_remove(struct platform_device *pdev)
{
  if(secdat.pentry){
    printk(KERN_INFO "[QFUSE]%s: free the pentry alloc\n", __func__);
    kfree(secdat.pentry);
    secdat.pentry = NULL;
  }
  return 0;
}

static int __init lge_qfprom_probe(struct platform_device *pdev)
{
  int err;
  printk(KERN_INFO "[QFUSE]%s : qfprom init\n", __func__);
  mutex_init(&secdat_lock);
  err = sysfs_create_group(&pdev->dev.kobj, &qfprom_attribute_group);
  if (err < 0)
    printk(KERN_ERR "[QFUSE]%s: cant create attribute file\n", __func__);
  return err;
}

static struct platform_driver lge_qfprom_driver __refdata = {
  .probe  = lge_qfprom_probe,
  .remove = __exit_p(lge_qfprom_interface_remove),
  .driver = {
  .name = LGE_QFPROM_INTERFACE_NAME,
  .owner = THIS_MODULE,
  },
};

static int __init lge_qfprom_interface_init(void)
{
  return platform_driver_register(&lge_qfprom_driver);
}

static void __exit lge_qfprom_interface_exit(void)
{
  platform_driver_unregister(&lge_qfprom_driver);
}

module_init(lge_qfprom_interface_init);
module_exit(lge_qfprom_interface_exit);

MODULE_DESCRIPTION("LGE QFPROM interface driver");
MODULE_AUTHOR("lg-security@lge.com");
MODULE_LICENSE("GPL");
