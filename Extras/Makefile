TARGET = Smartdose

all: $(TARGET)

OBJ = Smartdose.o
CXXFLAGS = -Wextra 
CPPFLAGS = -DLOG_LEVEL=3
# LIBMODBUS = -leModbusDEBUG
LIBMODBUS = -leModbus

DEPS := $(OBJ:.o=.d)
	-include $(DEPS)

Smartdose: Smartdose.o
	$(CXX) $^ ${LIBMODBUS} -pthread -lexplain -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -c $<

.PHONY: clean all

clean:
	$(RM) core *.o *.d

