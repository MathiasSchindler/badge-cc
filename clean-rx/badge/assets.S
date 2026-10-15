.section .rodata.clean_rx,"a",%progbits
.balign 4
.global clean_image
.global clean_image_end
clean_image:
.incbin CLEAN_CONTROLLER_PATH
clean_image_end:
