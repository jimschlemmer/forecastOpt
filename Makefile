#CC	= gcc -Wall
CC	= gcc

SATLIBDIR = ../satModel/v3/satLib
INCLUDEDIR = ../satModel/v3/include
SATLIB = ${SATLIBDIR}/satLib.a
INC	= -I./${INCLUDEDIR} # -I./include -I/usr/include 

#CC	= cc 
#CFLAGS = -O3 -march=nocona -DSQL_DEBUG -DNSNOW_DEBUG -DNALBEDOBOUNDS -DNALBEDOLIST_DEBUG -DNDUMP_NORM_PIXL -D NGRID_WRITE_HEAD_CHECK ${INC} 
#CFLAGS = -g -rdynamic -Wall -DNSUNAEDUMP -DSQL_DEBUG -DNALBEDOBOUNDS -DNALBEDOLIST_DEBUG -DNDUMP_NORM_PIXL -DNGRID_WRITE_HEAD_CHECK ${INC} 
CFLAGS = -g -rdynamic -Wall ${INC} 
LIBS	= -lm -ldbi -ldl
OBJS	= forecastOpt.o errorAnalysis.o

.c.o:
	${CC} ${CFLAGS} -c $*.c

forecastOpt: checkSatLib ${OBJS} 
	${CC} -o forecastOpt ${OBJS} ${LIBS} ${CFLAGS} ${SATLIB}
#	cp forecastOpt bin/forecastOpt.new

checkSatLib: 
	make -C ${SATLIBDIR} -f Makefile.satLib

#install:
	#make ${LIBSRC}
	#cp -p ${LIBSRC} ${LLIBDIR}

clean:
	make -C ${SATLIBDIR} -f Makefile.satLib clean
	rm -f ${OBJS}

