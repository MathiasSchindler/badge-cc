static void station_publish_mac(void);

static void publish_tx_diag(void) {
    const uint8_t *bytes = (const uint8_t *)&PUBLICATION(tx_diag);
    for (uint32_t i = 0u; i < sizeof(PUBLICATION(tx_diag)); ++i) MBOX->rx_input[i] = bytes[i];
}

