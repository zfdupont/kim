objects = *.o kim

CC = $(CXX)
CXXFLAGS = -Wall

all: kim

kim : kore.o main.o
	$(CC) $(CXXFLAGS) *.cpp -o kore

.PHONY : clean

clean:
	-rm -f $(objects)
