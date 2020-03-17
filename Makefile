objects = *.o kore

CC = $(CXX)
CXXFLAGS = -Wall

all: kore

kore : kore.o main.o
	$(CC) $(CXXFLAGS) *.cpp -o kore

.PHONY : clean

clean:
	-rm -f $(objects)