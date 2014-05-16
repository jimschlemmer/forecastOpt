##CC	= gcc -Wall
CC	= gcc

LIBDIR = ./satLib
INCLUDEDIR = ./include
SATLIB = ${LIBDIR}/satLib.a
INC	= -I./${INCLUDEDIR} # -I./include -I/usr/include 

#CC	= cc 
CFLAGS = -O3 -march=nocona -rdynamic ${INC} 
#CFLAGS = -g -rdynamic -Wall ${INC} 
#CFLAGS = -pg -Wall ${INC} 
LIBS	= -lm -ldbi -ldl
OBJS	= forecastOpt.o errorAnalysis.o optimizer.o 

.c.o:
	${CC} ${CFLAGS} -c $*.c

forecastOpt: checkSatLib ${OBJS} 
	${CC} -o forecastOpt ${OBJS} ${LIBS} ${CFLAGS} ${SATLIB}
#	cp forecastOpt bin/forecastOpt.new

checkSatLib: 
	make -C ${LIBDIR} -f Makefile.satLib

#install:
	#make ${LIBSRC}
	#cp -p ${LIBSRC} ${LLIBDIR}

clean:
	make -C ${LIBDIR} -f Makefile.satLib clean
	rm -f ${OBJS}

