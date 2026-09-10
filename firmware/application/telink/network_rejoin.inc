/* Main-loop-only SDK boundary. No factory reset, leave, NV erase or GPIO calls. */
extern volatile unsigned ws_diag_endpoint_registration_failures;
static bool ws_diag_network_ready(void) {
    return !zb_isDeviceFactoryNew() && zb_isDeviceJoinedNwk() && bdb_isIdle() &&
           zb_isTaskDone() && !tl_stackBusy();
}
static uint8_t ws_diag_request_rejoin(void) {
    if(ws_diag_endpoint_registration_failures) ws_diag_platform_endpoints_changed();
    if(ws_diag_endpoint_registration_failures)return 0xfe; /* local AF failure */
    uint8_t channel=g_zbMacPib.phyChannelCur;
    if(channel<11 || channel>26)return 0xfd; /* local invalid channel */
    zb_rejoinSecModeSet(REJOIN_SECURITY);
    return zb_rejoinReq(1u<<channel,zdo_cfg_attributes.config_nwk_scan_duration);
}
