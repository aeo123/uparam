#include "uparam.h"
#include <fal.h>
#include <finsh.h>

#define UPARAM_DEBUG
#define UPARAM_FINSH

#define LOG_TAG "uparam"
#ifdef UPARAM_DEBUG
#define LOG_LVL LOG_LVL_DBG
#else
#define LOG_LVL LOG_LVL_WARNING
#endif
#include <ulog.h>

#define DEFAULT_PRA_PART "param"

static char *praram_partition = DEFAULT_PRA_PART;
static const struct fal_partition *par_part = RT_NULL;

/* 写数据覆盖保护，必须等读取后才能写入 */
static int8_t write_protect = 0;
/* 参数表 */
static param_struct *ls = 0;
/* 参数表在内存的索引 */
static uint32_t param_index = 0;

/* 保存到flash的结构头部信息 */
static param_header_struct param_header;

/**
  * @brief  uparam_add_list
  * @note   添加参数表
  * @param  *list_address: 参数表地址
  * @param  list_size:   包含参数的个数
  * @retval 
  */
rt_err_t uparam_add_list(param_list *list_address, uint16_t list_size)
{
    param_list *pa_this;

    //先遍历一下参数表是否已经添加过
    for (int i = 0; i < param_index; i++)
    {
        pa_this = (param_list *)ls[i].par_list_add;

        if (pa_this == list_address)
        {
            LOG_E("param list is exist, address: 0x%X", (uint32_t)list_address);
            return RT_ERROR;
        }
    }

    //分配内存
    param_struct *new_ls = (param_struct *)rt_realloc(ls, (param_index + 1) * sizeof(param_struct));

    if (new_ls != RT_NULL)
    {
        ls = new_ls;
        ls[param_index].par_list_add = (uint32_t)list_address;
        ls[param_index].par_list_size = list_size;

        //按位标记参数是否有效

        uint16_t bit_num = (list_size % 8 == 0) ? (list_size / 8) : (list_size / 8 + 1);
        ls[param_index].read_valid = (uint8_t *)rt_malloc(bit_num);
        memset(ls[param_index].read_valid, 0, bit_num);
        param_index++;

        for (int i = 0; i < list_size; i++)
        {
            pa_this = list_address + i;
            param_header.size.u32 += pa_this->size;
            LOG_D("add param suc,name: %s , address: 0x%X, size: %d", pa_this->name, pa_this->address, pa_this->size);
        }
        param_header.cnt.u32 += list_size;
        LOG_D("add param list success, list size: %d, data size total: %d", list_size, param_header.size.u32);
        return RT_EOK;
    }
    else
    {
        rt_free(ls);
        LOG_E("uparam realloc memory failed");
    }
    return RT_ERROR;
}

/**
  * @brief  cal_crc
  * @note   计算校验值
  * @param  start: 起始值
  * @param  *buff: 
  * @param  size: 
  * @retval 
  */
static uint8_t cal_crc(uint8_t start, uint8_t *buff, uint16_t size)
{
    uint8_t check = start;
    for (int i = 0; i < size; i++)
    {
        check ^= buff[i];
    }
    return check;
}

/**
  * @brief  uparam_readall
  * @note   从flash读取所有参数到内存
  * @retval 
  */
static uint16_t uparam_readall()
{
    uint32_t offset = 0;
    param_header_struct header;
    uint8_t temp[260];
    uint16_t read_num = 0; //读成功的数量
    param_p pa_this;       //当前参数信息
    param_p *pa_next;      //指向下一组参数的信息
    uint8_t rsize = sizeof(param_header_struct) + sizeof(param_p);

    write_protect++;

    //read header
    if (fal_partition_read(par_part, 0, temp, rsize) != rsize)
    {
        LOG_E("Uparam read header failed!");
        return 0;
    }
    offset += rsize;
    memcpy(&header, temp, sizeof(param_header_struct));
    pa_next = (param_p *)(temp + sizeof(param_header_struct));

    //检查header是否有效
    uint8_t check = cal_crc(0x55, header.cnt.u8, 4);
    check = cal_crc(check, header.size.u8, 4);
    if (header.header != 0x55 || check != header.crc)
    {
        LOG_E("Uparam header invalid!");
        return 0;
    }

    LOG_D("read param number: %d", header.cnt.u32);
    if (header.cnt.u32 != param_header.cnt.u32)
    {
        LOG_W("param number is changed, the exist is %d", param_header.cnt.u32);
    }

    //循环读取每组参数
    for (int i = 0; i < header.cnt.u32; i++)
    {
        //读数据段,长度为数据长度 + 1CRC + 下一组参数信息
        rsize = pa_next->size + 1 + sizeof(param_p);
        //这里读取了数据后pa指向的size是下一组数据的size,需要先记录一下当前数据的size不然crc会错误
        pa_this.size = pa_next->size;
        pa_this.address = pa_next->address;
        if (fal_partition_read(par_part, offset, temp, rsize) != rsize)
        {
            LOG_E("Uparam read data failed!");
            return 0;
        }
        offset += rsize;

        //检查CRC
        check = cal_crc(0x55, temp, pa_this.size);

        LOG_D("read param address 0x%X, size:%d, crc:%X, calc:%X", pa_this.address, pa_this.size, check, temp[pa_this.size]);

        if (check != temp[pa_this.size])
        {
            LOG_E("Uparam check data failed! Index:%d, Address:%X", i, (uint32_t)pa_this.address);
            return 0;
        }

        //读成功了，对内存赋值
        //不要直接赋值，先检查一下是否在参数表里面存在，防止数据地址被修改后写入未知地址
        param_list *pa_list_t;
        param_p *pa_t;
        int li;
        int idx;
        for (li = 0; li < param_index; li++)
        {
            pa_list_t = (param_list *)ls[li].par_list_add;

            for (idx = 0; idx < ls[li].par_list_size; idx++)
            {
                pa_t = (param_p *)&pa_list_t[idx];
                //是否存在 并且 size相同
                if (pa_t->address == pa_this.address)
                {
                    if (pa_t->size == pa_this.size)
                    {
                        //对成功读出的数据标记一下
                        ls[li].read_valid[idx / 8] |= 1 << idx;

                        //对数据赋读出的值
                        memcpy((void *)(pa_this.address), temp, pa_this.size);
                        read_num++;
                        break;
                    }
                    else
                    {
                        LOG_W("target size is different, size: %d", pa_t->size);
                    }
                }
            }
            //找到了
            if (idx != ls[li].par_list_size)
            {
                break;
            }
        }
        //未找到此参数
        if (li == param_index)
        {
            LOG_W("param is not exist, address: 0x%X ,read size: %d", pa_this.address, pa_this.size);
        }

        pa_next = (param_p *)(temp + pa_this.size + 1);
    }
    LOG_D("read param success count: %d", read_num);

    return read_num;
}

/**
  * @brief  uparam_writeall
  * @note   将参数表里面所有参数写入到flash
  * @retval 
  */
static uint16_t uparam_writeall()
{
    uint32_t offset = 0;
    uint8_t temp[270];
    param_list *pa_list;
    param_p *pa;
    uint8_t wsize = sizeof(param_header_struct);

    if (write_protect < 1)
    {
        LOG_E("Uparam should read once before write!");
        return 0;
    }

    //计算要写入的总字节数 header+ 数据头+数据+校验
    int allsize = wsize + sizeof(param_p) * param_header.cnt.u32 + param_header.size.u32 + param_header.cnt.u32;
    LOG_D("Uparam write, cnt: %d, data size: %d, all size: %d!", param_header.cnt.u32, param_header.size.u32, allsize);

    //擦除flash
    fal_partition_erase(par_part, 0, allsize);

    offset = wsize;
    //循环写入所有参数
    for (int li = 0; li < param_index; li++)
    {
        pa_list = (param_list *)ls[li].par_list_add;

        for (int i = 0; i < ls[li].par_list_size; i++)
        {
            pa = (param_p *)&pa_list[i];

            memcpy(temp, pa, sizeof(param_p));
            //准备数据
            memcpy(temp + sizeof(param_p), (void *)(pa->address), pa->size);
            temp[sizeof(param_p) + pa->size] = cal_crc(0x55, (uint8_t *)(pa->address), pa->size);

            LOG_D("write [%-16s], size:%d, crc:%X", pa_list[i].name, pa->size, temp[sizeof(param_p) + pa->size]);

            //写入 数据加一字节校验
            wsize = sizeof(param_p) + pa->size + 1;
            if (fal_partition_write(par_part, offset, temp, wsize) != wsize)
            {
                LOG_E("Uparam write data failed!");
                return 0;
            }
            offset += wsize;
        }
    }

    //最后写入header
    wsize = sizeof(param_header_struct);
    param_header.header = 0x55;
    param_header.crc = cal_crc(0x55, param_header.cnt.u8, 4);
    param_header.crc = cal_crc(param_header.crc, param_header.size.u8, 4);
    if (fal_partition_write(par_part, 0, (uint8_t *)&param_header, wsize) != wsize)
    {
        LOG_E("Uparam write header failed!");
        return 0;
    }
    LOG_D("Uparam write param, cnt: %d, write size: %d!", param_header.cnt.u32, offset - wsize);
    return param_header.cnt.u32;
}

/**
  * @brief  
  * @note   
  * @retval 
  */
uint16_t uparam_flush()
{
    return uparam_writeall();
}

/**
  * @brief  uparam_default
  * @note   还原参数到默认值
  * @retval None
  */
static void uparam_default()
{

    //对新加入的数据执行默认操作
    LOG_D("Reset changed param to default!");

    //遍历没有读出的数据
    param_list *pa_list_t;
    for (int li = 0; li < param_index; li++)
    {
        pa_list_t = (param_list *)ls[li].par_list_add;

        for (int i = 0; i < ls[li].par_list_size; i++)
        {
            if ((ls[li].read_valid[i / 8] & (1 << i)) == 0)
            {
                //
                LOG_D("reset param [%-16s], address: 0x%X, size:%d", pa_list_t[i].name, pa_list_t[i].address, pa_list_t[i].size);
                if (pa_list_t[i].default_fun != RT_NULL)
                {
                    pa_list_t[i].default_fun(pa_list_t[i].address, pa_list_t[i].size);
                }
                else
                {
                    LOG_E("reset error [%-16s], address: 0x%X, default_fun is null", pa_list_t[i].name, pa_list_t[i].address);
                }
            }
        }
    }
}

/**
  * @brief  打印参数列表的描述信息
  * @note   
  * @retval None
  */
static void print_list_header()
{
    rt_kprintf("\nFormat show as: f=float,d=int,u=uint,v*=vector(hex or float),s=str\r\n");
    rt_kprintf("Index Param            Address     Size  Format  Value\r\n");
    rt_kprintf("----- ----------       ----------  ----  ------  -----\r\n");
}

/**
  * @brief  打印单个参数
  * @note   
  * @param  *pa: 
  * @param  index: 
  * @param  offset: 数组类型的打印起始偏移 
  * @retval None
  */
static void print_element(param_p *pa, uint32_t index, uint32_t offset)
{
    uint16_t len;
    char buff[64];
    char value[8];
    param_list *pa_list = (param_list *)pa;

    //打印信息
    rt_kprintf("%-5d %-16s 0x%-8X  %-4d  ", index, (const char *)pa_list->name,
               pa->address, pa->size);

    memset(buff, 0, sizeof(buff));
    memset(value, 0, sizeof(value));

    //打印数据
    if (pa_list->type[0] == 'f')
    {
        memcpy(value, (uint8_t *)pa->address, pa->size);
        len = sprintf(buff, "Float   %.3f\r\n", *(float *)(value));
    }
    else if (pa_list->type[0] == 's')
    {
        len = sprintf(buff, "String  %s\r\n", (char *)pa->address);
    }
    else if (pa_list->type[0] == 'd')
    {
        int64_t convert = 0;
        if (pa->size == 1)
        {
            convert = (int64_t)(*(int8_t *)(pa->address));
        }
        if (pa->size == 2)
        {
            convert = (int64_t)(*(int16_t *)(pa->address));
        }
        if (pa->size == 4)
        {
            convert = (int64_t)(*(int32_t *)(pa->address));
        }
        if (pa->size == 8)
        {
            convert = (int64_t)(*(int64_t *)(pa->address));
        }
        len = sprintf(buff, "Intger  %lld\r\n", convert);
    }
    else if (pa_list->type[0] == 'u')
    {
        memcpy(value, (uint8_t *)pa->address, pa->size);
        len = sprintf(buff, "UIntger %lld\r\n", *(uint64_t *)(value));
    }
    else if (pa_list->type[0] == 'v')
    {
        //vector 格式,判断下输出形式
        if (pa_list->type[1] == 'b')
        {
            /**按单字节打印输出 */
            len = sprintf(buff, "V Byte  ");
            //最长只打印5个数字
            for (int s = 0; s < pa->size && s < 5; s++)
            {
                len += sprintf(buff + len, "%02X ", *((uint8_t *)(pa->address) + offset + s));
            }
        }
        else if (pa_list->type[1] == 'w')
        {
            /**按双字节打印输出 */
            len = sprintf(buff, "V Word  ");
            //最长只打印5个数字
            for (int s = 0; s < (pa->size / 2 - offset) && s < 5; s++)
            {
                len += sprintf(buff + len, "%04X ", *((uint16_t *)(pa->address) + offset + s));
            }
        }
        else if (pa_list->type[1] == 'd')
        {
            /**按四字节打印输出 */
            len = sprintf(buff, "V Dword ");
            //最长只打印5个数字
            for (int s = 0; s < (pa->size / 4 - offset) && s < 5; s++)
            {
                len += sprintf(buff + len, "%08X ", *((uint32_t *)(pa->address) + offset + s));
            }
        }
        else if (pa_list->type[1] == 'f')
        {
            /**按float打印输出 */
            len = sprintf(buff, "V Float ");
            //最长只打印5个数字
            for (int s = 0; s < (pa->size / 4 - offset) && s < 5; s++)
            {
                len += sprintf(buff + len, "%.3f ", *((float *)(pa->address) + offset + s));
            }
        }
        len += sprintf(buff + len, "\r\n");
    }
    rt_kprintf("%s", buff);
}

/**
  * @brief  uparam_list
  * @note   打印参数
  * @retval None
  */
static void uparam_list()
{

    param_list *pa_list;
    param_p *pa;
    uint32_t index = 0;

    print_list_header();
    for (int li = 0; li < param_index; li++)
    {
        pa_list = (param_list *)ls[li].par_list_add;

        for (int i = 0; i < ls[li].par_list_size; i++)
        {
            pa = (param_p *)&pa_list[i];
            print_element(pa, index++, 0);
        }
    }
}

/**
 * @brief  find_param_by_index
 * @note   通过索引找到参数
 * @param  index: 
 * @retval 
 */
static param_list *find_param_by_index(uint32_t index)
{
    uint32_t pre_index = 0;
    param_list *pa_list;

    for (int li = 0; li < param_index; li++)
    {
        pa_list = (param_list *)ls[li].par_list_add;
        if (index - pre_index < ls[li].par_list_size)
        {
            return (pa_list + (index - pre_index));
        }
        pre_index += ls[li].par_list_size;
    }

    return NULL;
}

/**
  * @brief  reset_param_by_index
  * @note   通过索引还原参数到默认值
  * @param  index: 
  * @retval None
  */
static void reset_param_by_index(uint32_t index)
{
    param_list *pa_list;
    pa_list = find_param_by_index(index);

    if (pa_list->default_fun != RT_NULL)
    {
        pa_list->default_fun(pa_list->address, pa_list->size);
    }
}

/**
  * @brief  erase_all_param
  * @note   擦除所有参数。都会被还原到默认值
  * @retval None
  */
static void erase_all_param(void)
{
    //只需要擦除保存的header即可
    fal_partition_erase(par_part, 0, sizeof(param_header_struct));

    //清除参数读取标志
    for (int li = 0; li < param_index; li++)
    {
        uint16_t bit_num = (ls[li].par_list_size % 8 == 0) ? (ls[li].par_list_size / 8) : (ls[li].par_list_size / 8 + 1);
        memset(ls[li].read_valid, 0, bit_num);
    }
    //初始化参数到默认值
    uparam_default();
}

/**
  * @brief  uparam_init
  * @note   初始化参数
  * @retval 
  */
static int uparam_init(void)
{
    /* 寻找参数分区是否存在 */
    if ((par_part = fal_partition_find(praram_partition)) == RT_NULL)
    {
        LOG_E("Uparam init failed! Partition (%s) find error!", praram_partition);
        RT_ASSERT(RT_ERROR);
        return RT_ERROR;
    }

    if (param_index < 1)
    {
        LOG_W("params num is zero, nothing to be done!");
        return RT_EOK;
    }

    //加载参数失败
    if (uparam_readall() < param_header.cnt.u32)
    {
        LOG_W("Uparam read failed, reset to default!");
        //初始化参数到默认值
        uparam_default();

        //重新写入参数
        uparam_writeall();
    }

    return RT_EOK;
}

INIT_COMPONENT_EXPORT(uparam_init);
//INIT_ENV_EXPORT

#ifdef UPARAM_FINSH
#include <finsh.h>

static void par(uint8_t argc, char **argv)
{
#define CMD_LIST_INDEX 0
#define CMD_RESET_INDEX 1
#define CMD_SET_INDEX 2
#define CMD_ERASE_INDEX 3
#define CMD_FLUSH_INDEX 4
#define CMD_RELOAD_INDEX 5
    const char *help_info[] =
        {
            "par list  [*/index] [offset]     - list all param",
            "par reset [index]                - reset 'index' param to default",
            "par set   [index] [offset] data1 ... dataN  - set index data[offset] to param with the format",
            "par erase [yes]                  - erase all param and reset to default",
            "par flush                        - save all param to flash",
            "par reload                       - read all param to ram",
        };

    if (argc < 2)
    {
        rt_kprintf("Usage:\n");
        for (int i = 0; i < sizeof(help_info) / sizeof(char *); i++)
        {
            rt_kprintf("%s\n", help_info[i]);
        }
        rt_kprintf("\n");
    }
    else
    {
        const char *cmd = argv[1];
        uint32_t index;

        if (!strcmp(cmd, "list"))
        {
            if (argc == 2)
            {
                uparam_list();
            }
            else if (argc == 3)
            {
                index = strtoul(argv[2], NULL, 0);
                param_p *pa = (param_p *)find_param_by_index(index);
                print_element(pa, index, 0);
            }
            else if (argc == 4)
            {
                index = strtoul(argv[2], NULL, 0);
                int offset = strtoul(argv[3], NULL, 0);
                param_p *pa = (param_p *)find_param_by_index(index);
                print_element(pa, index, offset);
            }
        }
        else if (!strcmp(cmd, "reset"))
        {
            index = strtoul(argv[2], NULL, 0);
            if (index > param_header.cnt.u32)
            {
                rt_kprintf("index is over range\r\n");
                return;
            }

            rt_kprintf("reset param , index: %d\r\n", index);
            reset_param_by_index(index);
        }
        else if (!strcmp(cmd, "set"))
        {
            if (argc < 5)
            {
                rt_kprintf("Usage: %s.\n", help_info[CMD_SET_INDEX]);
                return;
            }

            //设置的数据不是vector的时候offset无效
            param_list *pa_list;
            uint32_t offset;
            index = strtoul(argv[2], NULL, 0);
            offset = strtoul(argv[3], NULL, 0);

            if (index > param_header.cnt.u32)
            {
                rt_kprintf("index is over range\r\n");
                return;
            }
            pa_list = find_param_by_index(index);
            if (pa_list->type[0] == 'f')
            {
                float value_f = (float)atof(argv[4]);
                *(float *)(pa_list->address) = value_f;
                char buff[32];
                memset(buff, 0, 32);
                sprintf(buff, "set index: %d, to value:%.5f \r\n", index, value_f);
                rt_kprintf("%s \r\n", buff);
            }
            else if (pa_list->type[0] == 'd')
            {
                long long value_d = atoll(argv[4]);
							  unsigned long long value_ud = (long long)1<<63;
							  value_ud = value_d & ~value_ud;
                memcpy((uint8_t *)pa_list->address, &value_ud, pa_list->size);
                if (value_d < 0)
                {
                    *((uint8_t *)pa_list->address + pa_list->size - 1) |= 0x80;
                }
								char buff[32];
                memset(buff, 0, 32);
                sprintf(buff, "set index: %d, to value:%lld \r\n", index, value_d);
                rt_kprintf("%s \r\n", buff);
            }
            else if (pa_list->type[0] == 'u')
            {
                long long value_u = atoll(argv[4]);
                memcpy((uint8_t *)pa_list->address, &value_u, pa_list->size);
                char buff[32];
                memset(buff, 0, 32);
                sprintf(buff, "set index: %d, to value:%lld \r\n", index, value_u);
                rt_kprintf("%s \r\n", buff);
            }
            else if (pa_list->type[0] == 's')
            {
                char *value_s = argv[4];
                if (strlen(value_s) > pa_list->size)
                {
                    rt_kprintf("input value is too long\n");
                    return;
                }
                memset(pa_list->address, 0, pa_list->size);
                memcpy((uint8_t *)pa_list->address, value_s, strlen(value_s));
                rt_kprintf("set index: %d, to value:%s\r\n", index, value_s);
            }
            else if (pa_list->type[0] == 'v')
            {
                uint8_t input_size = argc - 4;
                int v;
                //参数是数据的情况下，需要判断offset是否超出地址范围
                if (pa_list->type[1] == 'b')
                {
                    //按字节输入
                    if (offset >= pa_list->size)
                    {
                        rt_kprintf("offset error,data size: %d\r\n", pa_list->size);
                        return;
                    }
                    rt_kprintf("set index: %d, offset: %d, to value:", index, offset);
                    for (uint8_t i = 0; i < input_size && i < pa_list->size; i++)
                    {
                        v = atoi(argv[4 + i]);
                        *((uint8_t *)pa_list->address + offset + i) = (uint8_t)(v & 0xff);
                        rt_kprintf("%d ", (uint8_t)v);
                    }
                }
                else if (pa_list->type[1] == 'w')
                {
                    //按2字节输入
                    if (offset >= pa_list->size / 2)
                    {
                        rt_kprintf("offset error,data size: %d\r\n", pa_list->size);
                        return;
                    }
                    rt_kprintf("set index: %d, offset: %d, to value:", index, offset);
                    for (uint8_t i = 0; i < input_size && i < (pa_list->size / 2); i++)
                    {
                        v = atoi(argv[4 + i]);
                        *((uint16_t *)pa_list->address + offset + i) = (uint16_t)(v & 0xffff);
                        rt_kprintf("%d ", (uint16_t)v);
                    }
                }
                else if (pa_list->type[1] == 'd')
                {
                    //按4字节输入
                    if (offset >= pa_list->size / 4)
                    {
                        rt_kprintf("offset error,data size: %d\r\n", pa_list->size);
                        return;
                    }
                    rt_kprintf("set index: %d, offset: %d, to value:", index, offset);
                    for (uint8_t i = 0; i < input_size && i < (pa_list->size / 4); i++)
                    {
                        v = atoi(argv[4 + i]);
                        *((uint32_t *)pa_list->address + offset + i) = (uint32_t)v;
                        rt_kprintf("%d ", (uint32_t)v);
                    }
                }
                else if (pa_list->type[1] == 'f')
                {
                    //按float输入
                    if (offset >= pa_list->size / 4)
                    {
                        rt_kprintf("offset error,data size: %d\r\n", pa_list->size);
                        return;
                    }
                    rt_kprintf("set index: %d, offset: %d, to value:", index, offset);

                    char sbuff[128] = {0};
                    char slen = 0;
                    for (uint8_t i = 0; i < input_size && i < (pa_list->size / 4); i++)
                    {
                        double dv = atof(argv[4 + i]);
                        *((float *)pa_list->address + offset + i) = (float)dv;
                        slen += sprintf(sbuff + slen, "%.3f ", (float)dv);
                    }
                    rt_kprintf("%s", sbuff);
                }
                rt_kprintf("\r\n");
            }
        }
        else if (!strcmp(cmd, "erase"))
        {
            if (argc < 3)
            {
                rt_kprintf("Usage: %s.\n", help_info[CMD_ERASE_INDEX]);
                return;
            }
            if (!strcmp((const char *)argv[2], "yes"))
            {
                rt_kprintf("erase all param\r\n");
                erase_all_param();
            }
            else
            {
                rt_kprintf("input yes to erase\r\n");
            }
        }
        else if (!strcmp(cmd, "flush"))
        {
            uparam_flush();
        }
        else if (!strcmp(cmd, "reload"))
        {
            uparam_readall();
        }
    }
}

MSH_CMD_EXPORT(par, uparam operate);

#endif
