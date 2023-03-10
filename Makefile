

CFLAGS += -O3  -Wall -Wextra -Werror
TARGETS = glview tgen hilbert

BIN = ~/bin

all:	${TARGETS}

glview:		LDLIBS = -lglut -lGLU -lGL -lXext -lX11 -lm 

test: ${TARGETS}
	./tgen <words | ./glview
	./glview <view.test
	./hilbert | ./glview

install: ${TARGETS}
	cp ${TARGETS} ${BIN}

clean:
	rm -f ${TARGETS}

check:
	cppcheck -q *.[ch]
