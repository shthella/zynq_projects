DESCRIPTION = "Marvell SD9177 Wifi Firmware"
LICENSE = "CLOSED"
PR = "r0"

SRC_URI += "file://sduart_nw61x_v1.bin.se"

S = "${WORKDIR}"

do_install() {
    install -d ${D}${nonarch_base_libdir}/firmware/nxp
    install -m 0644 ${WORKDIR}/sduart_nw61x_v1.bin.se ${D}${nonarch_base_libdir}/firmware/nxp/
}

# Explicitly declare all files to be packaged

FILES_${PN} = "${nonarch_base_libdir}/firmware/nxp/sduart_nw61x_v1.bin.se"

