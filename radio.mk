# Floral cellular radio simulation and its standard Android Radio HAL adapter.
PRODUCT_PACKAGES += \
    floral-rild

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.gsm.xml

PRODUCT_VENDOR_PROPERTIES += \
    ro.telephony.default_network=9 \
    ro.telephony.sim.count=1
