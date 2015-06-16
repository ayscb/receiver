

TARGET=receiver
OBJ=receiver.o test.o log.o conf.o utils.o load.o datalist.o
CC=gcc
DEBUG=-g

${TARGET} : ${OBJ}
	${CC} ${DEBUG} -o ${TARGET} ${OBJ} -lpthread

log.o : log.c
	${CC} ${DEBUG} -c $^

receiver.o : receiver.c log.h conf.h utils.h datalist.h
	${CC} ${DEBUG} -c $<

test.o : test.c receiver.h
	${CC} ${DEBUG} -c $<

conf.o : conf.c log.h utils.h
	${CC} ${DEBUG} -c $<

utils.o : utils.c
	${CC} ${DEBUG} -c $<

datalist.o : datalist.c log.h
	${CC} ${DEBUG} -c $<

load.o : load.c conf.h log.h datalist.h
	${CC} ${DEBUG} -c $<

.PHONY :clean
clean :
	-rm *.o ${TARGET}  
