<!--
 * @Description: 
 * @Author: zpw
 * @LastEditors: zpw
 * @Date: 2019-04-19 19:20:39
 * @LastEditTime: 2019-04-19 20:30:07
 -->
# 程序参数管理
对需要保存到Flash的程序参数进行统一管理，提供读取、写入、还原默认值功能，含数据校验。
位置：      /packages/misc/uparam
依赖：      FAL

## 使用示例

原理简介
在应用里面初始化参数表，通过参数指针直接配置参数数据到flash，系统启动后自动读取数据并赋值到参数地址；会自动判断flash储存的值在内容是否存在，以及大小是否一致等等，防止溢出和错误。需要的RAM约每个参数占用5个字节。通过shell命令可以设置参数、复位参数、保存参数等等。

### 定义参数

``` C
#include "uparam.h"

/* 参数1*/
__attribute__((section(".PAR"))) static int pa1;
/* 参数2 */
__attribute__((section(".PAR"))) static float pa2;
/* 参数3 */
__attribute__((section(".PAR"))) static uint16_t pa3[3];
```

### 参数表

``` c
/* 参数表需要定义的数据结构 */
typedef struct
{
    /* 地址 */
    void *address;
    /* 长度 */
    uint8_t size;
    
    /* 参数名称 */
    const char *name;
    
    /* 参数解析的格式 'f'=float，'d'=int，'u'=uint, 
                     'vb'=byte vector,  'vw'=word vector, 
                     'vd'=dword vector, 'vf'=float vector
     */
    const char *type;
    /* 默认参数回调 */
    par_default default_fun;
} param_define_struct;
```

参数定义的结构体

``` C
param_list params[] = {
	{(void *)&pa1, sizeof(pa1), "pa1", "d",  params_default},
    {(void *)&pa2, sizeof(pa2), "pa2", "f",  params_default},
    {(void *)&pa3, sizeof(pa3), "pa3", "vd", params_default}, 
};
```

定义参数的默认值回调

``` C
static void params_default(void *address, uint8_t size)
{
    if ((uint32_t)address == (uint32_t)&pa1)
    {
        pa1 = 0.6f;
    }
	else if ((uint32_t)address == (uint32_t)&pa2)
    {
        pa2 = 0;
    }
    else if ((uint32_t)address == (uint32_t)&pa3[0])
    {
        pa3[0] = 450;
        pa3[1] = 600;
        pa3[2] = 0;
    }
}
```
初始化并导出此应用的参数
```C
static int par_init()
{
    int cnt = sizeof(params) / sizeof(params[0]);
    if (uparam_add_list(params, cnt) != RT_EOK)
    {
        LOG_E("param init failed");
    }
    return RT_EOK;
}

//导出到参数管理
INIT_PREV_EXPORT(par_init);
```
###其他应用内的参数配置同理

### shell指令
```C
Usage:
par list  [*/index] [offset]     - list all param
par reset [index]                - reset 'index' param to default
par set   [index] [offset] data1 ... dataN  - set index data[offset] to param with the format
par erase [yes]                  - erase all param and reset to default
par flush                        - save all param to flash
par reload                       - read all param to ram 
``` 
#### par list只会显示出数组的最长5个数据，需要显示更长的使用 par list index offset
