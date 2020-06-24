# mmdtwm - Martin's Mouse Driven Tiling Window Manager

NAME = mmdtwm
SRC = src/main.c
CC = cc
VERSION = PRE-ALPHA-0.1
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man
LINKS = -lxcb -lxcb-randr -lxcb-keysyms
INCLUDES = -Isrc
CFLAGS = -std=c99 -pedantic-errors -pedantic -Wall -msse2 -Wpointer-arith -Wstrict-prototypes -fomit-frame-pointer -ffast-math
CFLAGS_RELEASE = -flto -Os
CFLAGS_DEBUG = -g
OTHER_FILES = LICENSE Makefile README.md

${NAME}: ${SRC}
	@echo make release build
	@${CC} -o ${NAME} ${LINKS} ${INCLUDES} ${CFLAGS} ${CFLAGS_RELEASE} ${SRC}

debug:
	@echo make debug build
	@${CC} -o ${NAME} ${LINKS} ${INCLUDES} ${CFLAGS} ${CFLAGS_DEBUG} ${SRC}

clean:
	@echo cleaning
	@rm -f ${NAME} ${NAME}-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p ${NAME}-${VERSION}
	@cp -R ${OTHER_FILES} ${SRC} ${NAME}.1 ${NAME}-${VERSION}
	@tar -cf - "${NAME}-${VERSION}" | \
		gzip -c > "${NAME}-${VERSION}.tar.gz"
	@rm -rf "${NAME}-${VERSION}"

install: ${NAME}
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin/${NAME}
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f ${NAME} ${DESTDIR}${PREFIX}/bin/.
	@chmod 755 ${DESTDIR}${PREFIX}/bin/${NAME}
	@echo installing manual page to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@sed "s/VERSION/${VERSION}/g" < ${NAME}.1 > ${DESTDIR}${MANPREFIX}/man1/${NAME}.1
	@chmod 644 ${DESTDIR}${MANPREFIX}/man1/${NAME}.1

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin/${NAME}
	@rm -f ${DESTDIR}${PREFIX}/bin/${NAME}
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/${NAME}.1

.PHONY: debug clean dist install uninstall



