
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_SRC_FILES:= \
    lcdparamservice.c

LOCAL_C_INCLUDES += bionic \
$(call include-path-for, libhardware_legacy)/hardware_legacy

LOCAL_MODULE:=lcdparamservice

LOCAL_MODULE_TAGS := optional

LOCAL_STATIC_LIBRARIES := libfs_mgr libcutils libc liblog

LOCAL_SHARED_LIBRARIES := libhardware_legacy libnetutils liblog

include $(BUILD_EXECUTABLE)
