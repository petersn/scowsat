
CXXFLAGS=-Wall -Wextra -std=c++17 -g -pthread
CXXFLAGS+=-Ofast

scowsat: scowsat.o
	$(CXX) $(CXXFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f *.o scowsat

