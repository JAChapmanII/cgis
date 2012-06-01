SDIR=src
LDIR=lib
ODIR=obj
BDIR=.

BINS=${BDIR}/cgis

LDFLAGS=
CFLAGS=-std=c99 -D_BSD_SOURCE
#-D_POSIX_C_SOURCE=200112L -D_XOPEN_SOURCE=700
CFLAGS+=-pedantic -Wall -Wextra

ifndef RELEASE
CFLAGS+=-g
else
CFLAGS+=-O3 -Os
endif

ifdef PROFILE
CFLAGS+=-pg
LDFLAGS+=-pg
endif

all: dirs ${BINS}
dirs:
	mkdir -p ${SDIR} ${ODIR} ${BDIR}

${BDIR}/cgis: ${ODIR}/cgis.o
	${CC}    -o $@ $^ ${LDFLAGS}

${ODIR}/%.o: ${SDIR}/%.c
	${CC} -c -o $@ $< ${CFLAGS}

clean:
	rm -f ${ODIR}/*.o ${BINS}

