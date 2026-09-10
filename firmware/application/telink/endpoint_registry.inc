/* Included by the generated Telink HAL translation unit, which owns descriptors.
 * Matches original unregister 3..6, then left down/up, right down/up registration.
 * No ZCL reinitialization, allocation, NV writes, network leave or reset here. */
volatile unsigned ws_diag_endpoint_registration_failures;
void ws_diag_platform_endpoints_changed(void) {
    /* MiDi's successful YNDX-00532 interview reports the full profile in
     * ascending order 1..6.  Keep that order for the initial 2/2 pairing
     * profile; the 3,5,4,6 order is retained for the original runtime mode
     * transition path reconstructed from the Yandex binary. */
    static const uint8_t pairing_order[] = {3,4,5,6};
    static const uint8_t runtime_order[] = {3,5,4,6};
    const uint8_t *order = ws_diag_endpoint_active(3) &&
                           ws_diag_endpoint_active(4) &&
                           ws_diag_endpoint_active(5) &&
                           ws_diag_endpoint_active(6)
                         ? pairing_order : runtime_order;
    ws_diag_endpoint_registration_failures = 0;
    for (uint8_t ep=3;ep<=6;ep++) (void)af_endpointUnregister(ep);
    for (unsigned i=0;i<4;i++) {
        uint8_t ep=order[i];
        if (ws_diag_endpoint_active(ep) &&
            !af_endpointRegister(ep,&endpoint_descriptors[ep-1],af_rx_callback,NULL))
            ws_diag_endpoint_registration_failures++;
    }
}
