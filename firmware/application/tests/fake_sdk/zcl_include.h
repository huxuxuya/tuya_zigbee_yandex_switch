#ifndef WS_TEST_ZCL
#define WS_TEST_ZCL
#include <stdint.h>
#pragma pack(push, 1)
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint8_t status_t;
typedef struct { u8 unused; } zclAttrInfo_t;
typedef struct { u8 dstEp; } zclIncomingAddrInfo_t;
typedef status_t (*cluster_forAppCb_t)(zclIncomingAddrInfo_t *, u8, void *);
typedef struct {
    struct { struct { struct { u8 dir; } bf; } frmCtrl; u16 manufCode; u8 cmd; } hdr;
    zclIncomingAddrInfo_t addrInfo;
    u16 dataLen;
    u8 *pData;
    cluster_forAppCb_t clusterAppCb;
} zclIncoming_t;
typedef status_t (*cluster_cmdHdlr_t)(zclIncoming_t *);
#define ZCL_FRAME_CLIENT_SERVER_DIR 0
#define ZCL_STA_UNSUP_CLUSTER_COMMAND 0x81
#define ZCL_STA_UNSUP_MANU_CLUSTER_COMMAND 0x83
#define ZCL_STA_MALFORMED_COMMAND 0x80
status_t zcl_registerCluster(u8,u16,u16,u8,const zclAttrInfo_t *,cluster_cmdHdlr_t,cluster_forAppCb_t);
#pragma pack(pop)
#endif
