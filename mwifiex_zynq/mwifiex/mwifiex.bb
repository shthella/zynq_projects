SUMMARY = "Recipe for  build an external mwifiex Linux kernel module"
SECTION = "PETALINUX/modules"
LICENSE = "GPLv2"
LIC_FILES_CHKSUM = "file://COPYING;md5=12f884d2ae1ff87c09e5b7ccc2c4ca7e"

inherit module

INHIBIT_PACKAGE_STRIP = "1"

SRC_URI = "file://Makefile \
	   file://COPYING \
 file://mlan/mlan_11h.c \
	   file://mlan/mlan_meas.h \
       	   file://mlan/mlan_sta_cmd.c \
	   file://mlan/mlan_scan.c \
	   file://mlan/mlan_uap.h \
	   file://mlan/mlan_decl.h \
	   file://mlan/mlan_join.c \
	   file://mlan/mlan_11ac.h \
	   file://mlan/mlan.h \
	   file://mlan/mlan_sta_tx.c \
      	   file://mlan/mlan_meas.c \
	   file://mlan/mlan_event_ids.h \
	   file://mlan/mlan_11ax.h \
	file://mlan/mlan_hostcmd_ids.h \
	file://mlan/mlan_txrx.c \
	file://mlan/mlan_wmm.h \
	file://mlan/mlan_sdio.c \
	file://mlan/mlan_init.h \
	file://mlan/mlan_sta_event.c \
	file://mlan/mlan_sta_rx.c \
	file://mlan/mlan_11ax.c \
	file://mlan/mlan_11n_rxreorder.c \
	file://mlan/mlan_main.h \
	file://mlan/mlan_ieee.h \
	file://mlan/mlan_sdio.h \
	file://mlan/mlan_11n_aggr.h \
	file://mlan/mlan_11n.c \
	file://mlan/mlan_11h.h \
	file://mlan/mlan_ioctl.h \
	file://mlan/mlan_join.h \
	file://mlan/mlan_uap_txrx.c \
	file://mlan/mlan_uap_cmdevent.c \
	file://mlan/mlan_tlv_ids.h \
	file://mlan/mlan_cfp.c \
	file://mlan/mlan_uap_ioctl.c \
	file://mlan/mlan_sta_cmdresp.c \
	file://mlan/mlan_usb.c \
	file://mlan/mlan_11n.h \
	file://mlan/mlan_util.h \
	file://mlan/mlan_pcie.c \
	file://mlan/mlan_11n_rxreorder.h \
	file://mlan/mlan_fw.h \
	file://mlan/mlan_cmdevt.c \
	file://mlan/mlan_wmm.c \
	file://mlan/mlan_11n_aggr.c \
	file://mlan/mlan_11ac.c \
	file://mlan/mlan_11d.c \
	file://mlan/mlan_pcie.h \
	file://mlan/mlan_shim.c \
	file://mlan/mlan_module.c \
	file://mlan/mlan_init.c \
	file://mlan/mlan_sta_ioctl.c \
	file://mlan/mlan_misc.c \
file://mlinux/./moal_shim.h \
file://mlinux/./moal_priv.c \
file://mlinux/./mlan_decl.h \
file://mlinux/./moal_cfg80211_util.h \
file://mlinux/./moal_usb.h \
file://mlinux/./mlan.h \
file://mlinux/./moal_pcie.c \
file://mlinux/./moal_wext.c \
file://mlinux/./1 \
file://mlinux/./moal_cfg80211_util.c \
file://mlinux/./moal_cfg80211.c \
file://mlinux/./mlan_ieee.h \
file://mlinux/./moal_debug.c \
file://mlinux/./moal_uap_priv.c \
file://mlinux/./moal_sta_cfg80211.h \
file://mlinux/./mlan_ioctl.h \
file://mlinux/./moal_uap_cfg80211.c \
file://mlinux/./moal_sdio.h \
file://mlinux/./moal_priv.h \
file://mlinux/./moal_ioctl.c \
file://mlinux/./moal_wext.h \
file://mlinux/./moal_sta_cfg80211.c \
file://mlinux/./moal_main.c \
file://mlinux/./ioctl_error_codes.h \
file://mlinux/./moal_eth_ioctl.c \
file://mlinux/./moal_cfg80211.h \
file://mlinux/./moal_main.h \
file://mlinux/./moal_usb.c \
file://mlinux/./moal_uap.h \
file://mlinux/./moal_uap.c \
file://mlinux/./moal_uap_cfg80211.h \
file://mlinux/./moal_proc.c \
file://mlinux/./moal_init.c \
file://mlinux/./moal_eth_ioctl.h \
file://mlinux/./moal_sdio_mmc.c \
file://mlinux/./moal_uap_priv.h \
file://mlinux/./moal_shim.c \
file://mlinux/./moal_uap_wext.c \
file://mlinux/./moal_pcie.h" 

S = "${WORKDIR}"

# Tell BitBake to build inside the kernel source tree
KERNEL_MODULE_AUTOLOAD += "moal mlan"

# We override the default makefile to build both modules
do_compile() {
    unset LDFLAGS
    oe_runmake -C ${STAGING_KERNEL_DIR} M=${S} modules
}

do_install() {
    install -d ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra
    install -m 0644 ${S}/mlan.ko ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/
    install -m 0644 ${S}/moal.ko ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/
}


# The inherit of module.bbclass will automatically name module packages with
# "kernel-module-" prefix as required by the oe-core build environment.
