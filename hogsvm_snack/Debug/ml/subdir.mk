################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../ml/ann_mlp.cpp \
../ml/boost.cpp \
../ml/data.cpp \
../ml/em.cpp \
../ml/gbt.cpp \
../ml/inner_functions.cpp \
../ml/kdtree.cpp \
../ml/knearest.cpp \
../ml/lr.cpp \
../ml/nbayes.cpp \
../ml/rtrees.cpp \
../ml/svm.cpp \
../ml/testset.cpp \
../ml/tree.cpp 

OBJS += \
./ml/ann_mlp.o \
./ml/boost.o \
./ml/data.o \
./ml/em.o \
./ml/gbt.o \
./ml/inner_functions.o \
./ml/kdtree.o \
./ml/knearest.o \
./ml/lr.o \
./ml/nbayes.o \
./ml/rtrees.o \
./ml/svm.o \
./ml/testset.o \
./ml/tree.o 

CPP_DEPS += \
./ml/ann_mlp.d \
./ml/boost.d \
./ml/data.d \
./ml/em.d \
./ml/gbt.d \
./ml/inner_functions.d \
./ml/kdtree.d \
./ml/knearest.d \
./ml/lr.d \
./ml/nbayes.d \
./ml/rtrees.d \
./ml/svm.d \
./ml/testset.d \
./ml/tree.d 


# Each subdirectory must supply rules for building sources it contributes
ml/%.o: ../ml/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -D_DEBUGx -I/usr/local/include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


