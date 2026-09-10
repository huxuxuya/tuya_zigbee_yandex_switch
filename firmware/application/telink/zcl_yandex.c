#pragma pack(push, 1)
#include "zcl_include.h"
#pragma pack(pop)

static status_t yandex_handler(zclIncoming_t *msg) {
    if (msg->hdr.frmCtrl.bf.dir != ZCL_FRAME_CLIENT_SERVER_DIR ||
        (msg->hdr.manufCode != 0x140A && msg->hdr.manufCode != 0x132F))
        return ZCL_STA_UNSUP_MANU_CLUSTER_COMMAND;
    if (!msg->clusterAppCb) return ZCL_STA_UNSUP_CLUSTER_COMMAND;
    return msg->clusterAppCb(&msg->addrInfo, msg->hdr.cmd, msg->pData);
}
status_t zcl_yandex_register(u8 endpoint, u16 manuCode, u8 attrNum,
                            const zclAttrInfo_t attrs[], cluster_forAppCb_t cb) {
    return zcl_registerCluster(endpoint, 0xFC03, manuCode, attrNum, attrs,
                               yandex_handler, cb);
}

/* The SDK On/Off parser ignores application error returns and decodes optional
 * commands before our length checks. Diagnostic supports only off/on/toggle. */
static status_t onoff_handler(zclIncoming_t *msg) {
    if (msg->hdr.frmCtrl.bf.dir != ZCL_FRAME_CLIENT_SERVER_DIR || msg->hdr.manufCode != 0)
        return ZCL_STA_UNSUP_CLUSTER_COMMAND;
    if (msg->hdr.cmd > 2) return ZCL_STA_UNSUP_CLUSTER_COMMAND;
    if (msg->dataLen != 0) return ZCL_STA_MALFORMED_COMMAND;
    if (!msg->clusterAppCb) return ZCL_STA_UNSUP_CLUSTER_COMMAND;
    return msg->clusterAppCb(&msg->addrInfo, msg->hdr.cmd, msg->pData);
}
status_t zcl_diagnostic_onoff_register(u8 endpoint, u16 manuCode, u8 attrNum,
                                      const zclAttrInfo_t attrs[], cluster_forAppCb_t cb) {
    return zcl_registerCluster(endpoint, 6, manuCode, attrNum, attrs, onoff_handler, cb);
}
