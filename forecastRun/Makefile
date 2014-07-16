##CC	= gcc -Wall
CC	= gcc

LIBDIR = ../forecastLib
INCLUDEDIR = ../include
FORECASTLIB = ${LIBDIR}/forecastLib.a
INC	= -I./${INCLUDEDIR} # -I./include -I/usr/include 

#CC	= cc 
CFLAGS = -O3 -march=nocona -rdynamic ${INC} 
#CFLAGS = -g -rdynamic -Wall ${INC} 
#CFLAGS = -pg -Wall ${INC} 
LIBS	= -lm -ldbi -ldl
OBJS	= forecastRun.o

.c.o:
	${CC} ${CFLAGS} -c $*.c

forecastRun: checkForecastLib ${OBJS} 
	${CC} -o forecastRun ${OBJS} ${LIBS} ${CFLAGS} ${FORECASTLIB}

checkForecastLib: 
	make -C ${LIBDIR} -f Makefile.forecastLib

clean:
	rm -f ${OBJS}

