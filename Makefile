

CFLAGS += -g -O3 -Wall -Wextra -Werror
TARGETS = glview tgen hilbert fraggen hfrag

BIN = ~/bin

all:	${TARGETS}

glview:		LDLIBS = -lglut -lGLU -lGL -lXext -lX11 -lm 

test: ${TARGETS}
	./fraggen | ./hfrag | ./glview
	./tgen <words | ./glview
	./glview <view.test
	./hilbert | ./glview
	./glview <image.test

install: ${TARGETS}
	cp ${TARGETS} ${BIN}

clean:
	rm -f ${TARGETS}

check:
	cppcheck -q *.[ch]
