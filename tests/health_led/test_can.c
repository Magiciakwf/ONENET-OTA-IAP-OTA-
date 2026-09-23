#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../18FreeRTOS项目/BSP/CAN_Tp.c"

CanRxMsg RxMsg;
QueueHandle_t OTA_Queue = (QueueHandle_t)1;
static OTA_Msg_t captured;
static unsigned queued;
static int queue_full;

BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item, BaseType_t *wake)
{
    (void)queue;
    if(queue_full) return pdFALSE;
    captured = *(const OTA_Msg_t *)item;
    queued++;
    *wake = pdTRUE;
    return pdPASS;
}

static void NewFrame(uint32_t id, uint8_t dlc)
{
    memset(&RxMsg, 0, sizeof(RxMsg));
    RxMsg.StdId = id;
    RxMsg.DLC = dlc;
}

static void MultiFrame(unsigned payload_size)
{
    uint8_t payload[CANTP_RECVSIZE];
    unsigned i;
    unsigned offset;
    unsigned before = queued;
    uint8_t sn = 1;
    for(i = 0; i < payload_size; i++) payload[i] = (uint8_t)i;
    payload[0] = 0x12;
    payload[1] = 0x34;
    NewFrame(OTA_SEND_ID, 8);
    RxMsg.Data[0] = 0x10 | (payload_size >> 8);
    RxMsg.Data[1] = payload_size & 0xFF;
    memcpy(&RxMsg.Data[2], payload, 6);
    assert(CAN_TP_Recv() == 0);
    assert(queued == before);
    for(offset = 6; offset < payload_size; offset += 7)
    {
        unsigned count = payload_size - offset;
        if(count > 7) count = 7;
        NewFrame(OTA_SEND_ID, (uint8_t)(count + 1));
        RxMsg.Data[0] = 0x20 | sn;
        memcpy(&RxMsg.Data[1], &payload[offset], count);
        CAN_TP_Recv();
        sn = (sn + 1) & 0x0F;
    }
    assert(queued == before + 1 && status == IDLE);
    assert(captured.type == DATA && captured.sequence == 0x1234);
    assert(captured.Recv_len == payload_size - 2);
    assert(memcmp(captured.data.CANTP_RecvBuf, &payload[2], payload_size - 2) == 0);
}

int main(void)
{
    unsigned before;
    NewFrame(OTA_START_ID, 5);
    RxMsg.Data[0] = 0xEE;
    RxMsg.Data[3] = 1;
    assert(CAN_TP_Recv() == 0);
    assert(captured.type == START && captured.data.file_size == 256 && captured.needs_ack);

    NewFrame(OTA_SEND_ID, 5);
    RxMsg.Data[0] = 4;
    RxMsg.Data[2] = 7;
    RxMsg.Data[3] = 0xAB;
    RxMsg.Data[4] = 0xCD;
    assert(CAN_TP_Recv() == 0);
    assert(captured.type == DATA && captured.sequence == 7 && captured.Recv_len == 2);
    assert(captured.data.CANTP_RecvBuf[0] == 0xAB && captured.data.CANTP_RecvBuf[1] == 0xCD);

    MultiFrame(258); /* 与 PC 脚本的 256 字节固件 + 2 字节序号匹配 */
    MultiFrame(CANTP_RECVSIZE); /* 最大缓冲区与连续帧序号回绕 */
    before = queued;
    NewFrame(OTA_SEND_ID, 3);
    RxMsg.Data[0] = 7; /* 声明长度大于实际 DLC */
    assert(CAN_TP_Recv() == 3 && queued == before);
    NewFrame(OTA_START_ID, 2);
    RxMsg.Data[0] = 0xEE;
    assert(CAN_TP_Recv() == 3 && queued == before);
    NewFrame(OTA_SEND_ID, 8);
    RxMsg.Data[0] = 0x12;
    RxMsg.Data[1] = 1; /* 513 字节超过缓冲区 */
    assert(CAN_TP_Recv() == 4);
    RxMsg.Data[0] = 0x10;
    RxMsg.Data[1] = 20;
    assert(CAN_TP_Recv() == 0);
    RxMsg.Data[0] = 0x22; /* 应为 SN=1，模拟丢帧 */
    assert(CAN_TP_Recv() == 2 && status == IDLE && queued == before);
    RxMsg.Data[0] = 0x10;
    RxMsg.Data[1] = 20;
    assert(CAN_TP_Recv() == 0);
    RxMsg.Data[0] = 0x21;
    RxMsg.DLC = 2;
    assert(CAN_TP_Recv() == 3 && status == IDLE && queued == before);

    NewFrame(OTA_END_ID, 5);
    RxMsg.Data[0] = 0xFF;
    RxMsg.Data[1] = 0xAA;
    assert(CAN_TP_Recv() == 0);
    assert(captured.type == END && captured.data.crc_buf[0] == 0xAA);
    before = queued;
    queue_full = 1;
    CAN_TP_Recv();
    assert(CAN_QueueDropCount == 1 && queued == before);
    queue_full = 0;
    CAN_TP_Recv(); /* 发送端超时后重发，可再次成功入队 */
    assert(queued == before + 1);
    OTA_Queue = NULL;
    CAN_TP_Recv();
    assert(CAN_QueueDropCount == 2);
    puts("PASS: CAN START/END, single/multi-frame, sequence wrap, malformed frames and queue-full retry");
    return 0;
}
