TARGET = hogsvm
INC_PATH = -I./opencv-3.0.0/include -I/opt/hsa/include 
LIB_PATH = -L./opencv-3.0.0/lib -L/opt/hsa/lib  
LIBS = -lOpenCL -lopencv_core -lopencv_highgui -lopencv_imgcodecs -lopencv_imgproc -lopencv_videoio -lopencv_video -lopencv_objdetect -lopencv_features2d -lelf -lokra_x86_64
SRC = $(wildcard *.cpp) 

all: $(TARGET) $(TARGET).hsail

$(TARGET) : $(SRC) 
	g++ -Wl,--unresolved-symbols=ignore-in-shared-libs  -w -fpermissive  $(INC_PATH) $(LIB_PATH) $(SRC)  -o $(TARGET)  $(LIBS) 
$(TARGET).hsail : $(TARGET).cl
	cloc -hsail $(TARGET).cl
clean:
	rm -rf  *.brig $(TARGET)


