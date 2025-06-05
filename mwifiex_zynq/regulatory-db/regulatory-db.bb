DESCRIPTION = "Install custom regulatory.db and regulatory.db.p7s"
LICENSE = "CLOSED"
PR = "r0"

SRC_URI += "file://regulatory.db \				            			        
            file://regulatory.db.p7s"
S = "${WORKDIR}"
do_install() {
    	install -d ${D}${nonarch_base_libdir}/firmware
	install -m 0644 ${WORKDIR}/regulatory.db ${D}${nonarch_base_libdir}/firmware/
	install -m 0644 ${WORKDIR}/regulatory.db.p7s ${D}${nonarch_base_libdir}/firmware/
}

# Explicitly declare all files to be packaged
FILES_${PN}="${nonarch_base_libdir}/firmware/regulatory.db \
	     ${nonarch_base_libdir}/firmware/regulatory.db.p7s"



