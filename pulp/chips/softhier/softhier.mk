<<<<<<< HEAD
# Call the correct Makefile depending on TOPOLOGY variable
ifeq ($(TOPOLOGY),)
    ACTUAL_TOPO = softhier
else ifeq ($(findstring softhier,$(TOPOLOGY)),)
    ACTUAL_TOPO = softhier_$(TOPOLOGY)
else
    ACTUAL_TOPO = $(TOPOLOGY)
endif

SOFTHIER_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

include $(SOFTHIER_DIR)$(ACTUAL_TOPO)/$(ACTUAL_TOPO).mk