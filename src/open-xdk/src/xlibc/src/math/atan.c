// ******************************************************************
// * 
// * proj : openLIBC
// *
// * desc : Totally Free LIC replacement
// *
// * file : atan.c
// *
// * note : This LIBC is TOTALLY free - do what you like with it!!
// *
// ******************************************************************

#include <xlibc/math.h>

#ifdef _MSC_VER

double atan(double x) {
	__asm {
		fld x 
		fld1
		fpatan 
	}
}
double atan2(double y, double x) {
	__asm {
		fld y 
		fld x 
		fpatan
	}
}

float atanf(float x) {
	__asm {
		fld x 
		fld1
		fpatan 
	}
}
float atan2f(float y, float x) {
	__asm {
		fld y 
		fld x 
		fpatan
	}
}

#else

OPENXDK_UNIMPLEMENTEDC(atan/atan2)

#endif 
