################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Each subdirectory must supply rules for building sources it contributes
main.obj: ../main.cpp $(GEN_OPTS) $(GEN_HDRS)
	@echo 'Building file: $<'
	@echo 'Invoking: C6000 Compiler'
	"C:/ti/ccsv5/tools/compiler/c6000_7.4.4/bin/cl6x" -mv6740 --abi=eabi -O3 -ms3 --include_path="C:/ti/ccsv5/tools/compiler/c6000_7.4.4/include" --include_path="C:/ti/ccsref/ws55/K6747cpp/inc" --define=c6747 --define=LEVEL=3 --define="GREETING=hi there" --display_error_number --diag_warning=225 --diag_wrap=off --preproc_with_compile --preproc_dependency="main.pp" --exceptions $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: $<'
	@echo ' '


