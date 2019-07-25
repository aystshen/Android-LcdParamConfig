/*********************************************************************************
* Copyright 2019 Bob Shen
* FileName: lcdparamservice.c
* Author: Bob Shen
* Version: 1.0.0
* Date: 2019-3-14
* Description:
*     Monitor the /mnt/external_sd/lcd_parameters. If the parameters has been
*     changed, and then update the lcdparamers. The parameters will work after
*     reboot.
*
* Revision:
*     Date:
*     Reviser:
*     Description:
*********************************************************************************/

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <termio.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <cutils/properties.h>
#include <sys/utsname.h>
#include <cutils/list.h>
#include <cutils/log.h>
#include <cutils/sockets.h>
#include <sys/reboot.h>
#include <cutils/iosched_policy.h>

#define LOG_TAG "LcdParamService"

typedef unsigned short uint16;
typedef unsigned long uint32;
typedef unsigned char uint8;

#define LCDPARAM_FILE_PATH              "busybox find  /mnt/media_rw/ -name lcd_parameters"
#define LCDPARAM_PARTITIOM_NODE_PATH    "/dev/block/platform/ff0f0000.dwmmc/by-name/lcdparam"
#define LCDPARAM_STORGAE_DATA_LEN       2048 // lcdparam size

#define CRC_POLY                        0xEDB88320L // CRC stand
#define CONFIG_MAX                      34

char *key[CONFIG_MAX] = {
    "panel-type",  //1

    "unprepare-delay-ms",
    "enable-delay-ms",
    "disable-delay-ms",
    "prepare-delay-ms",
    "reset-delay-ms",
    "init-delay-ms",
    "width-mm",
    "height-mm",

    "clock-frequency", //10
    "hactive",
    "hfront-porch",
    "hsync-len",
    "hback-porch",
    "vactive",
    "vfront-porch",
    "vsync-len",
    "vback-porch",
    "hsync-active",
    "vsync-active", // 20
    "de-active",
    "pixelclk-active",

    "uboot-init",

    "lvds,format",
    "lvds,mode",
    "lvds,width",
    "lvds,channel",

    "dsi,lane-rate",
    "dsi,flags",
    "dsi,format",
    "dsi,lanes",

    "orientation",
    "density",

    "panel-init-sequence"
};

typedef struct {
    unsigned char data[LCDPARAM_STORGAE_DATA_LEN];
} LCDPARAM_STORGAE_T;

enum {
    OPT_SCAN,
    OPT_READ,
    OPT_WRITE
};

static uint32 crc32_tab[256];
static uint32 nand_crc = 0;

char *strreplace(char *s, char old, char new)
{
    for (; *s; ++s) {
        if (*s == old) {
            *s = new;
        }
    }
    return s;
}

void rknand_print_hex_data(uint8 *s, uint32 * buf, uint32 len)
{
    uint32 i, j, count;
    ALOGE("%s", s);
    for (i = 0; i < len; i += 4) {
        ALOGE("%x %x %x %x", buf[i], buf[i + 1], buf[i + 2], buf[i + 3]);
    }
}

void init_crc32_tab(void)
{
    int i = 0;
    int j = 0;
    uint32 crc = 0;

    for (i = 0; i < 256; i++) {
        crc = (uint32)i;
        for (j = 0; j < 8; j++) {
            if (crc & 0x00000001L) {
                crc = (crc >> 1) ^ CRC_POLY;
            } else {
                crc = crc >> 1;
            }
        }
        crc32_tab[i] = crc;
    }
}

uint32 get_crc32(uint32 crc_init, uint8 *crc_buf, uint32 buf_size)
{
    uint32 crc = crc_init ^ 0xffffffff;

    init_crc32_tab();
    while (buf_size--) {
        crc = (crc >> 8) ^ crc32_tab[(crc & 0xff) ^ *crc_buf++];
    }
    return crc ^ 0xfffffff;
}

uint32 getfile_crc(FILE *fp)
{
    uint32 size = 4 * 1024;
    uint8 crc_buf[size];
    uint32 readln = 0;
    uint crc = 0;

    while ((readln = fread(crc_buf, sizeof(uint8), size, fp)) > 0) {
        crc = get_crc32(crc, crc_buf, readln);
    }

    return crc;
}

/**
* @decs:去除字符串左端空格
* @param: pstr
* @return: char *
*/
char *strtriml(char *pstr)
{
    int i = 0, j;
    j = strlen(pstr) - 1;
    while (isspace(pstr[i]) && (i <= j)) {
        i++;
    }
    if (0 < i) {
        strcpy(pstr, &pstr[i]);
    }
    return pstr;
}

/**
* @decs:去除字符串右端空格
* @param: pstr
* @return: char *
*/
char *strtrimr(char *pstr)
{
    int i;
    i = strlen(pstr) - 1;
    while (isspace(pstr[i]) && (i >= 0)) {
        pstr[i--] = '\0';
    }
    return pstr;
}


/**
* @decs:去除字符串两端空格
* @param: pstr
* @return: char *
*/
char *strtrim(char *pstr)
{
    char *p;
    p = strtrimr(pstr);
    return strtriml(p);
}


char *strdelchr(char *pstr, int chr)
{
    int i = 0;
    int l = 0;
    int ll = 0;
    ll = l = strlen(pstr);

    while (i < l) {
        if (pstr[i] == chr) {
            memmove((pstr + i), (pstr + i + 1), (ll - i - 1));
            pstr[ll - 1] = '\0';
            ll--;
        }
        i++;
    }
    return pstr;
}


void strrmspace(char * str)
{
    char *p1, *p2;
    char ch;
    p1 = str; //first pointer
    p2 = str; //second pointer to the remaining string

    if (p1 == NULL) {
        return;
    }

    while (*p1) {
        if (*p1 != ' ') {
            ch = *p1;
            *p2 = ch;
            p1++;
            p2++;
        } else {
            p1++;
        }
    }
    *p2 = '\0';
}

int key2Index(char *k)
{
    int i;

    for (i = 0; i < CONFIG_MAX; i++) {
        if (strstr(k, key[i])) {
            return i;
        }
    }

    return -1;
}

void sync_properties(char *key, char *value) {
    if (strcmp(key, "orientation") == 0) {
        if (strcmp(value, "0") == 0 || strcmp(value, "90") == 0
		    || strcmp(value, "180") == 0 || strcmp(value, "270") == 0) {
            property_set("persist.sys.sf.hwrotation", value);
		}
    } else if (strcmp(key, "density") == 0) {
        if (strcmp(value, "120") == 0 || strcmp(value, "160") == 0
		    || strcmp(value, "240") == 0 || strcmp(value, "320") == 0) {
            property_set("persist.sys.sf.lcd_density", value);
		}
    }
}

/**
* @decs: 从oem分区读取特定字段参数
* @param: key
* @return: value
*/
uint32 read_param_from_nand(char *k)
{
    int ret = 0;
    int sys_fd;
    int keyIndex = key2Index(k);
    uint32 value = 0;
    LCDPARAM_STORGAE_T sysData;

    if (keyIndex < 0 || keyIndex >= CONFIG_MAX) {
        ALOGE("%s, invalid key[%s]!!!\n", __func__, k);
        return -1;
    }

    if (0 != access(LCDPARAM_PARTITIOM_NODE_PATH, R_OK | W_OK)) {
        ALOGE("%s, access %s failed!!!\n", __func__, LCDPARAM_PARTITIOM_NODE_PATH);
        return -1;
    }

    memset(sysData.data, '\0', sizeof(sysData.data));

    sys_fd = open(LCDPARAM_PARTITIOM_NODE_PATH, O_RDONLY);
    if (sys_fd < 0) {
        ALOGE("%s, open %s failed, err=%d\n", __func__, LCDPARAM_PARTITIOM_NODE_PATH, sys_fd);
        return -1;
    }

    ret = read(sys_fd, (void*)&sysData, sizeof(sysData));
    if (ret < 0) {
        ALOGE("%s, read %s failed, err=%d\n", __func__, LCDPARAM_PARTITIOM_NODE_PATH, sys_fd);
        return -1;
    }

    value = sysData.data[keyIndex * 4];
    value = (value << 8) + sysData.data[keyIndex * 4 + 1];
    value = (value << 8) + sysData.data[keyIndex * 4 + 2];
    value = (value << 8) + sysData.data[keyIndex * 4 + 3];

    close(sys_fd);

    return value;
}

/**
* @decs: 往oem分区特定字段写参数
* @param: key
* @return: value
*/
uint32 write_param_to_nand(char *k, char *v)
{
    int ret = 0;
    int keyIndex = key2Index(k);
    uint32 value = atoi(v);
    unsigned char data[4];
    int sys_fd;

    if (keyIndex < 0 || keyIndex >= CONFIG_MAX) {
        ALOGE("%s, invalid key[%s]!!!\n", __func__, k);
        return -1;
    }

    if (0 != access(LCDPARAM_PARTITIOM_NODE_PATH, R_OK | W_OK)) {
        ALOGE("%s, access %s failed!!!\n", __func__, LCDPARAM_PARTITIOM_NODE_PATH);
        return -1;
    }

    sys_fd = open(LCDPARAM_PARTITIOM_NODE_PATH, O_WRONLY);
    if (sys_fd < 0) {
        ALOGE("%s, open %s failed, err=%d\n", __func__, LCDPARAM_PARTITIOM_NODE_PATH, sys_fd);
        return -1;
    }

    data[0] = (uint8)(value >> 24);
    data[1] = (uint8)(value >> 16);
    data[2] = (uint8)(value >> 8);
    data[3] = (uint8)(value >> 0);

    ret = pwrite(sys_fd, (void*)&data[0], 4, keyIndex * 4);
    if (ret < 0) {
        ALOGE("%s, write %s failed, err=%d\n", __func__, LCDPARAM_PARTITIOM_NODE_PATH, sys_fd);
        return -1;
    }

    close(sys_fd);

    return 0;
}

/**
* @decs: 从oem分区读取crc
* @param:
* @return: crc
*/
uint32  getfile_crc_from_nand(void)
{
    int ret = 0;
    uint32 crc = 0;
    LCDPARAM_STORGAE_T sysData;
    int sys_fd;

    if (0 != access(LCDPARAM_PARTITIOM_NODE_PATH, R_OK | W_OK)) {
        ALOGE("%s, access %s failed!!!\n", __func__, LCDPARAM_PARTITIOM_NODE_PATH);
        return -1;
    }

    memset(sysData.data, '\0', sizeof(sysData.data));

    sys_fd = open(LCDPARAM_PARTITIOM_NODE_PATH, O_RDONLY);
    if (sys_fd < 0) {
        ALOGE("%s, open %s failed, err=%d\n", __func__, LCDPARAM_PARTITIOM_NODE_PATH, sys_fd);
        return -1;
    }

    ret = read(sys_fd, (void*)&sysData, sizeof(sysData));
    if (ret < 0) {
        ALOGE("%s, read %s failed, err=%d\n", __func__, LCDPARAM_PARTITIOM_NODE_PATH, sys_fd);
        return -1;
    }

    for (int i = 0; i < CONFIG_MAX + 1; i++) {
        crc = sysData.data[i * 4];
        crc = (crc << 8) + sysData.data[i * 4 + 1];
        crc = (crc << 8) + sysData.data[i * 4 + 2];
        crc = (crc << 8) + sysData.data[i * 4 + 3];
        ALOGE("%s, %d nand data = 0X%02X%02X%02X%02X", __func__, i, sysData.data[i * 4],
              sysData.data[i * 4 + 1],
              sysData.data[i * 4 + 2],
              sysData.data[i * 4 + 3]);
    }

    crc = sysData.data[LCDPARAM_STORGAE_DATA_LEN - 4];
    crc = (crc << 8) + sysData.data[LCDPARAM_STORGAE_DATA_LEN - 3];
    crc = (crc << 8) + sysData.data[LCDPARAM_STORGAE_DATA_LEN - 2];
    crc = (crc << 8) + sysData.data[LCDPARAM_STORGAE_DATA_LEN - 1];
    ALOGE("%s, nand crc data = 0X%02X%02X%02X%02X", __func__, sysData.data[LCDPARAM_STORGAE_DATA_LEN - 4],
          sysData.data[LCDPARAM_STORGAE_DATA_LEN - 3],
          sysData.data[LCDPARAM_STORGAE_DATA_LEN - 2],
          sysData.data[LCDPARAM_STORGAE_DATA_LEN - 1]);
    close(sys_fd);

    return crc;
}

/**
* @decs: 从sdcard中读取屏参保存到oem分区
* @param:
* @return: 0：success <0: failed
*/
int rk_update_lcd_parameters_from_sdcard(void)
{
    int ret = 0;
    FILE *fp = 0;
    int n = 0;
    char line[20480] = {0};
    int i = 0;
    LCDPARAM_STORGAE_T sysData;
    int sys_fd = 0;
    int oem_fd = 0;
    static uint32 file_crc = 0; //file lcdparameter  crc data
    static int updated = 0; //had store the param into the nand
    static char got_crc = 0; //get file crc flag
    char lcdparameter_buf[128];
    FILE *stream;

    memset(lcdparameter_buf, '\0', sizeof(lcdparameter_buf));
    memset(sysData.data, '\0', sizeof(sysData.data));
    stream = popen(LCDPARAM_FILE_PATH, "r");
    fread(lcdparameter_buf, sizeof(char), sizeof(lcdparameter_buf), stream);
    strreplace(lcdparameter_buf, '\n', '\0');
    pclose(stream);

    while (access(lcdparameter_buf, 0)) {
        if (updated) {
            updated = 0;
        }
        if (got_crc) {
            got_crc = 0;
        }
        return -1;
    }

    if (!updated) {
        if (!got_crc) {
            fp = fopen(lcdparameter_buf, "r");
            if (fp == NULL) {
                ALOGE("%s, open %s failed", __func__, lcdparameter_buf);
                return -1;
            } else {
                ALOGE("%s, open %s succes", __func__, lcdparameter_buf);
            }

            file_crc = getfile_crc(fp);
            got_crc = 1;
            ALOGE("%s, file crc is 0X%08X nand_crc is 0X%08X", __func__, file_crc, nand_crc);

            fclose(fp);
        }

        if ((nand_crc != file_crc)) {
            fp = fopen(lcdparameter_buf, "r");
            if (fp == NULL) {
                ALOGE("%s, open %s failed again", __func__, lcdparameter_buf);
                return -1;
            } else {
                ALOGE("%s, open %s success again", __func__, lcdparameter_buf);
            }

            while (fgets(line, 20480, fp)) {
                char *p = strtrim(line);
                int len = strlen(p);

                if (len <= 0) {
                    continue;
                } else if (p[0] == '#') {
                    continue;
                } else if (!strstr(p, "=") && !strstr(p, ";")) {
                    continue;
                } else {
                    // get key and value string like "screen_lvds_format = 1" spilt by ";"
                    char *key_val_str = strtok(p, ";");
                    char *value = strchr(key_val_str, '=');
                    char cmd[4] = {'\0'};
                    if (value == NULL) {
                        continue;
                    }
                    for (i = 0; i < CONFIG_MAX; i++) {
                        if (strstr(p, key[i])) {
                            char *val = strdelchr(value, '=');
                            strrmspace(val);
                            uint32 config1 = atoi(val);

                            sync_properties(key[i], val);

                            if (strstr(p, "panel-init-sequence")) {
                                int count = 0;
                                int cmdlen = strlen(val) / 2;
                                uint8 cmdu8;

                                sysData.data[i * 4] = (uint8)(cmdlen >> 24);
                                sysData.data[i * 4 + 1] = (uint8)(cmdlen >> 16);
                                sysData.data[i * 4 + 2] = (uint8)(cmdlen >> 8);
                                sysData.data[i * 4 + 3] = (uint8)(cmdlen >> 0);
                                ALOGE("%s, %s=%d val %s", __func__, key[i], config1, val);
                                i ++;

                                while (1) {
                                    cmd [0] = '0';
                                    cmd [1] = 'x';
                                    cmd [2] = *val;
                                    cmd [3] = *(++val);
                                    uint8 cmdu8 = strtol(cmd, NULL, 16);
                                    sysData.data[i * 4 + count++] = cmdu8;

                                    ALOGE("%s, cmd=%d count=%d total=%d", __func__, cmdu8, count, cmdlen);
                                    if (count < cmdlen) {
                                        ++val;    //next cmd
                                    } else {
                                        break;
                                    }
                                }


                            } else {
                                sysData.data[i * 4] = (uint8)(config1 >> 24);
                                sysData.data[i * 4 + 1] = (uint8)(config1 >> 16);
                                sysData.data[i * 4 + 2] = (uint8)(config1 >> 8);
                                sysData.data[i * 4 + 3] = (uint8)(config1 >> 0);
                                ALOGE("%s, %s=%d val %s", __func__, key[i], config1, val);
                            }
                            break;
                        }
                    }

                }

            }
            // file crc data
            sysData.data[LCDPARAM_STORGAE_DATA_LEN - 4] = (uint8)(file_crc >> 24);
            sysData.data[LCDPARAM_STORGAE_DATA_LEN - 3] = (uint8)(file_crc >> 16);
            sysData.data[LCDPARAM_STORGAE_DATA_LEN - 2] = (uint8)(file_crc >> 8);
            sysData.data[LCDPARAM_STORGAE_DATA_LEN - 1] = (uint8)(file_crc >> 0);
            ALOGE("%s, crc32 = 0X%02X%02X%02X%02X", __func__, sysData.data[LCDPARAM_STORGAE_DATA_LEN - 4],
                  sysData.data[LCDPARAM_STORGAE_DATA_LEN - 3],
                  sysData.data[LCDPARAM_STORGAE_DATA_LEN - 2],
                  sysData.data[LCDPARAM_STORGAE_DATA_LEN - 1]);

            if (0 == access(LCDPARAM_PARTITIOM_NODE_PATH, 0)) {
                sys_fd = open(LCDPARAM_PARTITIOM_NODE_PATH, O_WRONLY);
                if (sys_fd < 0) {
                    ALOGE("%s, open %s failed, err=%d\n", __func__, LCDPARAM_PARTITIOM_NODE_PATH, sys_fd);
                    return -1;
                }

                ret = write(sys_fd, (void*)&sysData.data[0], LCDPARAM_STORGAE_DATA_LEN);
                if (ret < 0) {
                    ALOGE("%s, write %s failed, err=%d\n", __func__, LCDPARAM_PARTITIOM_NODE_PATH, sys_fd);
                    return -1;
                }
            }

            close(sys_fd);
            if (ret == -1) {
                ALOGE("%s, save lcdparam failed!!!\n", __func__);
            } else {
                updated = 1;
                nand_crc = file_crc;
                fclose(fp);
                sync();
                reboot(RB_AUTOBOOT);
            }
            fclose(fp);
            return ret;
        }
    }
    return ret;
}

void help()
{
    printf("USAGE: [-srw] [-k key] [-v value]\n");
    printf("WHERE: -s = scan sdcard and udisk\n");
    printf("       -r = read parameter\n");
    printf("       -w = write parameter\n");
    printf("       -k = key\n");
    printf("       -v = value\n\n");
}

int main(int argc, char * argv[])
{
    int ret = 0;
    int ch;
    int opt = OPT_SCAN;
    char key[1024];
    char value[1024];

    ALOGE("%s, go...\n", __func__);

    while ((ch = getopt(argc, argv, "srwk:v:h")) != -1) {
        switch (ch) {
            case 's':
                opt = OPT_SCAN;
                break;

            case 'r':
                opt = OPT_READ;
                break;

            case 'w':
                opt = OPT_WRITE;
                break;

            case 'k':
                strcpy(key, optarg);
                break;

            case 'v':
                strcpy(value, optarg);
                break;

            case 'h':
                help();
                break;

            case '?':
                printf("Unknown option: %c\n\n", (char)optopt);
                help();
                break;
        }
    }

    if (OPT_SCAN == opt) {
        printf("lcdparamservice --> scan\n");
        nand_crc = getfile_crc_from_nand();
        while (1) {
            rk_update_lcd_parameters_from_sdcard();
            usleep(100000);
        }
    } else if (OPT_READ == opt) {
        if (strlen(key) == 0) {
            printf("Missing -k\n\n", key);
            help();
            return -1;
        }
        ret = read_param_from_nand(key);
        if (ret >= 0) {
            printf("%d", ret);
        }
    } else if (OPT_WRITE == opt) {
        if (strlen(key) == 0 || strlen(value) == 0) {
            printf("Missing -k -v\n\n", key);
            help();
            return -1;
        }
        sync_properties(key, value);
        write_param_to_nand(key, value);
    }

    return 0;
}

