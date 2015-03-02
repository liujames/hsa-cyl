TARGET = hogsvm
INC_PATH = -I./opencv-3.0.0/include -I/opt/hsa/include 
LIB_PATH = -L./opencv-3.0.0/lib -L/opt/hsa/lib  
LIBS = -lOpenCL -lopencv_core -lopencv_highgui -lopencv_imgcodecs -lopencv_imgproc -lopencv_videoio -lopencv_video -lopencv_objdetect -lopencv_features2d -lelf -lhsa-runtime64
COMMON_C_FILES = $(wildcard ./common/*.c)
COMMON_OBJ_FILES = $(addprefix ./common/obj/, $(notdir $(COMMON_C_FILES:.c=.o)))
SRC = $(wildcard *.cpp) 

all: $(TARGET) $(TARGET).brig

$(TARGET) : $(SRC) $(COMMON_OBJ_FILES) 
	g++ -w -fpermissive  $(INC_PATH) $(LIB_PATH) $(SRC)  -o $(TARGET)  $(COMMON_OBJ_FILES)  $(LIBS) 

$(TARGET).brig : $(TARGET).cl
	cloc $(TARGET).cl
clean:
	rm -rf  *.brig $(TARGET)


