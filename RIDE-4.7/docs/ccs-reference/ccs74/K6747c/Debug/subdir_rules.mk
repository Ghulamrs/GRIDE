################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
main.obj: ../main.c $(GEN_OPTS) | $(GEN_HDRS)
	@echo 'Building file: "$<"'
	@echo 'Invoking: C6000 Compiler'
	"C:/ti/ccsv7/tools/compiler/ti-cgt-c6000_8.2.2/bin/cl6x" -mv6740 --include_path="C:/ti/ccsref/ws7/K6747c" --include_path="C:/ti/ccsref/ws7/K6747c/inc" --include_path="C:/ti/ccsv7/tools/compiler/ti-cgt-c6000_8.2.2/include" --define=c6747 -g --diag_warning=225 --diag_wrap=off --display_error_number --preproc_with_compile --preproc_dependency="main.d_raw" $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: "$<"'
	@echo ' '

util.obj: C:/ti/ccsref/src/util.c $(GEN_OPTS) | $(GEN_HDRS)
	@echo 'Building file: "$<"'
	@echo 'Invoking: C6000 Compiler'
	"C:/ti/ccsv7/tools/compiler/ti-cgt-c6000_8.2.2/bin/cl6x" -mv6740 --include_path="C:/ti/ccsref/ws7/K6747c" --include_path="C:/ti/ccsref/ws7/K6747c/inc" --include_path="C:/ti/ccsv7/tools/compiler/ti-cgt-c6000_8.2.2/include" --define=c6747 -g --diag_warning=225 --diag_wrap=off --display_error_number --preproc_with_compile --preproc_dependency="util.d_raw" $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: "$<"'
	@echo ' '


