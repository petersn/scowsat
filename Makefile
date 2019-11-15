
CXXFLAGS=-Ofast -Wall -Wextra -std=c++17 -g

scowsat: scowsat.o
	$(CXX) $(CXXFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f *.o scowsat

