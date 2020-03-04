##CC	= gcc -Wall
CC	= gcc

LIBDIR = ../forecastLib
INCLUDEDIR = ../include
FORECASTLIB = ${LIBDIR}/forecastLib.a
INC	= -I./${INCLUDEDIR} # -I./include -I/usr/include 

#CC	= cc 
CFLAGS = -O3 -march=nocona -fopenmp ${INC} 
#CFLAGS = -g -rdynamic -Wall ${INC} 
#CFLAGS = -g -Wall -fopenmp ${INC} 
LIBS	= -lm -ldbi -ldl
OBJS	= forecastOpt.o optimizer.o costForecast.o

.c.o:
	${CC} ${CFLAGS} -c $*.c

forecastOpt:	checkForecastLib ${OBJS} 
	${CC} ${CFLAGS} -o forecastOpt ${OBJS} ${LIBS} ${CFLAGS} ${FORECASTLIB}

checkForecastLib: 
	make -C ${LIBDIR} -f Makefile.forecastLib

clean:
	rm -f ${OBJS}
	make -C ${LIBDIR} -f Makefile.forecastLib clean

