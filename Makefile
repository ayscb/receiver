

TARGET=receiver
OBJ=receiver.o test.c conf.o utils.o load.o
CC=gcc
DEBUG=-g

${TARGET} : ${OBJ}
	${CC} ${DEBUG} -o ${TARGET} ${OBJ} -lpthread

receiver.o : receiver.c  conf.h utils.h
	${CC} ${DEBUG} -c $<

conf.o : conf.c utils.h
	${CC} ${DEBUG} -c $<
	
load.o: load.c conf.h
	${CC} ${DEBUG} -c $<
	
utils.o : utils.c
	${CC} ${DEBUG} -c $<

.PHONY :clean
clean :
	-rm *.o ${TARGET}  
