#
# Component Makefile
#

LVGL_DRIVER_DIRS += drivers/shared_spi
LVGL_DRIVER_DIRS += drivers/ili9341
LVGL_DRIVER_DIRS += drivers/xpt2046

EXTRA_COMPONENT_DIRS += $(LVGL_DRIVER_DIRS)

COMPONENT_SRCDIRS := . \
	lv_core \
	lv_draw \
	lv_objx \
	lv_hal \
	lv_misc \
	lv_misc/lv_fonts \
	lv_themes \
	lv_fonts
COMPONENT_ADD_INCLUDEDIRS := $(COMPONENT_SRCDIRS) ..
