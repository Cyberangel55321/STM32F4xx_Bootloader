#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "stm32f4xx.h"
#include "board.h"
#include "bl_usart.h"
#include "tim_delay.h"
#include "stm32_flash.h"
#include "magic_header.h"
#include "crc16.h"
#include "crc32.h"
#include "ringbuffer.h"
#include "utils.h"
#include "boot_config.h"
#include "partition.h"

#define LOG_TAG    "booting"
#define LOG_LVL    ELOG_LVL_INFO
#include "elog.h"

#define BL_VERSION      "v0.9.0"
#define BL_ADDRESS      0x08000000
#define BL_SIZE         (48 * 1024)
#define BOOT_DELAY      300
#define RX_BUFFER_SIZE  (5 * 1024)
#define RX_TIMEOUT_MS   20
#define PAYLOAD_SIZE_MAX (4096 + 8) // 4096为Program最大数据长度，8为Program指令的地址(4)和长度(4)
#define PACKET_SIZE_MAX (4 + PAYLOAD_SIZE_MAX + 2) // header(1) + opcode(1) + length(2) + payload + crc16(2)

// 协议常数
#define PACKET_HEADER_REQUEST 0xAA
#define PACKET_HEADER_RESPONSE 0x55

// 数据包结构常数
#define PACKET_HEADER_SIZE 1
#define PACKET_OPCODE_SIZE 1
#define PACKET_LENGTH_SIZE 2
#define PACKET_CRC_SIZE 2
#define PACKET_HEADER_OFFSET 0
#define PACKET_OPCODE_OFFSET 1
#define PACKET_LENGTH_OFFSET 2
#define PACKET_PAYLOAD_OFFSET 4
#define PACKET_MIN_SIZE (PACKET_HEADER_SIZE + PACKET_OPCODE_SIZE + PACKET_LENGTH_SIZE + PACKET_CRC_SIZE)

// 参数长度常数
#define ADDR_SIZE_PARAM_LENGTH 8  // uint addr + uint size
#define ADDR_SIZE_CRC_PARAM_LENGTH 12  // uint addr + uint size + uint crc


typedef enum
{
    PACKET_STATE_HEADER,
    PACKET_STATE_OPCODE,
    PACKET_STATE_LENGTH,
    PACKET_STATE_PAYLOAD,
    PACKET_STATE_CRC16,
} packet_state_machine_t;

typedef enum
{
    PACKET_OPCODE_INQUERY = 0x01,
    PACKET_OPCODE_GET_STATUS = 0x02,      // 新增: 获取状态
    PACKET_OPCODE_ERASE = 0x81,
    PACKET_OPCODE_PROGRAM = 0x82,
    PACKET_OPCODE_VERIFY  = 0x83,
    PACKET_OPCODE_START_UPGRADE = 0x84,   // 新增: 开始升级
    PACKET_OPCODE_COMMIT = 0x85,          // 新增: 提交升级
    PACKET_OPCODE_ROLLBACK = 0x86,        // 新增: 回滚升级
    PACKET_OPCODE_RESET = 0x21,
    PACKET_OPCODE_BOOT = 0x22,
} packet_opcode_t;

typedef enum
{
    INQUERY_SUBCODE_VERSION = 0x00,
    INQUERY_SUBCODE_MTU = 0x01,
} inquery_subcode_t;

typedef enum
{
    PACKET_ERRCODE_OK = 0,
    PACKET_ERRCODE_OPCODE,
    PACKET_ERRCODE_OVERFLOW,
    PACKET_ERRCODE_TIMEOUT,
    PACKET_ERRCODE_FORMAT,
    PACKET_ERRCODE_VERIFY,
    PACKET_ERRCODE_PARAM,
    PACKET_ERRCODE_UNKNOWN = 0xff,
} packet_errcode_t;

static uint8_t rb_buffer[RX_BUFFER_SIZE];
static rb_t rxrb;

/* Boot Config全局变量 */
static boot_config_t boot_config;

static uint8_t packet_buffer[PACKET_SIZE_MAX];
static uint32_t packet_index;
static packet_state_machine_t packet_state = PACKET_STATE_HEADER;
static packet_opcode_t packet_opcode;
static uint16_t packet_payload_length;

static bool application_validate_partition(partition_t partition)
{
    if (!magic_header_validate_partition(partition))
    {
        log_e("magic header invalid for partition %d", partition);
        return false;
    }

    // 使用Magic Header中的data_address获取固件实际地址
    uint32_t firmware_address = magic_header_get_address_partition(partition);
    uint32_t size = magic_header_get_length_partition(partition);
    uint32_t crc = magic_header_get_crc32_partition(partition);
    uint32_t ccrc = crc32((uint8_t *)firmware_address, size);

    if (crc != ccrc)
    {
        log_e("crc error for partition %d: expected %08X, got %08X", partition, crc, ccrc);
        return false;
    }

    return true;
}

static void boot_application_partition(partition_t partition)
{
    // Magic Header中的data_address就是固件实际地址
    uint32_t app_address = magic_header_get_address_partition(partition);
    log_w("application address from magic header: 0x%08X", app_address);
    
    if (app_address == 0 || app_address == 0xFFFFFFFF)
    {
        log_e("invalid firmware address for partition %d", partition);
        return;
    }

    if (!application_validate_partition(partition))
    {
        log_e("application validate failed for partition %d", partition);
        return;
    }

    log_w("booting application from partition %d at 0x%08X...", partition, app_address);
    tim_delay_ms(5);

    led_off(led1);
    TIM_DeInit(TIM6);
    USART_DeInit(USART1);
    USART_DeInit(USART3);
    NVIC_DisableIRQ(TIM6_DAC_IRQn);
    NVIC_DisableIRQ(USART1_IRQn);
    NVIC_DisableIRQ(USART3_IRQn);

    SCB->VTOR = app_address;  // 指向固件向量表
    extern void JumpApp(uint32_t base);
    JumpApp(app_address);
}

static void boot_application(void)
{
    boot_application_partition(boot_config_get_active_partition(&boot_config));
}

static void bl_response(packet_opcode_t opcode, packet_errcode_t errcode, const uint8_t *data, uint16_t length)
{
    uint8_t *response = packet_buffer, *prsp = response;
    put_u8_inc(&prsp, PACKET_HEADER_RESPONSE);
    put_u8_inc(&prsp, (uint8_t)opcode);
    put_u8_inc(&prsp, (uint8_t)errcode);
    put_u16_inc(&prsp, length);
    put_bytes_inc(&prsp, data, length);
    uint16_t crc = crc16(response, prsp - response);
    put_u16_inc(&prsp, crc);

    bl_usart_write(response, prsp - response);
}

static void bl_opcode_inquery_handler(void)
{
    log_i("inquery handler");

    if (packet_payload_length != 1)
    {
        log_e("inquery packet length error");
        return;
    }

    uint8_t subcode = get_u8(packet_buffer + PACKET_PAYLOAD_OFFSET);

    switch (subcode)
    {
        case INQUERY_SUBCODE_VERSION:
        {
            bl_response(PACKET_OPCODE_INQUERY, PACKET_ERRCODE_OK, (const uint8_t *)BL_VERSION, strlen(BL_VERSION));
            break;
        }
        case INQUERY_SUBCODE_MTU:
        {
            uint8_t bmtu[2];
            put_u16(bmtu, PAYLOAD_SIZE_MAX);
            bl_response(PACKET_OPCODE_INQUERY, PACKET_ERRCODE_OK, (const uint8_t *)&bmtu, sizeof(bmtu));
            break;
        }
        default:
        {
            log_w("unknown inquery subcode: %02X", subcode);
            break;
        }
    }
}

static void bl_opcode_erase_handler(void)
{
    log_i("erase handler");

    /* 检查升级状态，如果不在升级中，拒绝擦除APP区域 */
    if (boot_config_get_upgrade_state(&boot_config) != UPGRADE_STATE_DOWNLOADING)
    {
        log_w("erase rejected: not in download state");
        bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    if (packet_payload_length != ADDR_SIZE_PARAM_LENGTH)
    {
        log_e("erase packet length error: %d", packet_payload_length);
        bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    uint8_t *payload = packet_buffer + PACKET_PAYLOAD_OFFSET;
    uint32_t address = get_u32_inc(&payload);
    uint32_t size = get_u32_inc(&payload);

    if (address < STM32_FLASH_BASE || address + size > STM32_FLASH_BASE + STM32_FLASH_SIZE)
    {
        log_e("erase address=0x%08X, size=%u out of range", address, size);
        bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    // 保护Bootloader区域
    if (address >= BL_ADDRESS && address < BL_ADDRESS + BL_SIZE)
    {
        log_e("address 0x%08X is in bootloader area", address);
        bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    // 保护Boot Config区域
    if (address >= 0x0800C000 && address < 0x0800C100)
    {
        log_e("address 0x%08X is in boot config area", address);
        bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    log_d("erase address=0x%08X, size=%u", address, size);

    stm32_flash_unlock();
    stm32_flash_erase(address, size);
    stm32_flash_lock();

    bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_OK, NULL, 0);
}

static void bl_opcode_program_handler(void)
{
    log_i("program handler");

    /* 检查升级状态，如果不在升级中，拒绝编程 */
    if (boot_config_get_upgrade_state(&boot_config) != UPGRADE_STATE_DOWNLOADING)
    {
        log_w("program rejected: not in download state");
        bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    if (packet_payload_length <= ADDR_SIZE_PARAM_LENGTH)
    {
        log_e("program packet length error: %d", packet_payload_length);
        bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    uint8_t *payload = packet_buffer + PACKET_PAYLOAD_OFFSET;
    uint32_t address = get_u32_inc(&payload);
    uint32_t size = get_u32_inc(&payload);
    uint8_t *data = payload;

    /* 验证地址在下载目标分区内 */
    uint32_t target_addr = boot_config.download_address;
    uint32_t target_size = partition_get_size(partition_get_download_target(&boot_config));
    
    if (address < target_addr || address + size > target_addr + target_size)
    {
        log_e("address 0x%08X out of download range", address);
        bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    if (address < STM32_FLASH_BASE || address + size > STM32_FLASH_BASE + STM32_FLASH_SIZE)
    {
        log_e("program address=0x%08X, size=%u out of range", address, size);
        bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    if (address >= BL_ADDRESS && address < BL_ADDRESS + BL_SIZE)
    {
        log_e("address 0x%08X is protected", address);
        bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    if (size != packet_payload_length - ADDR_SIZE_PARAM_LENGTH)
    {
        log_e("program size %u does not match payload length %u", size, packet_payload_length - 8);
        bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    log_i("program address=0x%08X, size=%u", address, size);

    stm32_flash_unlock();
    stm32_flash_program(address, data, size);
    stm32_flash_lock();

    bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_OK, NULL, 0);
}

static void bl_opcode_verify_handler(void)
{
    log_i("verify handler");

    if (packet_payload_length != ADDR_SIZE_CRC_PARAM_LENGTH)
    {
        log_e("verify packet length error: %d", packet_payload_length);
        bl_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    uint8_t *payload = packet_buffer + PACKET_PAYLOAD_OFFSET;
    uint32_t address = get_u32_inc(&payload);
    uint32_t size = get_u32_inc(&payload);
    uint32_t crc = get_u32_inc(&payload);

    if (address < STM32_FLASH_BASE || address + size > STM32_FLASH_BASE + STM32_FLASH_SIZE)
    {
        log_e("verify address=0x%08X, size=%u out of range", address, size);
        bl_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    log_d("verify address=0x%08X, size=%u, crc=0x%08X", address, size, crc);

    uint32_t ccrc = crc32((uint8_t *)address, size);

    if (ccrc != crc)
    {
        log_e("verify failed: expected 0x%08X, got 0x%08X", crc, ccrc);

        /* 验证失败，标记分区无效 */
        partition_t target = partition_get_download_target(&boot_config);
        boot_config_set_partition_valid(&boot_config, target, false);
        boot_config_set_upgrade_state(&boot_config, UPGRADE_STATE_DOWNLOADING);
        boot_config_save(&boot_config);

        bl_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_VERIFY, NULL, 0);
        return;
    }

    /* 验证成功，更新Boot Config */
    partition_t target = partition_get_download_target(&boot_config);
    boot_config_set_partition_valid(&boot_config, target, true);
    boot_config.download_crc32 = crc;
    boot_config.download_size = size;
    // boot_config_set_upgrade_state(&boot_config, UPGRADE_STATE_VERIFIED);
    
    if (!boot_config_save(&boot_config)) 
    {
        log_e("save boot config failed");
        bl_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_UNKNOWN, NULL, 0);
        return;
    }

    log_i("verify success for partition %d", target);

    bl_response(PACKET_OPCODE_VERIFY, PACKET_ERRCODE_OK, NULL, 0);
}

static void bl_opcode_reset_handler(void)
{
    log_i("reset handler");
    bl_response(PACKET_OPCODE_RESET, PACKET_ERRCODE_OK, NULL, 0);
    log_w("system resetting...");
    tim_delay_ms(2);

    NVIC_SystemReset();
}

static void bl_opcode_boot_handler(void)
{
    log_i("boot handler");
    bl_response(PACKET_OPCODE_BOOT, PACKET_ERRCODE_OK, NULL, 0);

    boot_application();
}

static void bl_opcode_start_upgrade_handler(void)
{
    log_i("start upgrade handler");

    /* 检查当前是否正在升级 */
    if (boot_config_get_upgrade_state(&boot_config) != UPGRADE_STATE_IDLE &&
        boot_config_get_upgrade_state(&boot_config) != UPGRADE_STATE_COMMITTED)
    {
        log_w("upgrade already in progress, state=%d", boot_config_get_upgrade_state(&boot_config));
    }

    /* 确定下载目标分区 */
    partition_t target = partition_get_download_target(&boot_config);
    log_i("download target: partition %d at 0x%08X", target, partition_get_address(target));

    /* 更新Boot Config */
    boot_config_set_upgrade_state(&boot_config, UPGRADE_STATE_DOWNLOADING);
    boot_config.download_address = partition_get_address(target);
    boot_config.download_size = 0;
    boot_config.download_crc32 = 0;

    /* 保存配置 */
    if (!boot_config_save(&boot_config))
    {
        log_e("save boot config failed");
        bl_response(PACKET_OPCODE_START_UPGRADE, PACKET_ERRCODE_UNKNOWN, NULL, 0);
        return;
    }

    bl_response(PACKET_OPCODE_START_UPGRADE, PACKET_ERRCODE_OK, NULL, 0);
}

static void bl_opcode_commit_handler(void)
{
    log_i("commit handler");

    /* 检查升级状态 */
    if (boot_config_get_upgrade_state(&boot_config) != UPGRADE_STATE_DOWNLOADING)
    {
        log_e("upgrade not verified, state=%d", boot_config_get_upgrade_state(&boot_config));
        bl_response(PACKET_OPCODE_COMMIT, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    /* 切换活动分区 */
    partition_t current_active = boot_config_get_active_partition(&boot_config);
    partition_t new_active = partition_get_opposite(current_active);

    boot_config_set_active_partition(&boot_config, new_active);
    boot_config_set_upgrade_state(&boot_config, UPGRADE_STATE_COMMITTED);

    /* 保存配置 */
    if (!boot_config_save(&boot_config))
    {
        log_e("save boot config failed");
        bl_response(PACKET_OPCODE_COMMIT, PACKET_ERRCODE_UNKNOWN, NULL, 0);
        return;
    }

    log_i("upgrade committed, new active partition: %d", new_active);
    bl_response(PACKET_OPCODE_COMMIT, PACKET_ERRCODE_OK, NULL, 0);
}

static void bl_opcode_get_status_handler(void)
{
    log_i("get status handler");

    /* 返回当前状态信息 */
    uint8_t status_data[16];
    uint8_t *p = status_data;

    put_u8_inc(&p, boot_config.active_partition);
    put_u8_inc(&p, boot_config.app0_valid);
    put_u8_inc(&p, boot_config.app1_valid);
    put_u8_inc(&p, boot_config.upgrade_state);
    put_u32_inc(&p, boot_config.boot_count);

    bl_response(PACKET_OPCODE_GET_STATUS, PACKET_ERRCODE_OK, status_data, p - status_data);
}

static void bl_opcode_rollback_handler(void)
{
    log_i("rollback handler");

    /* 回滚到另一个分区 */
    partition_t current_active = boot_config_get_active_partition(&boot_config);
    partition_t rollback_target = partition_get_opposite(current_active);

    /* 检查目标分区是否有效 */
    if (!boot_config_is_partition_valid(&boot_config, rollback_target))
    {
        log_e("rollback target partition %d is invalid", rollback_target);
        bl_response(PACKET_OPCODE_ROLLBACK, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    /* 切换活动分区 */
    boot_config_set_active_partition(&boot_config, rollback_target);
    boot_config_set_upgrade_state(&boot_config, UPGRADE_STATE_IDLE);

    /* 保存配置 */
    if (!boot_config_save(&boot_config))
    {
        log_e("save boot config failed");
        bl_response(PACKET_OPCODE_ROLLBACK, PACKET_ERRCODE_UNKNOWN, NULL, 0);
        return;
    }

    log_i("rollback to partition %d", rollback_target);
    bl_response(PACKET_OPCODE_ROLLBACK, PACKET_ERRCODE_OK, NULL, 0);
}

static void bl_packet_handler(void)
{
    switch (packet_opcode)
    {
        case PACKET_OPCODE_INQUERY:
            bl_opcode_inquery_handler();
            break;
        case PACKET_OPCODE_ERASE:
            bl_opcode_erase_handler();
            break;
        case PACKET_OPCODE_PROGRAM:
            bl_opcode_program_handler();
            break;
        case PACKET_OPCODE_VERIFY:
            bl_opcode_verify_handler();
            break;
        case PACKET_OPCODE_RESET:
            bl_opcode_reset_handler();
            break;
        case PACKET_OPCODE_BOOT:
            bl_opcode_boot_handler();
            break;
        case PACKET_OPCODE_START_UPGRADE:
            bl_opcode_start_upgrade_handler();
            break;
        case PACKET_OPCODE_COMMIT:
            bl_opcode_commit_handler();
            break;
        case PACKET_OPCODE_GET_STATUS:
            bl_opcode_get_status_handler();
            break;
        case PACKET_OPCODE_ROLLBACK:
            bl_opcode_rollback_handler();
            break;
        default:
            // 未知指令
            log_w("Unknown command: %02X", packet_opcode);
            break;
    }
}

static bool bl_byte_handler(uint8_t byte)
{
    bool full_packet = false;

    // 处理字节数据超时接收
    static uint64_t last_byte_ms;
    uint64_t now_ms = tim_get_ms();
    if (now_ms - last_byte_ms > RX_TIMEOUT_MS)
    {
        if (packet_state != PACKET_STATE_HEADER)
            log_w("last packet rx timeout");
        packet_index = 0;
        packet_state = PACKET_STATE_HEADER;
    }
    last_byte_ms = now_ms;

    log_v("recv: %02X", byte);

    // 字节接收状态机处理
    packet_buffer[packet_index++] = byte;
    switch (packet_state)
    {
        case PACKET_STATE_HEADER:
            if (packet_buffer[PACKET_HEADER_OFFSET] == PACKET_HEADER_REQUEST)
            {
                log_d("header ok");
                packet_state = PACKET_STATE_OPCODE;
            }
            else
            {
                log_w("header error: %02X", packet_buffer[PACKET_HEADER_OFFSET]);
                packet_index = 0;
                packet_state = PACKET_STATE_HEADER;
            }
            break;
            
        case PACKET_STATE_OPCODE:
            if (packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_INQUERY ||
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_GET_STATUS ||
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_ERASE ||
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_PROGRAM ||
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_VERIFY ||
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_START_UPGRADE ||
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_COMMIT ||
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_ROLLBACK ||
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_RESET ||
                packet_buffer[PACKET_OPCODE_OFFSET] == PACKET_OPCODE_BOOT)
            {
                log_d("opcode ok: %02X", packet_buffer[PACKET_OPCODE_OFFSET]);
                packet_opcode = (packet_opcode_t)packet_buffer[PACKET_OPCODE_OFFSET];
                packet_state = PACKET_STATE_LENGTH;
            }
            else
            {
                log_w("opcode error: %02X", packet_buffer[PACKET_OPCODE_OFFSET]);
                packet_index = 0;
                packet_state = PACKET_STATE_HEADER;
            }
            break;

        case PACKET_STATE_LENGTH:
            if (packet_index == PACKET_PAYLOAD_OFFSET)
            {
                uint16_t payload_length = get_u16(packet_buffer + PACKET_LENGTH_OFFSET);
                if (payload_length <= PAYLOAD_SIZE_MAX)
                {
                    log_d("length ok: %u", payload_length);
                    packet_payload_length = payload_length;
                    if (packet_payload_length > 0)
                        packet_state = PACKET_STATE_PAYLOAD;
                    else
                        packet_state = PACKET_STATE_CRC16;
                }
                else
                {
                    log_w("length error: %u", payload_length);
                    packet_index = 0;
                    packet_state = PACKET_STATE_HEADER;
                }
            }
            break;

        case PACKET_STATE_PAYLOAD:
            if (packet_index == PACKET_PAYLOAD_OFFSET + packet_payload_length)
            {
                log_d("payload receive ok");
                packet_state = PACKET_STATE_CRC16;
            }
            break;

        case PACKET_STATE_CRC16:
            if (packet_index == PACKET_MIN_SIZE + packet_payload_length)
            {
                uint16_t crc = get_u16(packet_buffer + PACKET_PAYLOAD_OFFSET + packet_payload_length);
                uint16_t ccrc = crc16(packet_buffer, PACKET_PAYLOAD_OFFSET + packet_payload_length);
                if (crc == ccrc)
                {
                    full_packet = true;
                    log_d("crc16 ok: %04X", crc);
                    log_d("packet received: opcode=%02X, length=%u", packet_opcode, packet_payload_length);
                    if (LOG_LVL >= ELOG_LVL_VERBOSE)
                        elog_hexdump("payload", 16, packet_buffer, PACKET_MIN_SIZE + packet_payload_length);
                }
                else
                {
                    log_w("crc16 error: expected %04X, got %04X", crc, ccrc);
                }

                packet_index = 0;
                packet_state = PACKET_STATE_HEADER;
            }
            break;
        default:
            break;
    }

    return full_packet;
}

static void bl_usart_rx_handler(const uint8_t *data, uint32_t length)
{
    rb_puts(rxrb, data, length);
}

static bool key_trap_check(void)
{
    for (uint32_t t = 0; t < BOOT_DELAY; t+=10)
    {
        tim_delay_ms(10);
        if (!key_read(key2))
            return false;
    }
    log_w("key pressed, trap into boot");
    return true;
}

static void wait_key_release(void)
{
    while (key_read(key2))
        tim_delay_ms(10);
}

static bool key_press_check(void)
{
    if (!key_read(key2))
        return false;

    tim_delay_ms(10);
    if (!key_read(key2))
        return false;

    return true;
}

bool magic_header_trap_boot(void)
{
    partition_t active = boot_config_get_active_partition(&boot_config);
    
    if (!magic_header_validate_partition(active))
    {
        log_w("magic header invalid for partition %d, trap into boot", active);
        return true;
    }

    if (!application_validate_partition(active))
    {
        log_w("application validate failed for partition %d, trap into boot", active);
        return true;
    }

    return false;
}


bool rx_trap_boot(void)
{
    for (uint32_t i = 0; i < 3000; i += 10)
    {
        tim_delay_ms(10);
        if (!rb_empty(rxrb))
        {
            log_w("data received, trap into boot");
            return true;
        }
    }

    return false;
}

void bootloader_main(void)
{
    log_i("Bootloader started.");

    /* 加载Boot Config */
    if (!boot_config_load(&boot_config))
    {
        log_e("load boot config failed");
        /* 继续执行，使用默认值 */
    }

    /* 检查升级状态，恢复中断的升级 */
    upgrade_state_t upgrade_state = boot_config_get_upgrade_state(&boot_config);
    if (upgrade_state != UPGRADE_STATE_IDLE && upgrade_state != UPGRADE_STATE_COMMITTED)
    {
        log_w("upgrade was interrupted, state=%d, resetting to IDLE", upgrade_state);
        boot_config_set_upgrade_state(&boot_config, UPGRADE_STATE_IDLE);
        boot_config_save(&boot_config);
    }

    key_init(key2);

    rxrb = rb_new(rb_buffer, RX_BUFFER_SIZE);
    bl_usart_init();
    bl_usart_register_rx_callback(bl_usart_rx_handler);

    bool trapboot = false;

    // 三种进入bootloader的方式：魔数检测、按键检测、串口数据接收检测，任意一种满足即可进入bootloader
    if (!trapboot)
        trapboot = magic_header_trap_boot();

    if (!trapboot)
        trapboot = key_trap_check();

    if (!trapboot)
        trapboot = rx_trap_boot();

    // if (!trapboot)
    //     boot_application();

    if (!trapboot)
    {
        /* 获取活动分区 */
        partition_t active = boot_config_get_active_partition(&boot_config);
        
        /* 检查分区有效性 */
        if (boot_config_is_partition_valid(&boot_config, active))
        {
            log_i("booting from partition %d", active);
            boot_application_partition(active);
        }
        else
        {
            log_w("active partition %d is invalid, trying opposite", active);
            
            /* 尝试另一个分区 */
            partition_t opposite = partition_get_opposite(active);
            if (boot_config_is_partition_valid(&boot_config, opposite))
            {
                log_i("booting from partition %d", opposite);
                boot_application_partition(opposite);
            }
            else
            {
                log_e("both partitions invalid, entering upgrade mode");
            }
        }
    }

    led_init(led1);
    led_on(led1);
    wait_key_release();

    while (1)
    {
        if (key_press_check())
        {
            log_w("key pressed, rebooting...");
            tim_delay_ms(2);
            NVIC_SystemReset();
        }

        if (!rb_empty(rxrb))
        {
            uint8_t byte;
            rb_get(rxrb, &byte);
            if (bl_byte_handler(byte))
            {
                bl_packet_handler();
            }
        }
    }
}
