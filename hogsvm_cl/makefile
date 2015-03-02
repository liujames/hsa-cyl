TARGET = hogsvm
INC_PATH = -I./opencv-3.0.0/include 
LIB_PATH = -L./opencv-3.0.0/lib  
LIBS = -lOpenCL -lopencv_core -lopencv_highgui -lopencv_imgcodecs -lopencv_imgproc -lopencv_videoio -lopencv_video -lopencv_objdetect -lopencv_features2d 
SRC = $(wildcard *.cpp) 

$(TARGET) : $(SRC)
	g++ -w  $(INC_PATH) $(LIB_PATH) $(SRC)  -o $(TARGET) $(LIBS) 
clean :
	rm $(TARGET)
