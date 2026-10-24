#if !CLEAN_RX_DIAGNOSTIC
        uint8_t version = (cmd & 255u) == CLEAN_HT_DATA_CMD ?
            *(const volatile uint8_t *)FW_TX_BUF_ADDR : 0u;
        if (!clean_runtime_command_allowed(cmd, version)) {
            fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
            break;
        }
#endif
