; Spec: SPRAB89B 11.4 and the ARM EHABI 6.3 - the personality routines a table names (PR0-PR4).
; RTS6x's unwinder reads the PR2 and PR3 tables cpp11 and c90 write itself, so these are the
; symbols the tables depend on and answer _URC_FAILURE (9) should anything call them.
	.global	__c6xabi_unwind_cpp_pr0
	.global	__c6xabi_unwind_cpp_pr1
	.global	__c6xabi_unwind_cpp_pr2
	.global	__c6xabi_unwind_cpp_pr3
	.global	__c6xabi_unwind_cpp_pr4
	.text
__c6xabi_unwind_cpp_pr0:
__c6xabi_unwind_cpp_pr1:
__c6xabi_unwind_cpp_pr2:
__c6xabi_unwind_cpp_pr3:
__c6xabi_unwind_cpp_pr4:
	MVK	9, A4
	B	B3
	NOP	5
