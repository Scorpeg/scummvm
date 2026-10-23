include $(srcdir)/backends/platform/ios7/module.mk

IOS5_EXCLUDED_OBJS := $(addprefix backends/platform/ios7/,ios7_main.o ios7_mouse_controller.o ios7_gamepad_controller.o ios7_scene_delegate.o)
OBJS := $(filter-out $(IOS5_EXCLUDED_OBJS),$(OBJS))
MODULE := backends/platform/ios5
MODULE_OBJS := $(addprefix $(MODULE)/,main.o metrics.o shared_texture.o)
OBJS := $(MODULE_OBJS) $(OBJS)
MODULE_DIRS += $(sort $(dir $(MODULE_OBJS)))
