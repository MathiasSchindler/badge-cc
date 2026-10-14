.section .rodata.clean_rx,"a",%progbits
.balign 4
.global clean_image
.global clean_image_end
clean_image:
.incbin "build/clean-rx/cyw43_rx_fw.bin"
clean_image_end:
