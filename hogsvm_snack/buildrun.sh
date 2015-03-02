#!/bin/bash
export TARGET=hogsvm
export KERNEL=svmlinear
export INC_PATH=-I./opencv-3.0.0/include
export LIB_PATH=-L./opencv-3.0.0/lib
#export LIBS=-lopencv_core -lopencv_highgui -lopencv_imgcodecs -lopencv_imgproc -lopencv_videoio -lopencv_video -lopencv_objdetect -lopencv_features2d  -lhsa-runtime64 -lelf 
export SRC=*.cpp 



#  Set HSA Environment variables
[ -z $HSA_RUNTIME_PATH ] && HSA_RUNTIME_PATH=/opt/hsa
[ -z HSA_LLVM_PATH ] && HSA_LLVM_PATH=/opt/amd/bin

LD_LIBRARY_PATH=opencv-3.0.0/lib:/opt/hsa/lib



 #Compile accelerated functions
echo 
if [ -f $TARGET.o ] ; then rm $TARGET.o ; fi
echo cloc -q -c $KERNEL.cl 
cloc -q -c $KERNEL.cl 

# Compile Main and link to accelerated functions in 
echo

if [ -f $TARGET ] ; then rm $TARGET ; fi
echo g++ -w -fpermissive  $INC_PATH $LIB_PATH $SRC  -o $TARGET  $KERNEL.o  -lopencv_core -lopencv_highgui -lopencv_imgcodecs -lopencv_imgproc -lopencv_videoio -lopencv_video -lopencv_objdetect -lopencv_features2d -L $HSA_RUNTIME_PATH/lib -lhsa-runtime64 -lelf
#
g++ -w -fpermissive  $INC_PATH $LIB_PATH $SRC  -o $TARGET  $KERNEL.o  -lopencv_core -lopencv_highgui -lopencv_imgcodecs -lopencv_imgproc -lopencv_videoio -lopencv_video -lopencv_objdetect -lopencv_features2d -L $HSA_RUNTIME_PATH/lib -lhsa-runtime64 -lelf
#  Execute
echo
./$TARGET
