#!/usr/bin/env python3
"""Create a disposable diagnostic source overlay; never modify upstream/SDK."""
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
upstream = root / 'firmware/upstream'
manifest = json.loads((root / 'firmware/upstream-files.sha256.json').read_text())
for name, digest in manifest.items():
    if hashlib.sha256((upstream / name).read_bytes()).hexdigest() != digest:
        raise SystemExit(f'Upstream changed: {name}; review overlay before building')
archives = {
    'V3.7.2.0.zip': '77ca35173fc9d6c4fc8a6f6a97bd601d66c7a607a5c89e3012c138736ae0c049',
    'tc32_gcc_v2.0.tar.bz2': '33b854be3e3db3dba4b4dacdda2cd4ea1c94dfd4d562864a095956de7991b430',
}
for name, digest in archives.items():
    archive = upstream / 'telink_tools/downloads' / name
    if not archive.is_file() or hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
        raise SystemExit(f'Missing or changed toolchain/SDK archive: {archive}')
stage = root / 'build/diagnostic-source'
# Only remove our generated tree, never resolve its SDK symlink for removal.
if stage.exists():
    shutil.rmtree(stage)
stage.mkdir(parents=True)
for name in manifest:
    dest = stage / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(upstream / name, dest)
# Private SDK extracted from the verified archive: patching must never traverse
# a symlink into upstream. Other tools remain shared and unchanged.
tools_stage=stage/'telink_tools'
tools_stage.mkdir()
for name in ('toolchain','downloads'):
    (tools_stage/name).symlink_to('../../../firmware/upstream/telink_tools/'+name, target_is_directory=True)
with zipfile.ZipFile(upstream/'telink_tools/downloads/V3.7.2.0.zip') as archive:
    prefix='telink_zigbee_sdk-3.7.2.0/tl_zigbee_sdk/'
    for member in archive.infolist():
        if not member.filename.startswith(prefix) or member.is_dir(): continue
        relative=Path(member.filename[len(prefix):])
        if relative.is_absolute() or '..' in relative.parts: raise SystemExit('Unsafe SDK archive member')
        target=tools_stage/'sdk'/relative
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(archive.read(member))
shutil.copytree(root / 'firmware/application', stage / 'src/application')
shutil.copy2(root / 'firmware/application/telink/main.c', stage / 'src/telink/main.c')
shutil.copy2(root / 'firmware/application/telink/timer.c', stage / 'src/telink/hal/timer.c')
shutil.copy2(root / 'firmware/application/telink/zcl_yandex.c', stage / 'src/telink/custom_zcl/zcl_yandex.c')

def replace(path, old, new, count=1):
    p = stage / path
    s = p.read_text()
    if s.count(old) != count:
        raise SystemExit(f'Overlay anchor changed: {path}: {old[:60]!r}')
    p.write_text(s.replace(old, new))

# Relocate every SDK NV module and reset/install-code sectors. Keep disabled OTA
# slot constants independent of NV placement; no upstream factory data migration.
nv_header='telink_tools/sdk/proj/drivers/drv_nv.h'
replace(nv_header, '#define NV_BASE_ADDRESS             (0xE6000)', '#define NV_BASE_ADDRESS             (0x80000)')
replace(nv_header, '#define CFG_FACTORY_RST_CNT             (0xFC000)', '#define CFG_FACTORY_RST_CNT             (0x96000)')
replace(nv_header, '#define CFG_PRE_INSTALL_CODE            (0xFD000)', '#define CFG_PRE_INSTALL_CODE            (0x97000)')
replace(nv_header, '#define FLASH_OTA_IMAGE_MAX_SIZE    ((NV_BASE_ADDRESS - FLASH_ADDR_OF_APP_FW) / 2)',
        '#define FLASH_OTA_IMAGE_MAX_SIZE    (0x6F000)', 2)
replace('telink_tools/sdk/platform/chip_8258/flash.c',
        '_attribute_ram_code_sec_noinline_ unsigned char flash_mspi_write_ram(',
        '#include "application/flash_guard.h"\n\n_attribute_ram_code_sec_noinline_ unsigned char flash_mspi_write_ram(')
replace('telink_tools/sdk/platform/chip_8258/flash.c',
        'unsigned long data_len)\n{\n\tunsigned char r = irq_disable();\n\tunsigned char ret = 1;',
        'unsigned long data_len)\n{\n\tif (!ws_flash_command_allowed(cmd, addr, addr_en, data_len)) return 0;\n\tunsigned char r = irq_disable();\n\tunsigned char ret = 1;')

# Target has 1 MiB flash and a stock bootloader with application at 0x8000.
# This establishes a compile-only candidate, not stock bootloader/NV compatibility.
replace('src/telink/configs/version_cfg.h',
        '#define BOOT_LOADER_MODE          0',
        '#define BOOT_LOADER_MODE          1')
replace('src/telink/configs/version_cfg.h',
        '#define _VERSION_CFG_H_',
        '#define _VERSION_CFG_H_\n#define FLASH_CAP_SIZE_1M 1')

mk = stage / 'src/telink/Makefile'
s = mk.read_text()
for line in ['\tota_reformating/ensure_ota_scheme.c \\\n', '\tota_reformating/ram_code_flash.c \\\n', '\thal/zigbee_ota.c \\\n']:
    assert line in s
    s = s.replace(line, '')
s = s.replace('\tcustom_zcl/zcl_multistate_input.c \\\n', '\tcustom_zcl/zcl_yandex.c \\\n\tcustom_zcl/zcl_multistate_input.c \\\n')
a = s.index('COMMON_SOURCES :=')
b = s.index('# All application sources', a)
s = s[:a] + '''COMMON_SOURCES := \\
    $(SRC_DIR)/application/gestures.c \\
    $(SRC_DIR)/application/button_adapter.c \\
    $(SRC_DIR)/application/yandex_diagnostic.c \\
    $(SRC_DIR)/application/rejoin.c \\
    $(SRC_DIR)/application/flash_guard.c \\
    $(SRC_DIR)/base_components/button.c

''' + s[b:]
mk.write_text(s)
replace('src/telink/configs/app_cfg.h', '#define ZCL_OTA_SUPPORT                1', '#define ZCL_OTA_SUPPORT                0')
replace('src/telink/hal/zigbee_network.c', 'ota_queryStart(OTA_QUERY_INTERVAL);', '/* Diagnostic: OTA queries disabled. */', 2)
replace('src/telink/hal/zigbee_network.c', '    zb_init();', '''    zb_init();
    af_nodeDescManuCodeUpdate(0x132F);
    node_descriptor_t diagnostic_node_descriptor;
    af_nodeDescriptorCopy(&diagnostic_node_descriptor);
    diagnostic_node_descriptor.max_in_tr_size = 0x0394;
    diagnostic_node_descriptor.max_out_tr_size = 0x0394;
    af_nodeDescriptorSet(&diagnostic_node_descriptor);''')
zcl = 'src/telink/hal/zigbee_zcl.c'
# The diagnostic overlay carries exact ZCL access bits without modifying the
# integrity-protected upstream source tree.
replace('src/hal/zigbee.h', '''typedef enum {
    ATTR_READONLY,
    ATTR_WRITABLE,
} hal_attr_flags_t;''', '''typedef enum {
    ATTR_READONLY            = 0x01,
    ATTR_WRITABLE            = 0x03,
    ATTR_READONLY_REPORTABLE = 0x05,
    ATTR_WRITABLE_REPORTABLE = 0x07,
} hal_attr_flags_t;''')
replace(zcl, '''                attr_table_ptr->access =
                    ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE;
                if (attr->flag == ATTR_WRITABLE) {
                    attr_table_ptr->access |= ACCESS_CONTROL_WRITE;
                }''', '''                /* Carry the exact Telink ZCL access mask; reportable is
                 * not implied for every diagnostic attribute. */
                attr_table_ptr->access = (u8)attr->flag;''')
sdk_zcl = 'telink_tools/sdk/zigbee/zcl/zcl.c'
replace(zcl, 'static void zcl_incoming_message_callback(zclIncoming_t *pInHdlrMsg) {\n    if (pInHdlrMsg->hdr.cmd == ZCL_CMD_WRITE ||', 'extern void ws_diag_zcl_message_seen(u8 endpoint, u16 cluster_id, u8 command_id);\nstatic void zcl_incoming_message_callback(zclIncoming_t *pInHdlrMsg) {\n    ws_diag_zcl_message_seen(pInHdlrMsg->msg->indInfo.dst_ep, pInHdlrMsg->msg->indInfo.cluster_id, pInHdlrMsg->hdr.cmd);\n    if (pInHdlrMsg->hdr.cmd == ZCL_CMD_WRITE ||')
sdk_zdp = 'telink_tools/sdk/zigbee/zdo/zdp.c'
replace(sdk_zdp, '_CODE_ZDO_ static void zdp_clientCmdHandler(void *ind)\n{\n    aps_data_ind_t *p = (aps_data_ind_t *)ind;', 'extern void ws_diag_zdo_request_seen(u16 cluster_id, u8 endpoint);\n_CODE_ZDO_ static void zdp_clientCmdHandler(void *ind)\n{\n    aps_data_ind_t *p = (aps_data_ind_t *)ind;\n    u8 requested_endpoint = 0;\n    if (p->cluster_id == SIMPLE_DESC_REQ_CLID && p->asduLength > 3)\n        requested_endpoint = p->asdu[3];\n    ws_diag_zdo_request_seen(p->cluster_id, requested_endpoint);')
replace(zcl, 'static cluster_registerFunc_t get_register_func_by_cluster_id(u16 cluster_id) {', '''extern status_t zcl_yandex_register(u8, u16, u8, const zclAttrInfo_t[], cluster_forAppCb_t);
extern status_t zcl_diagnostic_onoff_register(u8, u16, u8, const zclAttrInfo_t[], cluster_forAppCb_t);
static cluster_registerFunc_t get_register_func_by_cluster_id(u16 cluster_id) {
    if (cluster_id == 0xFC03) return zcl_yandex_register;''')
replace(zcl, 'static cluster_forAppCb_t get_cmd_callback_by_cluster_id(u16 cluster_id) {', '''static status_t cmd_callback_yandex(zclIncomingAddrInfo_t *info, u8 cmd, void *payload) {
    zclIncoming_t *msg = cmd_incoming_from_addr_info(info);
    return cmd_callback(info->dstEp, 0xFC03, cmd, msg->pData, msg->dataLen);
}
static cluster_forAppCb_t get_cmd_callback_by_cluster_id(u16 cluster_id) {
    if (cluster_id == 0xFC03) return cmd_callback_yandex;''')
replace(zcl, 'if (cluster->cluster_id == ZCL_CLUSTER_OTA) {', 'if (!cluster->is_server || cluster->cluster_id == ZCL_CLUSTER_OTA) {')
replace(zcl, 'cluster_info_ptr->manuCode            = 0;', 'cluster_info_ptr->manuCode            = cluster->cluster_id == 0xFC03 ? 0x140A : 0;')
# Public YNDX-00532 captures use 0x140A for FC03 commands, while the node
# descriptor advertises 0x132F. Some coordinators reuse the node code in the
# manufacturer-specific ZCL header during the first interview. The SDK's
# generic dispatcher rejects that frame before the application handler, so
# exempt only FC03 here; zcl_yandex.c still accepts exactly the two observed
# Yandex codes.
replace(sdk_zcl,
        'if (!pCluster || (pCluster && (pCluster->manuCode != inMsg.hdr.manufCode) && (inMsg.hdr.manufCode != 0))) {',
        'if (!pCluster || (pCluster && (pCluster->manuCode != inMsg.hdr.manufCode) && (inMsg.hdr.manufCode != 0) && (pApsdeInd->indInfo.cluster_id != 0xFC03 || (inMsg.hdr.manufCode != 0x132F && inMsg.hdr.manufCode != 0x140A)))) {')
print(f'Diagnostic overlay prepared: {stage}')

replace(zcl, 'return zcl_onOff_register;', 'return zcl_diagnostic_onoff_register;')

# Keep descriptor/ZCL storage stable; change only the AF active endpoint registry.
replace(zcl, 'static void af_rx_callback(void *arg) {', '#include "application/yandex_diagnostic.h"\n\nstatic void af_rx_callback(void *arg) {')
replace(zcl, '        af_endpointRegister(endpoint->endpoint, endpoint_desc_ptr,\n                            af_rx_callback, NULL);', '        if (ws_diag_endpoint_active(endpoint->endpoint))\n            af_endpointRegister(endpoint->endpoint, endpoint_desc_ptr, af_rx_callback, NULL);')
with (stage / zcl).open('a') as f:
    f.write('\n#include "application/telink/endpoint_registry.inc"\n')
