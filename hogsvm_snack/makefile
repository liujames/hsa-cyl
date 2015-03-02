HSA_RUNTIME_PATH = /opt/hsa
HSA_LLVM_PATH = /opt/amd/bin

TARGET = hogsvm
INC_PATH = -I./opencv-3.0.0/include 
LIB_PATH = -L./opencv-3.0.0/lib  -L$(HSA_RUNTIME_PATH)/lib
LIBS = -lopencv_core -lopencv_highgui -lopencv_imgcodecs -lopencv_imgproc -lopencv_videoio -lopencv_video -lopencv_objdetect -lopencv_features2d  -lhsa-runtime64 -lelf
SRC = $(wildcard *.cpp) 









$(TARGET) : $(SRC)
	export LD_LIBRARY_PATH=$(HSA_RUNTIME_PATH)/lib 
	cloc -q -c $(TARGET).cl 
	g++ -w -fpermissive  $(INC_PATH) $(LIB_PATH) $(SRC)  -o $(TARGET) $(LIBS) 
	export LD_LIBRARY_PATH=./opencv-3.0.0/lib
clean :
	rm $(TARGET)
