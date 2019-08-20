#ifndef UPARAM_H
#define UPARAM_H

#include <rtdevice.h>
#include <board.h>

typedef void (*par_default)(void *address, uint8_t size);

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

/* 使用这个来定义参数 */
typedef const param_define_struct param_list;

#pragma pack(1)
typedef struct
{
    /* 参数地址 */
    uint32_t address;
    /* 参数字节长度, 250字节够了  */
    uint8_t size;
} param_p;

typedef struct
{
    /* 指向参数表的地址 */
    uint32_t par_list_add;
    /* 参数表包含的参数数量 */
    uint16_t par_list_size;
    /* 参数是否有效的读出，使用bit来标记参数 */
    uint8_t *read_valid;
} param_struct;

typedef struct
{
    /* 固定头部0X55 */
    uint8_t header;

    /* 参数个数统计 */
    union {
        uint32_t u32;
        uint8_t u8[4];
    } cnt;

    /* 参数数据段(只含数据)总长度 */
    union {
        uint32_t u32;
        uint8_t u8[4];
    } size;

 
    /* 头部校验信息 */
    uint8_t crc;
} param_header_struct;

#pragma pack()

/* 添加参数 */
rt_err_t uparam_add_list(param_list *list_address, uint16_t list_size);
/* 写入到flash */
uint16_t uparam_flush(void);
#endif
